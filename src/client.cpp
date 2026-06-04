/// @file
/// @brief AntennaSwitcher: async wrapper over esphome::api::Client.
///
/// Owns a worker thread running the ESPHome event loop. connect() drives the
/// handshake, entity enumeration and per-channel entity discovery; commands are
/// posted onto the loop (Client is not thread-safe); inbound state updates are
/// mapped to the relevant channel/field and surfaced via a callback.

#include <antenna_switcher/client.hpp>

#include <esphome/api/client.hpp>
#include <esphome/api/client_options.hpp>
#include <esphome/api/connection/connection_state.hpp>
#include <esphome/api/exception.hpp>
#include <esphome/api/model/entity_registry.hpp>
#include <esphome/api/model/entity_store.hpp>
#include <esphome/api/model/entity_type.hpp>
#include <esphome/api/proto/message_id.hpp>
#include <esphome/api/proto/proto_fwd.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace antenna_switcher {

namespace {

constexpr std::size_t channel_count = 2;
constexpr auto connect_timeout = std::chrono::seconds(35);

/// Zero-based array index for a channel (Channel::One ⇒ 0, Channel::Two ⇒ 1).
std::size_t channel_index(const Channel channel) {
    return static_cast<std::size_t>(channel) - 1;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

Mode parse_mode(const std::string& s) {
    if (s == "manual") {
        return Mode::Manual;
    }
    if (s == "auto") {
        return Mode::Auto;
    }
    if (s == "plan") {
        return Mode::Plan;
    }
    return Mode::Unknown;
}

std::vector<int> parse_csv(const std::string& csv) {
    std::vector<int> out;
    std::string token;
    for (const char c : csv) {
        if (c == ',') {
            if (!token.empty()) {
                out.push_back(std::stoi(token));
                token.clear();
            }
        } else if (c != ' ') {
            token += c;
        }
    }
    if (!token.empty()) {
        out.push_back(std::stoi(token));
    }
    return out;
}

}  // namespace

struct AntennaSwitcher::Impl {
    /// The per-channel fields we track from inbound state updates.
    enum class Field { Bearing, Input, Interval, AngleOffset, Mode, Inputs };

    /// Resolved entity keys for one channel.
    struct ChannelEntities {
        std::optional<std::uint32_t> command;      // text  — command box
        std::optional<std::uint32_t> angleOffset;  // number — angle offset (r/w)
        std::optional<std::uint32_t> bearing;      // sensor
        std::optional<std::uint32_t> input;        // sensor
        std::optional<std::uint32_t> interval;     // sensor (µs)
        std::optional<std::uint32_t> mode;         // text_sensor
        std::optional<std::uint32_t> inputs;       // text_sensor (CSV)
    };

    explicit Impl(Options o) : options(std::move(o)), client(make_client_options()) {}

    [[nodiscard]] esphome::api::ClientOptions make_client_options() const {
        esphome::api::ClientOptions co;
        co.host = options.host;
        co.port = options.port;
        co.connection.noise_psk = options.noise_psk;
        co.connection.client_info = options.client_info;
        // We enumerate + subscribe ourselves after async_connect resolves.
        co.subscribe_on_connect = false;
        return co;
    }

    Options options;
    esphome::api::Client client;
    std::thread worker;
    bool running = false;

    std::atomic<bool> connected{false};

    // connect() handshake synchronisation.
    std::mutex connect_mtx;
    std::condition_variable connect_cv;
    bool connect_resolved = false;
    std::error_code connect_error;
    bool list_done = false;
    bool discover_done = false;

    // Discovered handles and the reverse key → (channel, field) map.
    std::array<ChannelEntities, channel_count> ent;
    std::unordered_map<std::uint32_t, std::pair<std::size_t, Field>> key_field;

    // Live state.
    mutable std::mutex state_mtx;
    std::array<ChannelState, channel_count> states;

    std::mutex cb_mtx;
    StateCallback callback;

    // --- loop-thread helpers (no external locking on the client) -------------

    void on_connection_state(const esphome::api::ConnectionState s) {
        connected.store(s == esphome::api::ConnectionState::Connected);
    }

    /// Resolve every channel's entity keys from the populated store.
    void discover() {
        const auto entities = client.entities();
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            const std::string prefix = "_" + std::to_string(ch + 1) + "_";
            auto& [command, angleOffset, bearing, input, interval, mode, inputs] = ent[ch];

            for (const auto& t : entities.texts()) {
                if (starts_with(t.object_id(), prefix) && ends_with(t.object_id(), "command")) {
                    command = t.key();
                }
            }
            for (const auto& n : entities.numbers()) {
                if (starts_with(n.object_id(), prefix) &&
                    ends_with(n.object_id(), "angle_offset")) {
                    angleOffset = n.key();
                }
            }
            for (const auto& s : entities.sensors()) {
                if (!starts_with(s.object_id(), prefix)) {
                    continue;
                }
                if (ends_with(s.object_id(), "bearing")) {
                    bearing = s.key();
                } else if (ends_with(s.object_id(), "input")) {
                    input = s.key();
                } else if (contains(s.object_id(), "interval")) {
                    interval = s.key();
                }
            }
            for (const auto& ts : entities.text_sensors()) {
                if (!starts_with(ts.object_id(), prefix)) {
                    continue;
                }
                if (ends_with(ts.object_id(), "mode")) {
                    mode = ts.key();
                } else if (ends_with(ts.object_id(), "inputs")) {
                    inputs = ts.key();
                }
            }

            register_key(angleOffset, ch, Field::AngleOffset);
            register_key(bearing, ch, Field::Bearing);
            register_key(input, ch, Field::Input);
            register_key(interval, ch, Field::Interval);
            register_key(mode, ch, Field::Mode);
            register_key(inputs, ch, Field::Inputs);

            // Seed the snapshot with whatever state is already known.
            update_field(ch, Field::AngleOffset);
            update_field(ch, Field::Bearing);
            update_field(ch, Field::Input);
            update_field(ch, Field::Interval);
            update_field(ch, Field::Mode);
            update_field(ch, Field::Inputs);
        }
    }

    void
    register_key(const std::optional<std::uint32_t>& key, const std::size_t ch, const Field field) {
        if (key) {
            key_field[*key] = {ch, field};
        }
    }

    /// Read one field's current value out of the store into the snapshot.
    /// Caller must hold no lock; this takes state_mtx. Runs on the loop thread.
    void update_field(const std::size_t ch, const Field field) {
        const ChannelEntities& e = ent[ch];
        std::scoped_lock lk(state_mtx);
        auto& [bearing, activeInput, angleOffset, intervalUs, mode, activeInputs] = states[ch];
        switch (field) {
        case Field::Bearing:
            if (const auto s = client.store().sensor_state(*e.bearing)) {
                bearing = static_cast<int>(std::lround(s->state));
            }
            break;
        case Field::Input:
            if (const auto s = client.store().sensor_state(*e.input)) {
                activeInput = static_cast<int>(std::lround(s->state));
            }
            break;
        case Field::Interval:
            if (const auto s = client.store().sensor_state(*e.interval)) {
                intervalUs = std::lround(s->state);
            }
            break;
        case Field::AngleOffset:
            if (const auto s = client.store().number_state(*e.angleOffset)) {
                angleOffset = static_cast<int>(std::lround(s->state));
            }
            break;
        case Field::Mode:
            if (const auto s = client.store().text_sensor_state(*e.mode)) {
                mode = parse_mode(s->state);
            }
            break;
        case Field::Inputs:
            if (const auto s = client.store().text_sensor_state(*e.inputs)) {
                activeInputs = parse_csv(s->state);
            }
            break;
        }
    }

    /// EntityStore::on_state handler — runs on the loop thread.
    void on_entity_state(const std::uint32_t key) {
        const auto it = key_field.find(key);
        if (it == key_field.end()) {
            return;
        }
        const std::size_t ch = it->second.first;
        update_field(ch, it->second.second);

        StateCallback cb;
        {
            std::scoped_lock lk(cb_mtx);
            cb = callback;
        }
        if (cb) {
            ChannelState snapshot;
            {
                std::scoped_lock lk(state_mtx);
                snapshot = states[ch];
            }
            cb(ch == 0 ? Channel::One : Channel::Two, snapshot);
        }
    }

    /// Throw if any expected entity is missing after discovery.
    void verify_discovery() const {
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            const auto& [command, angleOffset, bearing, input, interval, mode, inputs] = ent[ch];
            const std::string c = std::to_string(ch + 1);
            const auto require = [&](const std::optional<std::uint32_t>& key, const char* what) {
                if (!key) {
                    throw std::runtime_error("antenna-switcher: channel " + c +
                                             " is missing its '" + what + "' entity");
                }
            };
            require(command, "command");
            require(angleOffset, "angle_offset");
            require(bearing, "bearing");
            require(input, "input");
            require(interval, "interval");
            require(mode, "mode");
            require(inputs, "inputs");
        }
    }

    /// Send a command string through a channel's `text` command entity.
    void send_command(const std::size_t ch, const std::string& cmd) {
        client.post([this, ch, cmd] {
            if (const auto& key = ent[ch].command) {
                if (const auto t = client.entities().text(*key)) {
                    t->set(cmd);
                }
            }
        });
    }
};

AntennaSwitcher::AntennaSwitcher(Options opts) : impl_(std::make_unique<Impl>(std::move(opts))) {}

AntennaSwitcher::~AntennaSwitcher() {
    disconnect();
}

void AntennaSwitcher::connect() const {
    if (impl_->running) {
        return;
    }
    impl_->running = true;
    impl_->worker = std::thread([this] { impl_->client.run(); });

    namespace api = esphome::api;

    // Configure handlers and kick off the handshake, all on the loop thread.
    impl_->client.post([this] {
        impl_->client.on_state(
            [this](const api::ConnectionState s) { impl_->on_connection_state(s); });
        impl_->client.store().on_state(
            [this](api::EntityType, const std::uint32_t key) { impl_->on_entity_state(key); });
        impl_->client.on(static_cast<std::uint32_t>(api::MessageId::ListEntitiesDoneResponse),
                         [this](const api::ProtoMessage&) {
                             {
                                 std::scoped_lock lk(impl_->connect_mtx);
                                 impl_->list_done = true;
                             }
                             impl_->connect_cv.notify_all();
                         });
        impl_->client.async_connect([this](const std::error_code ec) {
            if (!ec) {
                impl_->client.subscribe_to_states();
                impl_->client.request_entity_list();
            }
            {
                std::scoped_lock lk(impl_->connect_mtx);
                impl_->connect_error = ec;
                impl_->connect_resolved = true;
            }
            impl_->connect_cv.notify_all();
        });
    });

    // Wait for the handshake to resolve.
    {
        std::unique_lock lk(impl_->connect_mtx);
        if (!impl_->connect_cv.wait_for(
                lk, connect_timeout, [this] { return impl_->connect_resolved; })) {
            lk.unlock();
            shutdown_worker();
            throw api::TimeoutError("antenna-switcher: timed out connecting to " +
                                    impl_->options.host);
        }
        if (impl_->connect_error) {
            const auto ec = impl_->connect_error;
            lk.unlock();
            shutdown_worker();
            throw api::ConnectionError("antenna-switcher: connect failed: " + ec.message());
        }
        // Wait for the entity list to finish streaming.
        if (!impl_->connect_cv.wait_for(lk, connect_timeout, [this] { return impl_->list_done; })) {
            lk.unlock();
            shutdown_worker();
            throw api::TimeoutError("antenna-switcher: timed out enumerating entities");
        }
    }

    // Run discovery on the loop thread (it reads the store concurrently-safely).
    std::exception_ptr discover_err;
    impl_->client.post([this, &discover_err] {
        try {
            impl_->discover();
            impl_->verify_discovery();
        } catch (...) {
            discover_err = std::current_exception();
        }
        {
            std::scoped_lock lk(impl_->connect_mtx);
            impl_->discover_done = true;
        }
        impl_->connect_cv.notify_all();
    });
    {
        std::unique_lock lk(impl_->connect_mtx);
        impl_->connect_cv.wait_for(lk, connect_timeout, [this] { return impl_->discover_done; });
    }
    if (discover_err) {
        shutdown_worker();
        std::rethrow_exception(discover_err);
    }
}

void AntennaSwitcher::shutdown_worker() const {
    if (!impl_->running) {
        return;
    }
    impl_->client.post([this] {
        impl_->client.disconnect();
        impl_->client.stop();
    });
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->running = false;
    impl_->connected.store(false);
}

void AntennaSwitcher::disconnect() const {
    shutdown_worker();
}

bool AntennaSwitcher::isConnected() const {
    return impl_->connected.load();
}

void AntennaSwitcher::setInput(const Channel channel, const int input) const {
    impl_->send_command(channel_index(channel), detail::build_set_input(input));
}

void AntennaSwitcher::startAuto(const Channel channel,
                                const int interval,
                                const TimeUnit unit,
                                const std::vector<int>& inputs) const {
    impl_->send_command(channel_index(channel), detail::build_start_auto(interval, unit, inputs));
}

void AntennaSwitcher::runPlan(const Channel channel,
                              const std::vector<PlanStep>& steps,
                              const bool repeat) const {
    impl_->send_command(channel_index(channel), detail::build_run_plan(steps, repeat));
}

void AntennaSwitcher::stop(const Channel channel) const {
    impl_->send_command(channel_index(channel), detail::build_stop());
}

void AntennaSwitcher::setAngleOffset(const Channel channel, const int degrees) const {
    const std::size_t ch = channel_index(channel);
    impl_->client.post([this, ch, degrees] {
        if (const auto& key = impl_->ent[ch].angleOffset) {
            if (const auto n = impl_->client.entities().number(*key)) {
                n->set(static_cast<float>(degrees));
            }
        }
    });
}

ChannelState AntennaSwitcher::state(const Channel channel) const {
    std::scoped_lock lk(impl_->state_mtx);
    return impl_->states[channel_index(channel)];
}

void AntennaSwitcher::onStateChanged(StateCallback cb) const {
    std::scoped_lock lk(impl_->cb_mtx);
    impl_->callback = std::move(cb);
}

}  // namespace antenna_switcher

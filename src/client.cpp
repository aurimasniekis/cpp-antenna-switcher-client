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
#include <esphome/api/proto/proto_message.hpp>

#include <algorithm>
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
/// How long connect() lets discovered capability states land before falling
/// back to the legacy defaults. Bounded and non-fatal — see connect().
constexpr auto caps_settle_timeout = std::chrono::milliseconds(750);

/// Zero-based array index for a channel (Channel::One ⇒ 0, Channel::Two ⇒ 1).
std::size_t channel_index(const Channel channel) {
    return static_cast<std::size_t>(channel) - 1;
}

std::string channel_name(const std::size_t ch) {
    return std::to_string(ch + 1);
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

/// An entity's two possible spellings, both normalized: the device may send an
/// `object_id` or (API ≥ 1.14) leave the dependency to derive one from the name,
/// and the two derivations differ in how they treat `#` and the `•`.
using EntityIds = std::array<std::string, 2>;

template <class E>
EntityIds normalized_ids(const E& entity) {
    return {detail::normalize_entity_id(entity.object_id()),
            detail::normalize_entity_id(entity.name())};
}

/// True when either spelling is this channel's and ends with `suffix`.
bool id_matches(const EntityIds& ids, const std::string& prefix, const std::string& suffix) {
    return std::any_of(ids.begin(), ids.end(), [&](const std::string& id) {
        return starts_with(id, prefix) && ends_with(id, suffix);
    });
}

/// Looser variant for entities whose id carries a unit suffix (`…interval_us`).
bool id_contains(const EntityIds& ids, const std::string& prefix, const std::string& needle) {
    return std::any_of(ids.begin(), ids.end(), [&](const std::string& id) {
        return starts_with(id, prefix) && contains(id, needle);
    });
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

/// Throw unless the channel advertises `f`. Runs on the caller's thread — a
/// throw from inside a posted lambda would escape on the loop thread.
void check_feature(const std::size_t ch,
                   const Capabilities& caps,
                   const Feature f,
                   const std::string& verb) {
    if (!caps.has(f)) {
        throw UnsupportedRequest("antenna-switcher: channel " + channel_name(ch) +
                                 " does not support '" + verb + "' (features " +
                                 detail::format_feature_flags(caps.features) + ")");
    }
}

/// Throw unless `input` is inside the channel's discovered range.
void check_input(const std::size_t ch, const Capabilities& caps, const int input) {
    if (input < 1 || input > caps.inputCount) {
        throw UnsupportedRequest("antenna-switcher: channel " + channel_name(ch) +
                                 ": input out of range (1.." + std::to_string(caps.inputCount) +
                                 "): " + std::to_string(input));
    }
}

}  // namespace

struct AntennaSwitcher::Impl {
    /// The per-channel fields we track from inbound state updates.
    enum class Field {
        Bearing,
        Input,
        Interval,
        AngleOffset,
        Mode,
        Inputs,
        CapsInputCount,
        CapsCircleCount,
        CapsFeatures,
    };

    /// Resolved entity keys for one channel.
    struct ChannelEntities {
        std::optional<std::uint32_t> command;      // text  — command box
        std::optional<std::uint32_t> angleOffset;  // number — angle offset (r/w)
        std::optional<std::uint32_t> bearing;      // sensor
        std::optional<std::uint32_t> input;        // sensor
        std::optional<std::uint32_t> interval;     // sensor (µs)
        std::optional<std::uint32_t> mode;         // text_sensor
        std::optional<std::uint32_t> inputs;       // text_sensor (CSV)
        // Capability entities — optional: only new ESP32 firmware publishes them.
        std::optional<std::uint32_t> capsInputCount;   // sensor
        std::optional<std::uint32_t> capsCircleCount;  // sensor
        std::optional<std::uint32_t> capsFeatures;     // text_sensor (hex word)
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
    bool caps_done = false;

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
    ///
    /// Entities are matched on the *normalized* object_id or name, so all three
    /// spellings the device and the dependency can produce collapse onto one
    /// key. Within each domain the chains run specific-before-generic, and every
    /// assignment is guarded so the first match wins deterministically —
    /// EntityRegistry::collect iterates an unordered_map.
    void discover() {
        const auto entities = client.entities();
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            const std::string prefix = channel_name(ch) + "_";
            ChannelEntities& e = ent[ch];

            for (const auto& t : entities.texts()) {
                if (id_matches(normalized_ids(t), prefix, "command") && !e.command) {
                    e.command = t.key();
                }
            }
            for (const auto& n : entities.numbers()) {
                if (id_matches(normalized_ids(n), prefix, "angle_offset") && !e.angleOffset) {
                    e.angleOffset = n.key();
                }
            }
            for (const auto& s : entities.sensors()) {
                const EntityIds ids = normalized_ids(s);
                if (id_matches(ids, prefix, "caps_input_count")) {
                    if (!e.capsInputCount) {
                        e.capsInputCount = s.key();
                    }
                } else if (id_matches(ids, prefix, "caps_circle_count")) {
                    if (!e.capsCircleCount) {
                        e.capsCircleCount = s.key();
                    }
                } else if (id_matches(ids, prefix, "bearing")) {
                    if (!e.bearing) {
                        e.bearing = s.key();
                    }
                } else if (id_matches(ids, prefix, "input")) {
                    if (!e.input) {
                        e.input = s.key();
                    }
                } else if (id_contains(ids, prefix, "interval")) {
                    if (!e.interval) {
                        e.interval = s.key();
                    }
                }
            }
            for (const auto& ts : entities.text_sensors()) {
                const EntityIds ids = normalized_ids(ts);
                if (id_matches(ids, prefix, "caps_features")) {
                    if (!e.capsFeatures) {
                        e.capsFeatures = ts.key();
                    }
                } else if (id_matches(ids, prefix, "mode")) {
                    if (!e.mode) {
                        e.mode = ts.key();
                    }
                } else if (id_matches(ids, prefix, "inputs")) {
                    if (!e.inputs) {
                        e.inputs = ts.key();
                    }
                }
            }

            register_key(e.angleOffset, ch, Field::AngleOffset);
            register_key(e.bearing, ch, Field::Bearing);
            register_key(e.input, ch, Field::Input);
            register_key(e.interval, ch, Field::Interval);
            register_key(e.mode, ch, Field::Mode);
            register_key(e.inputs, ch, Field::Inputs);
            register_key(e.capsInputCount, ch, Field::CapsInputCount);
            register_key(e.capsCircleCount, ch, Field::CapsCircleCount);
            register_key(e.capsFeatures, ch, Field::CapsFeatures);

            // Seed the snapshot with whatever state is already known. Capability
            // fields come first so a same-pass input value is read against them.
            update_field(ch, Field::CapsInputCount);
            update_field(ch, Field::CapsCircleCount);
            update_field(ch, Field::CapsFeatures);
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

    // --- store readers -------------------------------------------------------
    // nullopt when the entity is absent, has no state yet, is flagged
    // missing_state, or carries a non-finite value. Every update_field case goes
    // through these, so an undiscovered entity can never be dereferenced.

    [[nodiscard]] std::optional<float> sensor_value(const std::optional<std::uint32_t>& key) const {
        if (!key) {
            return std::nullopt;
        }
        const auto s = client.store().sensor_state(*key);
        if (!s || s->missing_state || !std::isfinite(s->state)) {
            return std::nullopt;
        }
        return s->state;
    }

    [[nodiscard]] std::optional<float> number_value(const std::optional<std::uint32_t>& key) const {
        if (!key) {
            return std::nullopt;
        }
        const auto s = client.store().number_state(*key);
        if (!s || s->missing_state || !std::isfinite(s->state)) {
            return std::nullopt;
        }
        return s->state;
    }

    [[nodiscard]] std::optional<std::string>
    text_value(const std::optional<std::uint32_t>& key) const {
        if (!key) {
            return std::nullopt;
        }
        const auto s = client.store().text_sensor_state(*key);
        if (!s || s->missing_state) {
            return std::nullopt;
        }
        return s->state;
    }

    /// Read one field's current value out of the store into the snapshot.
    /// Caller must hold no lock; this takes state_mtx. Runs on the loop thread.
    /// A field with no usable value keeps whatever it had — it never adopts a 0.
    void update_field(const std::size_t ch, const Field field) {
        const ChannelEntities& e = ent[ch];
        std::scoped_lock lk(state_mtx);
        ChannelState& st = states[ch];
        switch (field) {
        case Field::Bearing:
            if (const auto v = sensor_value(e.bearing)) {
                st.bearing = static_cast<int>(std::lround(*v));
            }
            break;
        case Field::Input:
            if (const auto v = sensor_value(e.input)) {
                st.activeInput = static_cast<int>(std::lround(*v));
                // The device reports 0 after `off`: every RF port isolated.
                st.inputState = st.activeInput == 0 ? InputState::Isolated : InputState::Selected;
            }
            break;
        case Field::Interval:
            if (const auto v = sensor_value(e.interval)) {
                st.intervalUs = std::lround(*v);
            }
            break;
        case Field::AngleOffset:
            if (const auto v = number_value(e.angleOffset)) {
                st.angleOffset = static_cast<int>(std::lround(*v));
            }
            break;
        case Field::Mode:
            if (const auto v = text_value(e.mode)) {
                st.mode = parse_mode(*v);
            }
            break;
        case Field::Inputs:
            if (const auto v = text_value(e.inputs)) {
                st.activeInputs = parse_csv(*v);
            }
            break;
        case Field::CapsInputCount:
            if (const auto v = sensor_value(e.capsInputCount)) {
                // A reported 0 would make every `set` impossible — keep the
                // fallback instead of trusting it.
                if (const int n = static_cast<int>(std::lround(*v)); n > 0) {
                    st.capabilities.inputCount = n;
                    st.capabilities.reported = true;
                }
            }
            break;
        case Field::CapsCircleCount:
            if (const auto v = sensor_value(e.capsCircleCount)) {
                if (const int n = static_cast<int>(std::lround(*v)); n > 0) {
                    st.capabilities.circleCount = n;
                    st.capabilities.reported = true;
                }
            }
            break;
        case Field::CapsFeatures:
            if (const auto v = text_value(e.capsFeatures)) {
                if (const auto flags = detail::parse_feature_flags(*v)) {
                    st.capabilities.features = *flags;
                    st.capabilities.reported = true;
                }
            }
            break;
        }
    }

    static bool is_caps_field(const Field field) {
        return field == Field::CapsInputCount || field == Field::CapsCircleCount ||
               field == Field::CapsFeatures;
    }

    /// True once every *discovered* capability entity has reported a value.
    /// Entities that were never discovered (old ESP32 firmware, which does not
    /// publish them at all) do not hold this back. Runs on the loop thread.
    [[nodiscard]] bool caps_settled() const {
        return std::all_of(ent.begin(), ent.end(), [this](const ChannelEntities& e) {
            return (!e.capsInputCount || sensor_value(e.capsInputCount)) &&
                   (!e.capsCircleCount || sensor_value(e.capsCircleCount)) &&
                   (!e.capsFeatures || text_value(e.capsFeatures));
        });
    }

    void mark_caps_settled() {
        {
            std::scoped_lock lk(connect_mtx);
            caps_done = true;
        }
        connect_cv.notify_all();
    }

    /// EntityStore::on_state handler — runs on the loop thread.
    void on_entity_state(const std::uint32_t key) {
        const auto it = key_field.find(key);
        if (it == key_field.end()) {
            return;
        }
        const std::size_t ch = it->second.first;
        const Field field = it->second.second;
        update_field(ch, field);
        if (is_caps_field(field) && caps_settled()) {
            mark_caps_settled();
        }

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
    ///
    /// All seven of these are wired unconditionally by the device firmware, so
    /// they stay required — including `bearing`, which exists even on a board
    /// with no magnetometer fitted. The three `caps_*` entities are deliberately
    /// *not* required: only newer ESP32 firmware publishes them, and their
    /// absence is exactly what the legacy fallback covers.
    void verify_discovery() const {
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            const ChannelEntities& e = ent[ch];
            const std::string c = channel_name(ch);
            const auto require = [&](const std::optional<std::uint32_t>& key, const char* what) {
                if (!key) {
                    throw std::runtime_error("antenna-switcher: channel " + c +
                                             " is missing its '" + what + "' entity");
                }
            };
            require(e.command, "command");
            require(e.angleOffset, "angle_offset");
            require(e.bearing, "bearing");
            require(e.input, "input");
            require(e.interval, "interval");
            require(e.mode, "mode");
            require(e.inputs, "inputs");
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

    [[nodiscard]] Capabilities capabilities_of(const std::size_t ch) const {
        std::scoped_lock lk(state_mtx);
        return states[ch].capabilities;
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
        const bool settled = impl_->caps_settled();
        {
            std::scoped_lock lk(impl_->connect_mtx);
            impl_->discover_done = true;
            impl_->caps_done = settled;
        }
        impl_->connect_cv.notify_all();
    });
    {
        std::unique_lock lk(impl_->connect_mtx);
        if (!impl_->connect_cv.wait_for(
                lk, connect_timeout, [this] { return impl_->discover_done; })) {
            lk.unlock();
            shutdown_worker();
            throw api::TimeoutError("antenna-switcher: timed out discovering entities");
        }
    }
    if (discover_err) {
        shutdown_worker();
        std::rethrow_exception(discover_err);
    }

    // Bounded and non-fatal: let capability states land so validation uses real
    // values. Normally already satisfied — an already-booted device pushes its
    // retained states before the entity list finishes. A device still probing
    // its switchers (up to ~10 s after boot) simply falls back to the legacy
    // shape, and the real values arrive later through on_entity_state().
    {
        std::unique_lock lk(impl_->connect_mtx);
        impl_->connect_cv.wait_for(lk, caps_settle_timeout, [this] { return impl_->caps_done; });
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

std::string AntennaSwitcher::setInput(const Channel channel, const int input) const {
    const std::size_t ch = channel_index(channel);
    check_input(ch, impl_->capabilities_of(ch), input);
    const std::string cmd = detail::build_set_input(input);
    impl_->send_command(ch, cmd);
    return cmd;
}

std::string AntennaSwitcher::startAuto(const Channel channel,
                                       const int interval,
                                       const TimeUnit unit,
                                       const std::vector<int>& inputs) const {
    const std::size_t ch = channel_index(channel);
    const Capabilities caps = impl_->capabilities_of(ch);
    check_feature(ch, caps, Feature::Auto, "auto");
    for (const int i : inputs) {
        check_input(ch, caps, i);
    }
    const std::string cmd = detail::build_start_auto(interval, unit, inputs, caps.inputCount);
    impl_->send_command(ch, cmd);
    return cmd;
}

std::string AntennaSwitcher::runPlan(const Channel channel,
                                     const std::vector<PlanStep>& steps,
                                     const bool repeat) const {
    const std::size_t ch = channel_index(channel);
    const Capabilities caps = impl_->capabilities_of(ch);
    check_feature(ch, caps, Feature::Plan, "plan");
    for (const PlanStep& step : steps) {
        if (step.kind == PlanStep::Kind::Input) {
            check_input(ch, caps, step.input);
        }
    }
    const std::string cmd = detail::build_run_plan(steps, repeat);
    impl_->send_command(ch, cmd);
    return cmd;
}

std::string AntennaSwitcher::stop(const Channel channel) const {
    const std::string cmd = detail::build_stop();
    impl_->send_command(channel_index(channel), cmd);
    return cmd;
}

std::string AntennaSwitcher::off(const Channel channel) const {
    const std::size_t ch = channel_index(channel);
    check_feature(ch, impl_->capabilities_of(ch), Feature::Off, "off");
    const std::string cmd = detail::build_off();
    impl_->send_command(ch, cmd);
    return cmd;
}

std::string AntennaSwitcher::setAngleOffset(const Channel channel, const int degrees) const {
    const std::size_t ch = channel_index(channel);
    impl_->client.post([this, ch, degrees] {
        if (const auto& key = impl_->ent[ch].angleOffset) {
            if (const auto n = impl_->client.entities().number(*key)) {
                n->set(static_cast<float>(degrees));
            }
        }
    });
    return "angle_offset=" + std::to_string(degrees);
}

ChannelState AntennaSwitcher::state(const Channel channel) const {
    std::scoped_lock lk(impl_->state_mtx);
    return impl_->states[channel_index(channel)];
}

Capabilities AntennaSwitcher::capabilities(const Channel channel) const {
    return impl_->capabilities_of(channel_index(channel));
}

void AntennaSwitcher::onStateChanged(StateCallback cb) const {
    std::scoped_lock lk(impl_->cb_mtx);
    impl_->callback = std::move(cb);
}

}  // namespace antenna_switcher

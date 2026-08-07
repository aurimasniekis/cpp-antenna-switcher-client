#include "cli/commands.hpp"

#include "cli/output.hpp"
#include "cli/signal.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <spdlog/spdlog.h>

namespace asw_cli {

namespace as = antenna_switcher;

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return {};
    const auto end = s.find_last_not_of(" \t");
    return s.substr(begin, end - begin + 1);
}

int parse_int(const std::string& tok, const std::string& what) {
    try {
        std::size_t pos = 0;
        const int v = std::stoi(tok, &pos);
        if (pos != tok.size())
            throw std::invalid_argument(tok);
        return v;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid " + what + ": '" + tok + "'");
    }
}

as::Channel channel_of(const int n) {
    return n == 2 ? as::Channel::Two : as::Channel::One;
}

/// The action to run once connected. It returns the command string actually
/// sent, so the caller no longer has to predict it — the library decides it from
/// the board's discovered capabilities.
using ActionFn = std::function<std::string(const as::AntennaSwitcher&, as::Channel)>;

/// Connect to `host`, run `action` against the target channel, wait up to the
/// settle timeout for a state update on that channel, then emit the result.
///
/// The nested try keeps the exit-code split honest: connection failures reach
/// the outer handler (3/4/5), while a request the board cannot serve — an
/// unadvertised feature, an out-of-range input — is a usage error and exits 2.
int run_action(CliContext& ctx, const std::string& host, const ActionFn& action) {
    const as::Channel ch = ctx.channel();
    const as::Options opt = resolve_options(ctx, host);

    try {
        // Declared before `dev` on purpose: ~AntennaSwitcher joins the loop
        // thread, and the state callback below touches all three.
        bool updated = false;
        std::condition_variable cv;
        std::mutex mtx;

        as::AntennaSwitcher dev(opt);
        dev.onStateChanged([&](const as::Channel c, const as::ChannelState&) {
            if (c != ch)
                return;
            const std::scoped_lock lk(mtx);
            updated = true;
            cv.notify_all();
        });

        dev.connect();
        persist_connection(ctx, host, opt);

        std::string sent;
        try {
            sent = action(dev, ch);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 2;
        }

        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::milliseconds(ctx.timeout_ms()), [&] { return updated; });
        }

        const as::ChannelState st = dev.state(ch);
        const nlohmann::json doc = {{"channel", static_cast<int>(ch)},
                                    {"sent", sent},
                                    {"state", channel_state_to_json(ch, st)}};
        ctx.out.emit(doc, [&](std::ostream& os) {
            os << "sent " << sent << "\n";
            os << channel_state_to_text(ch, st) << "\n";
        });

        dev.disconnect();
        return 0;
    } catch (const std::exception& e) {
        return report_connect_error(ctx, host, e);
    }
}

}  // namespace

std::vector<int> parse_inputs_csv(const std::string& csv) {
    std::vector<int> out;
    const std::string trimmed = trim(csv);
    if (trimmed.empty())
        return out;
    std::stringstream ss(trimmed);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        const std::string t = trim(tok);
        if (t.empty())
            continue;
        const int v = parse_int(t, "input");
        // Only the lower bound is device-independent; the upper one is the
        // board's discovered input count and is checked once connected.
        if (v < 1)
            throw std::runtime_error("input must be 1 or greater: '" + t + "'");
        out.push_back(v);
    }
    return out;
}

std::vector<as::PlanStep> parse_plan_steps(const std::vector<std::string>& tokens) {
    std::vector<as::PlanStep> steps;
    for (const std::string& raw : tokens) {
        const std::string tok = trim(raw);
        if (tok.empty())
            continue;
        const auto ends_with = [&](const char* suffix) {
            const std::string s(suffix);
            return tok.size() > s.size() && tok.compare(tok.size() - s.size(), s.size(), s) == 0;
        };
        if (ends_with("ms")) {
            const int v = parse_int(tok.substr(0, tok.size() - 2), "delay");
            steps.push_back(as::PlanStep::delay_step(v, as::TimeUnit::Ms));
        } else if (ends_with("us")) {
            const int v = parse_int(tok.substr(0, tok.size() - 2), "delay");
            steps.push_back(as::PlanStep::delay_step(v, as::TimeUnit::Us));
        } else {
            const int v = parse_int(tok, "input");
            if (v < 1)
                throw std::runtime_error("input must be 1 or greater: '" + tok + "'");
            steps.push_back(as::PlanStep::input_step(v));
        }
    }
    if (steps.empty())
        throw std::runtime_error("plan needs at least one step");
    return steps;
}

int run_set(CliContext& ctx, const std::string& host, const int input) {
    // The upper bound depends on the board and is enforced by dev.setInput().
    if (input < 1) {
        std::cerr << "error: input must be 1 or greater: " << input << "\n";
        return 2;
    }
    return run_action(ctx, host, [&](const as::AntennaSwitcher& dev, const as::Channel ch) {
        return dev.setInput(ch, input);
    });
}

int run_auto(CliContext& ctx,
             const std::string& host,
             const int interval,
             const bool micros,
             const std::vector<int>& inputs) {
    const as::TimeUnit unit = micros ? as::TimeUnit::Us : as::TimeUnit::Ms;
    return run_action(ctx, host, [&](const as::AntennaSwitcher& dev, const as::Channel ch) {
        return dev.startAuto(ch, interval, unit, inputs);
    });
}

int run_plan(CliContext& ctx,
             const std::string& host,
             const std::vector<as::PlanStep>& steps,
             const bool repeat) {
    return run_action(ctx, host, [&](const as::AntennaSwitcher& dev, const as::Channel ch) {
        return dev.runPlan(ch, steps, repeat);
    });
}

int run_stop(CliContext& ctx, const std::string& host) {
    return run_action(ctx, host, [](const as::AntennaSwitcher& dev, const as::Channel ch) {
        return dev.stop(ch);
    });
}

int run_off(CliContext& ctx, const std::string& host) {
    return run_action(ctx, host, [](const as::AntennaSwitcher& dev, const as::Channel ch) {
        return dev.off(ch);
    });
}

int run_offset(CliContext& ctx, const std::string& host, const int degrees) {
    if (degrees < 0 || degrees > 359) {
        std::cerr << "error: offset out of range (0..359): " << degrees << "\n";
        return 2;
    }
    return run_action(ctx, host, [&](const as::AntennaSwitcher& dev, const as::Channel ch) {
        return dev.setAngleOffset(ch, degrees);
    });
}

int run_state(CliContext& ctx, const std::string& host) {
    const as::Options opt = resolve_options(ctx, host);
    try {
        const as::AntennaSwitcher dev(opt);
        dev.connect();
        persist_connection(ctx, host, opt);

        std::vector<as::Channel> channels;
        if (ctx.globals.channel)
            channels.push_back(ctx.channel());
        else
            channels = {as::Channel::One, as::Channel::Two};

        std::vector<as::ChannelState> snapshots;
        snapshots.reserve(channels.size());
        for (const as::Channel ch : channels)
            snapshots.push_back(dev.state(ch));

        nlohmann::json arr = nlohmann::json::array();
        for (std::size_t i = 0; i < channels.size(); ++i)
            arr.push_back(channel_state_to_json(channels[i], snapshots[i]));

        const nlohmann::json doc = channels.size() == 1 ? arr.front() : arr;
        ctx.out.emit(doc, [&](std::ostream& os) {
            for (std::size_t i = 0; i < channels.size(); ++i) {
                os << channel_state_to_text(channels[i], snapshots[i]) << "\n";
                os << capabilities_to_text(channels[i], snapshots[i].capabilities) << "\n";
            }
        });

        dev.disconnect();
        return 0;
    } catch (const std::exception& e) {
        return report_connect_error(ctx, host, e);
    }
}

int run_watch(CliContext& ctx, const std::string& host, const std::optional<int> channel_filter) {
    const as::Options opt = resolve_options(ctx, host);
    const OutputWriter& out = ctx.out;
    try {
        const as::AntennaSwitcher dev(opt);
        dev.onStateChanged([&](as::Channel c, const as::ChannelState& s) {
            if (channel_filter && static_cast<int>(c) != *channel_filter)
                return;
            out.stream_line(channel_state_to_text(c, s), channel_state_to_json(c, s));
        });

        dev.connect();
        persist_connection(ctx, host, opt);

        // Emit the current snapshot up front so the stream starts with known state.
        std::vector<as::Channel> initial;
        if (channel_filter)
            initial.push_back(channel_of(*channel_filter));
        else
            initial = {as::Channel::One, as::Channel::Two};
        for (const as::Channel ch : initial) {
            const as::ChannelState s = dev.state(ch);
            out.stream_line(channel_state_to_text(ch, s), channel_state_to_json(ch, s));
        }

        install_signal_handlers();
        while (!stop_requested() && dev.isConnected())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        dev.disconnect();
        return 0;
    } catch (const std::exception& e) {
        return report_connect_error(ctx, host, e);
    }
}

int run_config(const CliContext& ctx,
               const std::string& action,
               const std::vector<std::string>& args) {
    if (action.empty() || action == "list") {
        nlohmann::json doc;
        doc["path"] = ctx.config.path().string();
        doc["defaults"] = {{"output", ctx.config.default_output},
                           {"log_level", ctx.config.default_log_level}};
        doc["save_keys"] = ctx.config.save_keys;
        nlohmann::json devs = nlohmann::json::array();
        for (const auto& [host, rec] : ctx.config.devices)
            devs.push_back({{"host", host},
                            {"name", rec.name},
                            {"address", rec.address},
                            {"port", rec.port},
                            {"has_key", !rec.psk.empty()}});
        doc["devices"] = devs;

        ctx.out.emit(doc, [&](std::ostream& os) {
            os << "config: " << ctx.config.path().string() << "\n";
            os << "defaults: output=" << ctx.config.default_output
               << " log_level=" << ctx.config.default_log_level << "\n";
            os << "save_keys: " << (ctx.config.save_keys ? "true" : "false") << "\n";
            os << "devices: " << ctx.config.devices.size() << "\n";
            for (const auto& [host, rec] : ctx.config.devices)
                os << "  " << host << " -> " << (rec.address.empty() ? host : rec.address) << ":"
                   << rec.port << (rec.psk.empty() ? "" : " [key]") << "\n";
        });
        return 0;
    }

    if (action == "path") {
        ctx.out.emit({{"path", ctx.config.path().string()}},
                     [&](std::ostream& os) { os << ctx.config.path().string() << "\n"; });
        return 0;
    }

    if (action == "forget") {
        if (args.empty()) {
            std::cerr << "usage: config forget <alias>\n";
            return 2;
        }
        if (!ctx.config.forget(args.front())) {
            std::cerr << "error: no saved device matching '" << args.front() << "'\n";
            return 2;
        }
        ctx.config.save();
        ctx.log->info("forgot device {}", args.front());
        return 0;
    }

    std::cerr << "error: unknown config action '" << action << "' (expected list|path|forget)\n";
    return 2;
}

}  // namespace asw_cli

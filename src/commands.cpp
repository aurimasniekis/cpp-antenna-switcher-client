/// @file
/// @brief Command-string builders for the antenna-switcher grammar.
///
/// These mirror the device web UI's `_sendCommand` payloads exactly
/// (web/src/esp-antenna-switcher.ts: `_startAuto`, `_runPlan`, `set:N`, `stop`).

#include <antenna_switcher/client.hpp>

#include <string>
#include <vector>

namespace antenna_switcher::detail {

namespace {

std::string join_ints(const std::vector<int>& xs) {
    std::string out;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        out += std::to_string(xs[i]);
    }
    return out;
}

/// True when `in` is exactly 1,2,…,n ascending — the case the web UI encodes as
/// a bare `auto:<delay>`. Deliberately not a size comparison: a full-length but
/// reordered selection (`{3,1,2}` on a three-input board) carries a cycle order
/// that must reach the device.
bool covers_all_in_order(const std::vector<int>& in, const int n) {
    if (n <= 0 || in.size() != static_cast<std::size_t>(n)) {
        return false;
    }
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != static_cast<int>(i) + 1) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string build_set_input(const int input) {
    return "set:" + std::to_string(input);
}

std::string build_start_auto(const int interval,
                             const TimeUnit unit,
                             const std::vector<int>& inputs,
                             const int input_count) {
    // delay = "<n>" (ms) or "<n>u" (µs), matching the UI's `${interval}u`.
    std::string delay = std::to_string(interval);
    if (unit == TimeUnit::Us) {
        delay += 'u';
    }
    // An explicit selection becomes a cycle order; an empty one — or exactly
    // 1,2,…,input_count — cycles every input.
    if (!inputs.empty() && !covers_all_in_order(inputs, input_count)) {
        return "auto:" + delay + ':' + join_ints(inputs);
    }
    return "auto:" + delay;
}

std::string build_run_plan(const std::vector<PlanStep>& steps, const bool repeat) {
    std::string parts;
    bool first = true;
    const auto append = [&](const std::string& part) {
        if (!first) {
            parts += ',';
        }
        parts += part;
        first = false;
    };
    for (const auto& [kind, input, delay, unit] : steps) {
        if (kind == PlanStep::Kind::Input) {
            append(std::to_string(input));
        } else {
            std::string part = "s" + std::to_string(delay);
            if (unit == TimeUnit::Us) {
                part += 'u';
            }
            append(part);
        }
    }
    if (repeat) {
        append("r");
    }
    return "plan:" + parts;
}

std::string build_stop() {
    return "stop";
}

std::string build_off() {
    return "off";
}

}  // namespace antenna_switcher::detail

#include "cli/output.hpp"

#include <iostream>

namespace asw_cli {

namespace as = antenna_switcher;

Format parse_format(const std::string& name) {
    if (name == "json")
        return Format::Json;
    return Format::Text;
}

std::string mode_to_string(const as::Mode mode) {
    switch (mode) {
    case as::Mode::Manual:
        return "manual";
    case as::Mode::Auto:
        return "auto";
    case as::Mode::Plan:
        return "plan";
    case as::Mode::Unknown:
        break;
    }
    return "unknown";
}

as::Mode mode_from_string(const std::string& name) {
    if (name == "manual")
        return as::Mode::Manual;
    if (name == "auto")
        return as::Mode::Auto;
    if (name == "plan")
        return as::Mode::Plan;
    return as::Mode::Unknown;
}

nlohmann::json channel_state_to_json(as::Channel channel, const as::ChannelState& state) {
    return {{"channel", static_cast<int>(channel)},
            {"mode", mode_to_string(state.mode)},
            {"activeInput", state.activeInput},
            {"bearing", state.bearing},
            {"angleOffset", state.angleOffset},
            {"intervalUs", state.intervalUs},
            {"activeInputs", state.activeInputs}};
}

std::string channel_state_to_text(as::Channel channel, const as::ChannelState& state) {
    std::string inputs;
    for (const int i : state.activeInputs) {
        if (!inputs.empty())
            inputs += ',';
        inputs += std::to_string(i);
    }
    std::string out = "#" + std::to_string(static_cast<int>(channel));
    out += "  mode=" + mode_to_string(state.mode);
    out += " input=" + std::to_string(state.activeInput);
    out += " bearing=" + std::to_string(state.bearing);
    out += " offset=" + std::to_string(state.angleOffset);
    out += " interval=" + std::to_string(state.intervalUs) + "us";
    out += " inputs=[" + inputs + "]";
    return out;
}

void OutputWriter::emit(const nlohmann::json& doc,
                        const std::function<void(std::ostream&)>& render_text) const {
    switch (format_) {
    case Format::Json:
        std::cout << doc.dump(2) << "\n";
        break;
    case Format::Text:
        render_text(std::cout);
        break;
    }
}

void OutputWriter::stream_line(const std::string& text, const nlohmann::json& obj) const {
    switch (format_) {
    case Format::Json:
        std::cout << obj.dump() << "\n";
        break;
    case Format::Text:
        std::cout << text << "\n";
        break;
    }
    std::cout.flush();
}

}  // namespace asw_cli

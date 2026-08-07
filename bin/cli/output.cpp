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

std::string input_state_to_string(const as::InputState state) {
    switch (state) {
    case as::InputState::Isolated:
        return "isolated";
    case as::InputState::Selected:
        return "selected";
    case as::InputState::Unknown:
        break;
    }
    return "unknown";
}

namespace {

nlohmann::json capabilities_to_json(const as::Capabilities& caps) {
    nlohmann::json features = nlohmann::json::array();
    for (const as::Feature f : as::detail::decode_features(caps.features))
        features.push_back(as::detail::feature_name(f));
    return {{"inputCount", caps.inputCount},
            {"circleCount", caps.circleCount},
            // A hex string, never a number: a uint64 above 2^53 loses precision
            // in every JavaScript consumer, and high-bit masks are exactly what
            // must not be corrupted.
            {"features", as::detail::format_feature_flags(caps.features)},
            {"featureList", features},
            {"reported", caps.reported}};
}

/// How activeInput reads in text: the number, `isolated` after `off`, or `?`
/// while the device has not reported one.
std::string active_input_to_text(const as::ChannelState& state) {
    switch (state.inputState) {
    case as::InputState::Isolated:
        return "isolated";
    case as::InputState::Selected:
        return std::to_string(state.activeInput);
    case as::InputState::Unknown:
        break;
    }
    return "?";
}

}  // namespace

nlohmann::json channel_state_to_json(as::Channel channel, const as::ChannelState& state) {
    nlohmann::json doc = {{"channel", static_cast<int>(channel)},
                          {"mode", mode_to_string(state.mode)},
                          {"activeInput", state.activeInput},
                          {"inputState", input_state_to_string(state.inputState)},
                          {"angleOffset", state.angleOffset},
                          {"intervalUs", state.intervalUs},
                          {"activeInputs", state.activeInputs},
                          {"capabilities", capabilities_to_json(state.capabilities)}};
    // Omitted rather than zeroed on a board with no compass fitted.
    if (state.capabilities.has(as::Feature::Magnetometer))
        doc["bearing"] = state.bearing;
    return doc;
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
    out += " input=" + active_input_to_text(state);
    if (state.capabilities.has(as::Feature::Magnetometer))
        out += " bearing=" + std::to_string(state.bearing);
    out += " offset=" + std::to_string(state.angleOffset);
    out += " interval=" + std::to_string(state.intervalUs) + "us";
    out += " inputs=[" + inputs + "]";
    return out;
}

std::string capabilities_to_text(as::Channel channel, const as::Capabilities& caps) {
    std::string out = "#" + std::to_string(static_cast<int>(channel));
    out += "  caps inputs=" + std::to_string(caps.inputCount);
    out += " circle=" + std::to_string(caps.circleCount);
    out += " features=[" + as::detail::feature_list(caps.features) + "]";
    if (!caps.reported)
        out += " (defaults)";
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

#pragma once

/// @file
/// @brief Format-aware output writer (text / JSON) plus ChannelState <-> JSON
///        and Mode <-> string helpers.

#include <antenna_switcher/client.hpp>

#include <nlohmann/json.hpp>

#include <functional>
#include <ostream>
#include <string>

namespace asw_cli {

enum class Format { Text, Json };

/// Parse "text"/"json" into a Format (defaults to Text on anything else).
Format parse_format(const std::string& name);

/// Human/JSON name for a Mode ("manual"/"auto"/"plan"/"unknown").
std::string mode_to_string(antenna_switcher::Mode mode);

/// Inverse of mode_to_string (unknown names map to Mode::Unknown).
antenna_switcher::Mode mode_from_string(const std::string& name);

/// Human/JSON name for an InputState ("unknown"/"isolated"/"selected").
std::string input_state_to_string(antenna_switcher::InputState state);

/// Serialize one channel snapshot to JSON:
///   {channel, mode, activeInput, inputState, angleOffset, intervalUs,
///    activeInputs, capabilities}
/// plus `bearing`, which is present only when the board advertises a
/// magnetometer — `jq .bearing` yields null on a board without one.
nlohmann::json channel_state_to_json(antenna_switcher::Channel channel,
                                     const antenna_switcher::ChannelState& state);

/// One-line human rendering of a channel snapshot (used for text output).
/// Deliberately kept short: the interactive panel repaints a fixed three rows,
/// so a line that wraps at 80 columns would break its cursor geometry.
std::string channel_state_to_text(antenna_switcher::Channel channel,
                                  const antenna_switcher::ChannelState& state);

/// One-line human rendering of a channel's advertised capabilities. Rendered
/// separately from the state line (and never inside the interactive panel) so
/// the panel's row count stays fixed.
std::string capabilities_to_text(antenna_switcher::Channel channel,
                                 const antenna_switcher::Capabilities& caps);

/// Writes structured results in the selected format. For one-shot documents use
/// emit(); for incremental streams (watch) use stream_line().
class OutputWriter {
public:
    explicit OutputWriter(const Format format) : format_(format) {}

    [[nodiscard]] Format format() const noexcept {
        return format_;
    }

    /// Emit a complete document. In text mode `render_text` is invoked to write a
    /// human-friendly rendering; in JSON mode `doc` is serialized (pretty).
    void emit(const nlohmann::json& doc,
              const std::function<void(std::ostream&)>& render_text) const;

    /// Emit one record of a continuing stream. Text mode prints `text`; JSON mode
    /// prints `obj` as NDJSON (one compact object per line). Output is flushed so
    /// streams stay live.
    void stream_line(const std::string& text, const nlohmann::json& obj) const;

private:
    Format format_;
};

}  // namespace asw_cli

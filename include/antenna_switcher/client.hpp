#pragma once

/// @file
/// @brief Domain-specific async client for the `antenna-switcher` ESP32 device.
///
/// A thin wrapper over esphome-api-client (`esphome::api`) that speaks the two
/// RS-485 antenna switchers' command grammar (manual / auto / plan / stop / off)
/// and surfaces their live state and advertised capabilities. The generic
/// ESPHome client stays an internal dependency — only the antenna-switcher
/// surface is exposed here.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace antenna_switcher {

/// One of the two switchers on the device. The value is the channel number used
/// in the device's entity names (`#1 …` / `#2 …`).
enum class Channel { One = 1, Two = 2 };

/// Time unit for auto-cycle intervals and plan delays.
enum class TimeUnit { Ms, Us };

/// Operating mode reported by the device.
enum class Mode { Manual, Auto, Plan, Unknown };

/// Optional board features, as advertised by the switcher's `id` command in its
/// 64-bit `flags` word. Bits outside this set are reserved: they are preserved
/// verbatim but carry no meaning here.
enum class Feature : std::uint64_t {
    Magnetometer = 1ULL << 0,  ///< A compass is fitted; `bearing` is meaningful.
    Auto = 1ULL << 1,          ///< `auto:` cycling is implemented.
    Plan = 1ULL << 2,          ///< `plan:` sequences are implemented.
    Off = 1ULL << 3,           ///< `off` (isolate every RF port) is implemented.
    Echo = 1ULL << 4,          ///< The board echoes received commands.
    Led = 1ULL << 5,           ///< Status LED control is fitted.
};

/// The shape assumed for a board that never answered `id` — magnetometer, auto,
/// plan and echo on a ten-input switcher with eight inputs on the compass ring.
/// The device firmware applies the same fallback, so both ends agree.
inline constexpr std::uint64_t legacy_features = 0x0000000000000017ULL;
inline constexpr int legacy_input_count = 10;
inline constexpr int legacy_circle_count = 8;

/// One switcher's advertised shape. Fields start at the legacy fallback and are
/// replaced only when the device reports a usable value.
struct Capabilities {
    int inputCount = legacy_input_count;       ///< Selectable inputs, `1..inputCount`.
    int circleCount = legacy_circle_count;     ///< Inputs arranged on the compass ring.
    std::uint64_t features = legacy_features;  ///< Raw feature bits (see Feature).
    bool reported = false;                     ///< false while still the legacy fallback.

    /// True when the board advertises `f`.
    [[nodiscard]] bool has(Feature f) const noexcept;
};

/// How to read ChannelState::activeInput.
enum class InputState {
    Unknown,   ///< The device has not reported an input yet.
    Isolated,  ///< Every RF port is isolated (after `off`); activeInput is 0.
    Selected,  ///< activeInput names the live input.
};

/// One step of a plan: either switch to an input, or wait for a delay.
struct PlanStep {
    enum class Kind { Input, Delay } kind = Kind::Input;
    int input = 0;
    int delay = 0;
    TimeUnit unit = TimeUnit::Ms;

    /// A step that selects input `n`.
    static PlanStep input_step(const int n) {
        return PlanStep{Kind::Input, n, 0, TimeUnit::Ms};
    }
    /// A step that waits `value` in `unit`.
    static PlanStep delay_step(const int value, const TimeUnit unit = TimeUnit::Ms) {
        return PlanStep{Kind::Delay, 0, value, unit};
    }
};

/// A snapshot of one channel's live state.
struct ChannelState {
    int bearing = 0;            ///< Compass bearing in degrees (only if Feature::Magnetometer).
    int activeInput = 0;        ///< Selected input, 0 when isolated or not yet reported.
    int angleOffset = 0;        ///< Compass offset for input 1, in degrees (0..359).
    long intervalUs = 0;        ///< Auto-cycle interval in microseconds.
    Mode mode = Mode::Unknown;  ///< Manual / auto / plan.
    InputState inputState = InputState::Unknown;  ///< How to read activeInput.
    std::vector<int> activeInputs;                ///< Inputs in the current auto cycle.
    Capabilities capabilities;                    ///< The channel's advertised shape.
};

/// A request rejected locally, before anything reaches the wire: either the
/// board does not advertise the feature, or the input is outside its range.
class UnsupportedRequest : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Everything needed to reach and authenticate with the device.
struct Options {
    std::string host;           ///< Device hostname or IP.
    std::uint16_t port = 6053;  ///< ESPHome native API port.
    std::string noise_psk;      ///< base64 Noise key (api.encryption.key); empty ⇒ plaintext.
    std::string client_info = "antenna-switcher-client";  ///< Identification sent in HelloRequest.
};

/// Async, event-driven controller for the antenna-switcher device.
///
/// Owns a background worker thread that drives the ESPHome event loop. Commands
/// are marshalled onto that loop, so the public methods are safe to call from
/// any thread. State-change callbacks run on the loop thread.
class AntennaSwitcher {
public:
    /// Called with the channel and its new state after each state update.
    using StateCallback = std::function<void(Channel, const ChannelState&)>;

    explicit AntennaSwitcher(Options opts);
    ~AntennaSwitcher();

    AntennaSwitcher(const AntennaSwitcher&) = delete;
    AntennaSwitcher& operator=(const AntennaSwitcher&) = delete;

    /// Connect, enumerate entities, resolve the per-channel handles and give the
    /// advertised capabilities a brief chance to arrive. Blocks until ready;
    /// throws (`esphome::api::ApiError`, `std::runtime_error`) on failure.
    void connect() const;
    /// Gracefully disconnect and stop the worker thread.
    void disconnect() const;
    [[nodiscard]] bool isConnected() const;

    // --- Actions. Each validates against the channel's capabilities on the
    // calling thread and returns the command string actually sent. ------------

    /// Select input `input` (1..Capabilities::inputCount); sends `set:N`.
    std::string setInput(Channel channel, int input) const;
    /// Start auto-cycling `channel` with the given interval. A partial or
    /// reordered `inputs` list becomes an explicit cycle order; an empty list —
    /// or exactly 1,2,…,inputCount — cycles every input. Needs Feature::Auto.
    std::string startAuto(Channel channel,
                          int interval,
                          TimeUnit unit,
                          const std::vector<int>& inputs = {}) const;
    /// Run an ordered plan on `channel`, optionally repeating. Needs Feature::Plan.
    std::string
    runPlan(Channel channel, const std::vector<PlanStep>& steps, bool repeat = false) const;
    /// Stop any auto/plan activity on `channel` (sends `stop`).
    std::string stop(Channel channel) const;
    /// Isolate every RF port on `channel` (sends `off`). Needs Feature::Off.
    std::string off(Channel channel) const;
    /// Set the compass angle offset for input 1 (degrees, written to the number
    /// entity); returns `angle_offset=<deg>`.
    std::string setAngleOffset(Channel channel, int degrees) const;

    /// Thread-safe snapshot of `channel`'s latest state.
    [[nodiscard]] ChannelState state(Channel channel) const;
    /// `channel`'s advertised capabilities, read from the same snapshot as
    /// state() so the two can never disagree.
    [[nodiscard]] Capabilities capabilities(Channel channel) const;
    /// Register a state-change listener (invoked on the loop thread).
    void onStateChanged(StateCallback cb) const;

private:
    void shutdown_worker() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Pure, device-free helpers — the command-string builders (which mirror the
/// device web UI's grammar verbatim) and the capability codecs. All unit-tested
/// offline.
namespace detail {

std::string build_set_input(int input);
std::string build_start_auto(int interval,
                             TimeUnit unit,
                             const std::vector<int>& inputs,
                             int input_count = legacy_input_count);
std::string build_run_plan(const std::vector<PlanStep>& steps, bool repeat);
std::string build_stop();
std::string build_off();

/// Fold an entity `object_id` or `name` into one comparable key: lowercase, with
/// every run of non-`[a-z0-9]` bytes collapsed to a single `_` and leading /
/// trailing `_` trimmed. This makes the device's three spellings of an entity
/// (`#1 Caps • Input Count`, `_1_caps____input_count`, `1_caps__input_count`)
/// all normalize to `1_caps_input_count`.
std::string normalize_entity_id(const std::string& raw);

/// Parse a 1..16 digit hex feature word (optional `0x`, either case, surrounding
/// whitespace allowed). Returns nullopt on anything malformed or too long.
std::optional<std::uint64_t> parse_feature_flags(const std::string& hex);

/// Render a feature word as 16 uppercase hex digits.
std::string format_feature_flags(std::uint64_t flags);

/// Lowercase name of a single feature bit ("magnetometer", "auto", …).
std::string feature_name(Feature f);

/// The known features set in `flags`, in bit order. Reserved bits are ignored.
std::vector<Feature> decode_features(std::uint64_t flags);

/// `decode_features` joined with commas, e.g. "magnetometer,auto,plan,echo".
std::string feature_list(std::uint64_t flags);

}  // namespace detail

}  // namespace antenna_switcher

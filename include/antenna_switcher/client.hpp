#pragma once

/// @file
/// @brief Domain-specific async client for the `antenna-switcher` ESP32 device.
///
/// A thin wrapper over esphome-api-client (`esphome::api`) that speaks the two
/// RS-485 antenna switchers' command grammar (manual / auto / plan / stop) and
/// surfaces their live state. The generic ESPHome client stays an internal
/// dependency — only the antenna-switcher surface is exposed here.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace antenna_switcher {

/// One of the two switchers on the device. The value is the channel number used
/// in the device's entity object-ids (`_1_…` / `_2_…`).
enum class Channel { One = 1, Two = 2 };

/// Time unit for auto-cycle intervals and plan delays.
enum class TimeUnit { Ms, Us };

/// Operating mode reported by the device.
enum class Mode { Manual, Auto, Plan, Unknown };

/// One step of a plan: either switch to an input, or wait for a delay.
struct PlanStep {
    enum class Kind { Input, Delay } kind = Kind::Input;
    int input = 0;
    int delay = 0;
    TimeUnit unit = TimeUnit::Ms;

    /// A step that selects input `n` (1..10).
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
    int bearing = 0;                ///< Current compass bearing in degrees.
    int activeInput = 0;            ///< Currently selected input (1..10), 0 if unknown.
    int angleOffset = 0;            ///< Compass offset for input 1, in degrees (0..359).
    long intervalUs = 0;            ///< Auto-cycle interval in microseconds.
    Mode mode = Mode::Unknown;      ///< Manual / auto / plan.
    std::vector<int> activeInputs;  ///< Inputs in the current auto cycle (CSV-parsed).
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

    /// Connect, enumerate entities, and resolve the per-channel handles. Blocks
    /// until ready; throws (`esphome::api::ApiError`, `std::runtime_error`) on
    /// failure.
    void connect() const;
    /// Gracefully disconnect and stop the worker thread.
    void disconnect() const;
    [[nodiscard]] bool isConnected() const;

    /// Select input `input` (1..10) on `channel` (sends `set:N`).
    void setInput(Channel channel, int input) const;
    /// Start auto-cycling `channel` with the given interval. When `inputs` has
    /// 1..9 entries they form the cycle order; empty or all-ten cycles every input.
    void startAuto(Channel channel,
                   int interval,
                   TimeUnit unit,
                   const std::vector<int>& inputs = {}) const;
    /// Run an ordered plan on `channel`, optionally repeating.
    void runPlan(Channel channel, const std::vector<PlanStep>& steps, bool repeat = false) const;
    /// Stop any auto/plan activity on `channel` (sends `stop`).
    void stop(Channel channel) const;
    /// Set the compass angle offset for input 1 (degrees, written to the number entity).
    void setAngleOffset(Channel channel, int degrees) const;

    /// Thread-safe snapshot of `channel`'s latest state.
    [[nodiscard]] ChannelState state(Channel channel) const;
    /// Register a state-change listener (invoked on the loop thread).
    void onStateChanged(StateCallback cb) const;

private:
    void shutdown_worker() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Command-string builders — pure, device-free, and unit-tested offline. They
/// mirror the device web UI's grammar verbatim.
namespace detail {

std::string build_set_input(int input);
std::string build_start_auto(int interval, TimeUnit unit, const std::vector<int>& inputs);
std::string build_run_plan(const std::vector<PlanStep>& steps, bool repeat);
std::string build_stop();

}  // namespace detail

}  // namespace antenna_switcher

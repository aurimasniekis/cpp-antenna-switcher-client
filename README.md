# Antenna Switcher Client

[![CI](https://github.com/aurimasniekis/cpp-antenna-switcher-client/actions/workflows/ci.yml/badge.svg)](https://github.com/aurimasniekis/cpp-antenna-switcher-client/actions/workflows/ci.yml)
[![Docs](https://github.com/aurimasniekis/cpp-antenna-switcher-client/actions/workflows/docs.yml/badge.svg)](https://aurimasniekis.github.io/cpp-antenna-switcher-client/)

An asynchronous, domain-specific **C++17 client library** for the `antenna-switcher` ESP32 device spoken over the **ESPHome native API** (TCP + protobuf, optionally Noise-encrypted).

It is a thin wrapper over [esphome-api-client](https://github.com/aurimasniekis/cpp-esphome-api) 
(`esphome::api`): the generic
ESPHome client stays an internal dependency, and this library exposes only the antenna-switcher 
surface.

> Looking for the command-line tool? See **[`bin/README.md`](bin/README.md)** for the full
> `antenna-switcher-cli` reference (Docker, Homebrew, every command and flag).

## Quick example

```cpp
#include <antenna_switcher/client.hpp>

#include <iostream>

int main() {
    antenna_switcher::Options opts;
    opts.host = "10.28.0.2";
    // The base64 api.encryption.key from the device firmware YAML. Leave empty
    // for a plaintext device.
    opts.noise_psk = "0a2wipu2cBWSNiaJ2z4bYvdCaRTcgPaJtS535m3IP1g=";

    try {
        antenna_switcher::AntennaSwitcher dev(opts);

        // State updates arrive on the worker thread; keep this callback short.
        dev.onStateChanged([](antenna_switcher::Channel ch,
                              const antenna_switcher::ChannelState& s) {
            std::cout << "channel #" << static_cast<int>(ch)
                      << " input=" << s.activeInput
                      << " bearing=" << s.bearing << "deg\n";
        });

        dev.connect();  // blocks until connected + entities discovered; throws on failure

        // The board reports its own shape — how many inputs it has, and which
        // optional features it implements.
        auto caps = dev.capabilities(antenna_switcher::Channel::One);
        std::cout << "#1 has " << caps.inputCount << " inputs\n";

        dev.setInput(antenna_switcher::Channel::One, 3);  // sends "set:3"

        antenna_switcher::ChannelState now = dev.state(antenna_switcher::Channel::One);
        std::cout << "current input on #1: " << now.activeInput << "\n";

        dev.disconnect();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

Why this works:

- `Options` carries everything needed to reach the device. Only `host` is required; `port` defaults
  to `6053` and an empty `noise_psk` means plaintext.
- Constructing `AntennaSwitcher` starts a background worker thread but does **not** connect yet.
- `connect()` blocks until the handshake, entity enumeration, and per-channel discovery finish, then
  returns. On any failure it throws — so the call belongs inside `try`/`catch`.
- `setInput` and the other actions are safe to call from your thread; internally they are marshalled
  onto the worker's event loop.
- `state()` returns a thread-safe snapshot you can read at any time after `connect()`.

## Installation

Requires CMake ≥ 3.25 and a C++17 compiler. The library target is `antenna_switcher::client` and the
public header is `<antenna_switcher/client.hpp>`.

### CMake FetchContent (recommended)

Pin the release tarball with a checksum:

```cmake
cmake_minimum_required(VERSION 3.25)
project(example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(antenna-switcher-client
    URL      https://github.com/aurimasniekis/cpp-antenna-switcher-client/archive/refs/tags/v0.5.0.tar.gz
    URL_HASH SHA256=6fc51e2b23f6e38bbb971e4bcf3b90f2c4aef22c107db67f73a88969be99da5e
)
FetchContent_MakeAvailable(antenna-switcher-client)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE antenna_switcher::client)
```

### CMake add_subdirectory

```cmake
add_subdirectory(third_party/antenna-switcher-client)
target_link_libraries(example PRIVATE antenna_switcher::client)
```

### Installed package (find_package)

```cmake
find_package(antenna-switcher-client CONFIG REQUIRED)
target_link_libraries(example PRIVATE antenna_switcher::client)
```

Install rules are generated only when esphome-api-client is provided as an **installed** package
(`find_package`) rather than fetched from source. When the dependency is built from source (the
default), `ANTENNA_SWITCHER_INSTALL` is auto-disabled because a source-built dependency cannot be
re-exported. For FetchContent / add_subdirectory consumption this does not matter — you link the
target directly.

## Requirements

- **Language:** C++17. (The project sets `CMAKE_CXX_STANDARD 17` / `target_compile_features(... cxx_std_17)`.)
- **Build system:** CMake ≥ 3.25.
- **Required dependency:** [esphome-api-client](https://github.com/aurimasniekis/cpp-esphome-api)
  (`esphome::api`), resolved via FetchContent from the `v0.5.0` release tarball. As of v0.3.0 the
  protobuf wire layer and the Noise crypto are carried in-tree, so the only thing fetched for the
  library is header-only Asio — there are no system `protobuf`/`libsodium` prerequisites.


## Core concepts

### `antenna_switcher::Options`

Connection settings passed to the controller's constructor.

```cpp
antenna_switcher::Options opts;
opts.host      = "antenna.local";  // hostname or IP (required)
opts.port      = 6053;             // ESPHome native API port (default)
opts.noise_psk = "";               // base64 Noise key; empty => plaintext
// opts.client_info defaults to "antenna-switcher-client"
```

### `antenna_switcher::AntennaSwitcher`

The controller. It owns a background thread that runs the ESPHome event loop. Construction starts the
thread; `connect()` performs the blocking handshake + discovery. It is **non-copyable**.

```cpp
antenna_switcher::AntennaSwitcher dev(opts);
dev.connect();
// ... use dev ...
dev.disconnect();
```

Lifetime: keep the `AntennaSwitcher` alive for as long as you use it; the destructor stops the worker
thread. Do not keep references to a `ChannelState` past the call that produced it — copy what you
need (it is a small value type).

### `Channel`

Which of the two switchers a call targets. The enum value is the channel number used in the device's
entity object-ids.

```cpp
using antenna_switcher::Channel;
dev.setInput(Channel::One, 1);  // switcher #1
dev.setInput(Channel::Two, 4);  // switcher #2
```

### `ChannelState`

A snapshot of one channel's live state. Returned by `state()` and delivered to your `onStateChanged`
callback.

```cpp
struct ChannelState {
    int bearing;                 // compass bearing, degrees (only if Feature::Magnetometer)
    int activeInput;             // selected input, 0 when isolated or not yet reported
    int angleOffset;             // compass offset for input 1, degrees 0..359
    long intervalUs;             // auto-cycle interval, microseconds
    Mode mode;                   // Manual / Auto / Plan / Unknown
    InputState inputState;       // Unknown / Isolated / Selected — how to read activeInput
    std::vector<int> activeInputs;  // inputs in the current auto cycle
    Capabilities capabilities;      // the channel's advertised shape
};
```

`activeInput` is `0` both when the device has never reported an input and when every RF port is
isolated (after `off`). `inputState` distinguishes the two:

```cpp
switch (s.inputState) {
case InputState::Selected: /* s.activeInput names the live input */ break;
case InputState::Isolated: /* every RF port is isolated */         break;
case InputState::Unknown:  /* nothing reported yet */              break;
}
```

### `Mode`

`Manual`, `Auto`, `Plan`, or `Unknown` — the device's current operating mode for a channel.

### `Capabilities` and `Feature`

The hardware is not a fixed shape. Each switcher answers an `id` command with its input count, how
many of those inputs sit on the compass ring, and a 64-bit feature word; the device republishes all
three, and `connect()` picks them up.

```cpp
struct Capabilities {
    int inputCount;         // selectable inputs, 1..inputCount
    int circleCount;        // inputs arranged on the compass ring
    std::uint64_t features; // raw feature bits
    bool reported;          // false while still the legacy fallback

    bool has(Feature f) const noexcept;
};

enum class Feature : std::uint64_t {
    Magnetometer = 1ULL << 0,  // a compass is fitted; `bearing` is meaningful
    Auto         = 1ULL << 1,  // `auto:` cycling is implemented
    Plan         = 1ULL << 2,  // `plan:` sequences are implemented
    Off          = 1ULL << 3,  // `off` (isolate every RF port) is implemented
    Echo         = 1ULL << 4,  // the board echoes received commands
    Led          = 1ULL << 5,  // status LED control is fitted
};
```

Read them with `capabilities(ch)` — or from any `ChannelState`, which carries the same snapshot:

```cpp
const auto caps = dev.capabilities(Channel::One);
if (caps.has(Feature::Off))
    dev.off(Channel::One);
for (int i = 1; i <= caps.inputCount; ++i)
    /* every input this board actually has */;
```

When a board does not report — because it never answered `id`, or because the device firmware
predates the capability entities — the fields hold the **legacy fallback** and `reported` stays
`false`: `legacy_input_count` (10), `legacy_circle_count` (8) and `legacy_features` (`0x17`:
magnetometer, auto, plan, echo). The device firmware applies the same fallback, so both ends agree.

Capabilities can lag up to ~10 s after the device boots. `connect()` waits a bounded **750 ms** for
them and then proceeds with the fallback rather than failing; real values that arrive later reach
you through `onStateChanged`.

Bits outside `Feature` are reserved: they are preserved verbatim in `features` and otherwise ignored.
`antenna_switcher::detail` exposes `parse_feature_flags`, `format_feature_flags`, `feature_name`,
`decode_features` and `feature_list` for rendering them.

### `PlanStep` and `TimeUnit`

A plan is an ordered list of steps. Each step either selects an input or waits a delay. `TimeUnit` is
`Ms` (milliseconds) or `Us` (microseconds).

```cpp
using antenna_switcher::PlanStep;
using antenna_switcher::TimeUnit;

std::vector<PlanStep> steps{
    PlanStep::input_step(1),                  // switch to input 1
    PlanStep::delay_step(100, TimeUnit::Ms),  // wait 100 ms
    PlanStep::input_step(2),                  // switch to input 2
    PlanStep::delay_step(50, TimeUnit::Us),   // wait 50 µs
};
```

### Thread-safety model

- Public methods (`setInput`, `startAuto`, `runPlan`, `stop`, `setAngleOffset`, `state`,
  `isConnected`) are safe to call from any thread; commands are marshalled onto the event loop.
- `onStateChanged` fires **on the worker (loop) thread**. Keep it short and do not call back into a
  blocking `connect()` / `disconnect()` from inside it.
- `state()` returns a mutex-guarded copy.

## Common usage patterns

### Connect and read initial state

```cpp
#include <antenna_switcher/client.hpp>
#include <iostream>

int main() {
    antenna_switcher::Options opts;
    opts.host = "10.28.0.2";

    antenna_switcher::AntennaSwitcher dev(opts);
    try {
        dev.connect();
    } catch (const std::exception& e) {
        std::cerr << "connect failed: " << e.what() << "\n";
        return 1;
    }

    for (auto ch : {antenna_switcher::Channel::One, antenna_switcher::Channel::Two}) {
        auto s = dev.state(ch);
        std::cout << "#" << static_cast<int>(ch) << " mode=" << static_cast<int>(s.mode)
                  << " input=" << s.activeInput << "\n";
    }
    dev.disconnect();
}
```

Scenario: normal successful usage. After `connect()` returns, both channels already have a known
snapshot.

### Select an input (manual mode)

```cpp
dev.setInput(antenna_switcher::Channel::One, 5);  // wire command: "set:5"
```

Inputs are `1..capabilities(ch).inputCount`. Selecting an input puts the channel in manual mode.
An input outside that range throws `UnsupportedRequest` before anything reaches the wire.

### Auto-cycle inputs

```cpp
using antenna_switcher::Channel;
using antenna_switcher::TimeUnit;

// Cycle inputs 1,2,3 every 250 ms  -> "auto:250:1,2,3"
dev.startAuto(Channel::One, 250, TimeUnit::Ms, {1, 2, 3});

// Cycle every input every 5000 microseconds  -> "auto:5000u"
dev.startAuto(Channel::One, 5000, TimeUnit::Us, {});
```

Edge case: an explicit selection becomes a cycle order; an **empty** selection — or exactly
`1,2,…,inputCount` ascending — cycles every input and emits no CSV suffix. A full-length but
*reordered* selection keeps its CSV, because that order is the point. Verified in the tests
(`StartAutoFullSelectionDropsCsv`, `StartAutoFullLengthButReorderedKeepsCsv`).

Needs `Feature::Auto`, and every listed input must be within range.

### Run a plan

```cpp
using antenna_switcher::PlanStep;
using antenna_switcher::TimeUnit;

dev.runPlan(antenna_switcher::Channel::One, {
    PlanStep::input_step(1),
    PlanStep::delay_step(100, TimeUnit::Ms),
    PlanStep::input_step(2),
    PlanStep::delay_step(50, TimeUnit::Us),
}, /*repeat=*/true);   // wire command: "plan:1,s100,2,s50u,r"
```

Pass `repeat=false` (the default) to run the plan once.

### Stop auto/plan activity

```cpp
dev.stop(antenna_switcher::Channel::One);  // wire command: "stop"
```

### Isolate every RF port

```cpp
using antenna_switcher::Channel;
using antenna_switcher::Feature;

if (dev.capabilities(Channel::One).has(Feature::Off))
    dev.off(Channel::One);  // wire command: "off"
```

The channel then reports `mode=manual`, `activeInput=0` with `inputState == InputState::Isolated`,
and an empty `activeInputs`. On a board that does not advertise `Feature::Off`, `off()` throws
`UnsupportedRequest` and sends nothing.

### Set the compass angle offset

```cpp
dev.setAngleOffset(antenna_switcher::Channel::One, 45);  // degrees 0..359 (number entity)
```

Unlike the other actions, this writes the device's `number` entity directly rather than sending a
command string.

### React to live updates

```cpp
#include <antenna_switcher/client.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    antenna_switcher::Options opts;
    opts.host = "10.28.0.2";
    antenna_switcher::AntennaSwitcher dev(opts);

    std::atomic<int> updates{0};
    dev.onStateChanged([&](antenna_switcher::Channel ch,
                           const antenna_switcher::ChannelState& s) {
        // Runs on the worker thread — do minimal work here.
        ++updates;
        std::cout << "#" << static_cast<int>(ch) << " bearing=" << s.bearing << "\n";
    });

    try {
        dev.connect();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    dev.startAuto(antenna_switcher::Channel::One, 250, antenna_switcher::TimeUnit::Ms, {1, 2, 3});
    std::this_thread::sleep_for(std::chrono::seconds(3));  // let updates arrive

    dev.stop(antenna_switcher::Channel::One);
    dev.disconnect();
    std::cout << "received " << updates.load() << " updates\n";
}
```

Scenario: event-driven usage. The callback uses an atomic because it runs on a different thread than
`main`.

## Command grammar

The wire strings are replicated verbatim from the device web UI and pinned by the offline tests. All
command strings are written to the channel's `text` command entity; the angle offset is written to
the `number` entity.

| Method                         | Command string     | Example                |
|--------------------------------|--------------------|------------------------|
| `setInput(ch, n)`              | `set:N`            | `set:5`                |
| `startAuto(ch, v, Ms, inputs)` | `auto:DELAY[:csv]` | `auto:100:1,2,3`       |
| `startAuto(ch, v, Us, {})`     | `auto:DELAYu`      | `auto:5000u`           |
| `runPlan(ch, steps, repeat)`   | `plan:part,…[,r]`  | `plan:1,s100,2,s50u,r` |
| `stop(ch)`                     | `stop`             | `stop`                 |
| `off(ch)`                      | `off`              | `off`                  |
| `setAngleOffset(ch, deg)`      | *(number entity)*  | —                      |

Every action method returns the command string it actually sent, so you never have to predict it.

Rules:

- **auto** — delay is `<v>` for milliseconds or `<v>u` for microseconds. An explicit selection is
  appended as a CSV cycle order; an empty selection, or exactly `1,2,…,inputCount`, cycles every
  input.
- **plan** — each step is either an input number (`N`) or a delay (`sV` ms / `sVu` µs); append `r` to
  repeat.

The pure builders behind these are available in `antenna_switcher::detail`
(`build_set_input`, `build_start_auto`, `build_run_plan`, `build_stop`, `build_off`) and are
unit-testable without a device.

## Error handling

The library reports errors with **exceptions**:

- `connect()` throws on handshake failure, an authentication/encryption error
  (`esphome::api::ApiError` subclasses), or a `std::runtime_error` if an expected entity is missing
  after discovery. Always call it inside `try`/`catch`.
- The action methods (`setInput`, etc.) validate the request against the channel's capabilities on
  the **calling** thread and throw `antenna_switcher::UnsupportedRequest` (a `std::runtime_error`)
  when the board does not advertise the feature or the input is outside its range — nothing is sent
  in that case. Otherwise they marshal work onto the loop without blocking on a device response, so
  they report no device-side failure; observe their effect through `state()` or `onStateChanged`.
- `state()`, `capabilities()` and `isConnected()` do not throw.

Success and failure:

```cpp
antenna_switcher::AntennaSwitcher dev(opts);
try {
    dev.connect();                 // success path
    dev.setInput(antenna_switcher::Channel::One, 2);
    dev.disconnect();
} catch (const std::exception& e) {
    // failure path: bad host, refused connection, wrong/empty PSK, missing entity, ...
    std::cerr << "error: " << e.what() << "\n";
}
```

## API overview

| API                              | Purpose                     | Notes                                                       |
|----------------------------------|-----------------------------|-------------------------------------------------------------|
| `Options`                        | Connection settings         | `host` required; `port`=6053; empty `noise_psk` ⇒ plaintext |
| `AntennaSwitcher(Options)`       | Construct controller        | Starts worker thread; non-copyable                          |
| `connect()`                      | Connect + discover entities | Blocks; throws on failure                                   |
| `disconnect()`                   | Stop and disconnect         | —                                                           |
| `isConnected()`                  | Connection status           | Does not throw                                              |
| `setInput(ch, n)`                | Select input `1..inputCount`| Manual mode; throws `UnsupportedRequest` out of range       |
| `startAuto(ch, v, unit, inputs)` | Auto-cycle                  | Needs `Feature::Auto`; empty/full-ascending ⇒ all           |
| `runPlan(ch, steps, repeat)`     | Run an ordered plan         | Needs `Feature::Plan`; `repeat` defaults to `false`         |
| `stop(ch)`                       | Stop auto/plan              | —                                                           |
| `off(ch)`                        | Isolate every RF port       | Needs `Feature::Off`                                        |
| `setAngleOffset(ch, deg)`        | Set compass offset 0..359   | Writes the `number` entity                                  |
| `state(ch)`                      | Thread-safe snapshot        | Returns a `ChannelState` by value                           |
| `capabilities(ch)`               | Advertised board shape      | Same snapshot as `state(ch).capabilities`                   |
| `onStateChanged(cb)`             | Register state listener     | Fires on the worker thread                                  |

Every action method returns the command string it sent (`setAngleOffset` returns
`angle_offset=<deg>`). None are `[[nodiscard]]` — ignoring the result is fine.

## Examples

| Example                                        | Demonstrates                                                                                                   |
|------------------------------------------------|----------------------------------------------------------------------------------------------------------------|
| [`examples/control.cpp`](examples/control.cpp) | Connect, print the advertised capabilities, exercise set / auto / plan / stop / off / angle-offset on channel #1 (skipping what the board does not advertise), and print each `onStateChanged` event |

Build and run it against a live device:

```sh
make examples
./build/examples/control 10.28.0.2 --key 0a2wipu2cBWSNiaJ2z4bYvdCaRTcgPaJtS535m3IP1g=
```

## Command-line tool

A ready-to-use CLI, `antenna-switcher-cli`, ships in [`bin/`](bin). It wraps this library and adds
text/JSON output, a config of saved devices, a streaming `watch` mode, and an interactive live panel.

```sh
# Build it from source
make cli
./build/bin/antenna-switcher-cli --help

# Or run the published Docker image
docker run --rm --network host aurimasniekis/antenna-switcher-cli -k <psk> 10.28.0.2 state

# Or install via Homebrew
brew install aurimasniekis/tap/antenna-switcher-cli
```

The CLI's own dependencies (CLI11, spdlog, nlohmann/json) are private to the tool and never reach the
library target. **See [`bin/README.md`](bin/README.md) for the complete command reference, flags,
environment variables, output formats, and interactive-mode keys.**

## Building and testing

The build is driven by CMake; a `Makefile` and `CMakePresets.json` memorize the common invocations.

```sh
make test          # configure + build + run the offline tests (Debug)
make release       # optimized build + tests
make examples      # build the example programs
make cli           # build the antenna-switcher-cli tool
make ci            # format-check + clang-tidy + tests + sanitizers + release
```

…or with plain CMake / presets:

```sh
cmake -B build && cmake --build build && ctest --test-dir build
# or
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

## Contributing

Contributions to the library are welcome! If you encounter any issues or have suggestions for
improvements,
please feel free to submit a pull request or open an issue on the project's repository.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
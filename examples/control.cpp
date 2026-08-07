/// @file
/// @brief Live example: connect to an antenna-switcher device, drive each
///        control, and print state changes as they arrive.
///
///   ./control <host> [--key <base64-psk>]
///
/// e.g.  ./control 10.28.0.2 --key 0a2wipu2cBWSNiaJ2z4bYvdCaRTcgPaJtS535m3IP1g=

#include <antenna_switcher/client.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

const char* mode_name(const antenna_switcher::Mode m) {
    switch (m) {
    case antenna_switcher::Mode::Manual:
        return "manual";
    case antenna_switcher::Mode::Auto:
        return "auto";
    case antenna_switcher::Mode::Plan:
        return "plan";
    default:
        return "unknown";
    }
}

/// How to read ChannelState::activeInput: a number, `isolated` after `off`, or
/// `?` while the device has not reported one.
std::string input_text(const antenna_switcher::ChannelState& s) {
    switch (s.inputState) {
    case antenna_switcher::InputState::Isolated:
        return "isolated";
    case antenna_switcher::InputState::Selected:
        return std::to_string(s.activeInput);
    default:
        return "?";
    }
}

void print_state(const antenna_switcher::Channel ch, const antenna_switcher::ChannelState& s) {
    std::string inputs;
    for (const int i : s.activeInputs) {
        if (!inputs.empty()) {
            inputs += ',';
        }
        inputs += std::to_string(i);
    }
    // The bearing is only meaningful on a board that advertises a magnetometer.
    std::string bearing = "n/a";
    if (s.capabilities.has(antenna_switcher::Feature::Magnetometer)) {
        bearing = std::to_string(s.bearing);
    }
    std::printf(
        "  [#%d] mode=%-7s input=%-8s bearing=%-3s offset=%-3d interval=%ldus inputs=[%s]\n",
        static_cast<int>(ch),
        mode_name(s.mode),
        input_text(s).c_str(),
        bearing.c_str(),
        s.angleOffset,
        s.intervalUs,
        inputs.c_str());
}

void print_caps(const antenna_switcher::Channel ch, const antenna_switcher::Capabilities& caps) {
    std::printf("  [#%d] inputs=%d circle=%d features=%s [%s]%s\n",
                static_cast<int>(ch),
                caps.inputCount,
                caps.circleCount,
                antenna_switcher::detail::format_feature_flags(caps.features).c_str(),
                antenna_switcher::detail::feature_list(caps.features).c_str(),
                caps.reported ? "" : " (defaults — the device did not report)");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <host> [--key <base64-psk>]\n", argv[0]);
        return 1;
    }

    antenna_switcher::Options opts;
    opts.host = argv[1];
    for (int i = 2; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--key") == 0 || std::strcmp(argv[i], "-k") == 0) &&
            i + 1 < argc) {
            opts.noise_psk = argv[++i];
        }
    }

    using namespace antenna_switcher;
    using namespace std::chrono_literals;

    try {
        AntennaSwitcher dev(opts);
        dev.onStateChanged([](Channel ch, const ChannelState& s) {
            std::printf("state changed:\n");
            print_state(ch, s);
        });

        std::printf("Connecting to %s ...\n", opts.host.c_str());
        dev.connect();
        std::printf("Connected. Advertised capabilities:\n");
        print_caps(Channel::One, dev.capabilities(Channel::One));
        print_caps(Channel::Two, dev.capabilities(Channel::Two));
        std::printf("Initial state:\n");
        print_state(Channel::One, dev.state(Channel::One));
        print_state(Channel::Two, dev.state(Channel::Two));

        // Every action below is validated against these before anything is sent;
        // asking for an unadvertised feature or an out-of-range input throws
        // UnsupportedRequest without touching the wire.
        const Capabilities caps = dev.capabilities(Channel::One);

        const int input = caps.inputCount >= 3 ? 3 : 1;
        std::printf("\n-> setInput(#1, %d)\n", input);
        std::printf("   sent %s\n", dev.setInput(Channel::One, input).c_str());
        std::this_thread::sleep_for(1s);

        std::printf("\n-> setAngleOffset(#1, 45)\n");
        std::printf("   sent %s\n", dev.setAngleOffset(Channel::One, 45).c_str());
        std::this_thread::sleep_for(1s);

        if (caps.has(Feature::Auto) && caps.inputCount >= 3) {
            std::printf("\n-> startAuto(#1, 250ms, {1,2,3})\n");
            std::printf("   sent %s\n",
                        dev.startAuto(Channel::One, 250, TimeUnit::Ms, {1, 2, 3}).c_str());
            std::this_thread::sleep_for(2s);
        } else {
            std::printf("\n-- skipping startAuto: not advertised by this board\n");
        }

        if (caps.has(Feature::Plan) && caps.inputCount >= 2) {
            std::printf("\n-> runPlan(#1, [in1, 100ms, in2, 50us], repeat)\n");
            std::printf("   sent %s\n",
                        dev.runPlan(Channel::One,
                                    {PlanStep::input_step(1),
                                     PlanStep::delay_step(100, TimeUnit::Ms),
                                     PlanStep::input_step(2),
                                     PlanStep::delay_step(50, TimeUnit::Us)},
                                    true)
                            .c_str());
            std::this_thread::sleep_for(2s);
        } else {
            std::printf("\n-- skipping runPlan: not advertised by this board\n");
        }

        std::printf("\n-> stop(#1)\n");
        std::printf("   sent %s\n", dev.stop(Channel::One).c_str());
        std::this_thread::sleep_for(1s);

        if (caps.has(Feature::Off)) {
            std::printf("\n-> off(#1)\n");
            std::printf("   sent %s\n", dev.off(Channel::One).c_str());
            std::this_thread::sleep_for(1s);
        } else {
            std::printf("\n-- skipping off: not advertised by this board\n");
        }

        std::printf("\nWatching for state changes for 10s (Ctrl-C to quit)...\n");
        std::this_thread::sleep_for(10s);

        dev.disconnect();
        std::printf("Disconnected.\n");
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}

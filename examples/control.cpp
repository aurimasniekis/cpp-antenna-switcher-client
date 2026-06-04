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

void print_state(const antenna_switcher::Channel ch, const antenna_switcher::ChannelState& s) {
    std::string inputs;
    for (const int i : s.activeInputs) {
        if (!inputs.empty()) {
            inputs += ',';
        }
        inputs += std::to_string(i);
    }
    std::printf(
        "  [#%d] mode=%-7s input=%-2d bearing=%-3d offset=%-3d interval=%ldus inputs=[%s]\n",
        static_cast<int>(ch),
        mode_name(s.mode),
        s.activeInput,
        s.bearing,
        s.angleOffset,
        s.intervalUs,
        inputs.c_str());
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
        std::printf("Connected. Initial state:\n");
        print_state(Channel::One, dev.state(Channel::One));
        print_state(Channel::Two, dev.state(Channel::Two));

        std::printf("\n-> setInput(#1, 3)\n");
        dev.setInput(Channel::One, 3);
        std::this_thread::sleep_for(1s);

        std::printf("\n-> setAngleOffset(#1, 45)\n");
        dev.setAngleOffset(Channel::One, 45);
        std::this_thread::sleep_for(1s);

        std::printf("\n-> startAuto(#1, 250ms, {1,2,3})\n");
        dev.startAuto(Channel::One, 250, TimeUnit::Ms, {1, 2, 3});
        std::this_thread::sleep_for(2s);

        std::printf("\n-> runPlan(#1, [in1, 100ms, in2, 50us], repeat)\n");
        dev.runPlan(Channel::One,
                    {PlanStep::input_step(1),
                     PlanStep::delay_step(100, TimeUnit::Ms),
                     PlanStep::input_step(2),
                     PlanStep::delay_step(50, TimeUnit::Us)},
                    true);
        std::this_thread::sleep_for(2s);

        std::printf("\n-> stop(#1)\n");
        dev.stop(Channel::One);
        std::this_thread::sleep_for(1s);

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

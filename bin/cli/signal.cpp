#include "cli/signal.hpp"

#include <atomic>
#include <csignal>

namespace asw_cli {
namespace {

std::atomic<bool> g_stop{false};

extern "C" void handle_signal(int /*sig*/) {
    g_stop.store(true, std::memory_order_relaxed);
}

}  // namespace

void install_signal_handlers() {
    static_cast<void>(std::signal(SIGINT, handle_signal));
    static_cast<void>(std::signal(SIGTERM, handle_signal));
}

bool stop_requested() {
    return g_stop.load(std::memory_order_relaxed);
}

}  // namespace asw_cli

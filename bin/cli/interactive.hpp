#pragma once

/// @file
/// @brief Live interactive mode: an in-place ANSI status panel for both channels
///        plus a command REPL sharing the subcommand verb grammar.

#include "cli/options.hpp"

#include <string>

namespace asw_cli {

/// Connect, paint a live two-channel panel that repaints in place on every state
/// change, and run a "> " REPL accepting set/auto/plan/stop/offset/channel/quit.
/// Returns a process exit code.
int run_interactive(CliContext& ctx, const std::string& host);

}  // namespace asw_cli

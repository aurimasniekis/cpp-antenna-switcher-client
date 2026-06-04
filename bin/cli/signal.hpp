#pragma once

/// @file
/// @brief SIGINT/SIGTERM handling for streaming commands. The handler only sets
///        an atomic flag; loops poll it between bounded sleeps.

namespace asw_cli {

/// Install handlers for SIGINT and SIGTERM (idempotent).
void install_signal_handlers();

/// True once a stop signal has been received.
bool stop_requested();

}  // namespace asw_cli

#pragma once

/// @file
/// @brief Command entry points (each returns a process exit code) plus the
///        plan-step and inputs-CSV token parsers shared with interactive mode.

#include "cli/options.hpp"

#include <antenna_switcher/client.hpp>

#include <optional>
#include <string>
#include <vector>

namespace asw_cli {

/// Parse an "1,2,3" CSV of input numbers (1..10). Throws std::runtime_error on a
/// malformed or out-of-range token. An empty string yields an empty vector.
std::vector<int> parse_inputs_csv(const std::string& csv);

/// Parse plan step tokens: a bare integer is an input (1..10); "<n>ms"/"<n>us"
/// is a delay. Throws std::runtime_error on a malformed token.
std::vector<antenna_switcher::PlanStep> parse_plan_steps(const std::vector<std::string>& tokens);

// --- Action commands (connect, send, settle, emit, disconnect) --------------
int run_set(CliContext& ctx, const std::string& host, int input);
int run_auto(CliContext& ctx,
             const std::string& host,
             int interval,
             bool micros,
             const std::vector<int>& inputs);
int run_plan(CliContext& ctx,
             const std::string& host,
             const std::vector<antenna_switcher::PlanStep>& steps,
             bool repeat);
int run_stop(CliContext& ctx, const std::string& host);
int run_offset(CliContext& ctx, const std::string& host, int degrees);

// --- Read / stream commands -------------------------------------------------
int run_state(CliContext& ctx, const std::string& host);
int run_watch(CliContext& ctx, const std::string& host, std::optional<int> channel_filter);

// --- Host-less commands -----------------------------------------------------
int run_config(const CliContext& ctx,
               const std::string& action,
               const std::vector<std::string>& args);

}  // namespace asw_cli

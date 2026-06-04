/// @file
/// @brief antenna-switcher-cli entry point: build the CLI, classify the host,
///        and dispatch to the selected command.

#include "cli/commands.hpp"
#include "cli/interactive.hpp"
#include "cli/options.hpp"
#include "cli/output.hpp"

#include <CLI/CLI.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/logger.h>

namespace asw_cli {
namespace {

/// Owns the per-invocation config + output + logger that CliContext references.
struct Session {
    Config config;
    std::unique_ptr<OutputWriter> out;
    std::shared_ptr<spdlog::logger> log;
};

Session make_session(const GlobalOptions& g) {
    Session s;
    const std::filesystem::path path =
        g.config_path.empty() ? default_config_path() : std::filesystem::path(g.config_path);
    s.config = Config::load(path);

    const std::string output = !g.output.empty() ? g.output : s.config.default_output;
    const std::string level = !g.log_level.empty() ? g.log_level : s.config.default_log_level;
    s.out = std::make_unique<OutputWriter>(parse_format(output));
    s.log = make_logger(level);
    return s;
}

/// Require a host for device-scoped commands; print a hint and return false if
/// none was supplied via positional / --host / env.
bool require_host(const std::string& host) {
    if (!host.empty())
        return true;
    std::cerr << "error: no host specified. Provide <host>, --host, or set "
                 "ANTENNA_SWITCHER_CLI_HOST.\n";
    return false;
}

int run(const int argc, char** argv) {
    CLI::App app{"antenna-switcher-cli — control the antenna-switcher device",
                 "antenna-switcher-cli"};
    app.set_version_flag("--version", std::string("antenna-switcher-cli ") + cli_version());
    app.require_subcommand(1);

    GlobalOptions g;
    add_global_options(app, g);
    // Positional host alongside the env-bound --host option (distinct CLI names,
    // same target). Either form — or ANTENNA_SWITCHER_CLI_HOST — sets the host.
    app.add_option("HOST", g.host, "Device host/IP or saved alias");

    auto* c_set = app.add_subcommand("set", "Select an input (1..10)")->fallthrough();
    int set_input = 0;
    c_set->add_option("input", set_input, "Input number 1..10")->required();

    auto* c_auto = app.add_subcommand("auto", "Auto-cycle inputs")->fallthrough();
    int auto_interval = 0;
    bool auto_us = false;
    std::string auto_inputs;
    c_auto->add_option("interval", auto_interval, "Cycle interval")->required();
    c_auto->add_flag("--us", auto_us, "Interpret the interval as microseconds");
    c_auto->add_option("--inputs", auto_inputs, "CSV cycle order, e.g. 1,2,3");

    auto* c_plan = app.add_subcommand("plan", "Run an ordered plan")->fallthrough();
    std::vector<std::string> plan_steps;
    bool plan_repeat = false;
    c_plan->add_option("steps", plan_steps, "Steps: input ints and <n>ms/<n>us delays")->required();
    c_plan->add_flag("--repeat", plan_repeat, "Repeat the plan");

    const auto* c_stop = app.add_subcommand("stop", "Stop auto/plan activity")->fallthrough();

    auto* c_offset = app.add_subcommand("offset", "Set the angle offset (0..359)")->fallthrough();
    int offset_deg = 0;
    c_offset->add_option("degrees", offset_deg, "Offset in degrees 0..359")->required();

    const auto* c_state = app.add_subcommand("state", "Show channel state once")->fallthrough();

    auto* c_watch = app.add_subcommand("watch", "Stream state changes until SIGINT")->fallthrough();
    std::optional<int> watch_channel;
    c_watch->add_option("--channel", watch_channel, "Limit to channel 1 or 2")
        ->check(CLI::IsMember({1, 2}));

    const auto* c_inter =
        app.add_subcommand("interactive", "Live panel + command REPL")->fallthrough();

    auto* c_config = app.add_subcommand("config", "Manage saved devices")->fallthrough();
    std::string config_action;
    std::vector<std::string> config_args;
    c_config->add_option("action", config_action, "list|path|forget");
    c_config->add_option("args", config_args, "action arguments");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    auto [config, out, log] = make_session(g);
    CliContext ctx{g, config, *out, log};

    // Host-less command first.
    if (c_config->parsed())
        return run_config(ctx, config_action, config_args);

    if (!require_host(g.host))
        return 2;
    const std::string& host = g.host;

    if (c_set->parsed())
        return run_set(ctx, host, set_input);
    if (c_auto->parsed()) {
        try {
            return run_auto(ctx, host, auto_interval, auto_us, parse_inputs_csv(auto_inputs));
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 2;
        }
    }
    if (c_plan->parsed()) {
        try {
            return run_plan(ctx, host, parse_plan_steps(plan_steps), plan_repeat);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 2;
        }
    }
    if (c_stop->parsed())
        return run_stop(ctx, host);
    if (c_offset->parsed())
        return run_offset(ctx, host, offset_deg);
    if (c_state->parsed())
        return run_state(ctx, host);
    if (c_watch->parsed())
        return run_watch(ctx, host, watch_channel);
    if (c_inter->parsed())
        return run_interactive(ctx, host);

    return 0;
}

}  // namespace
}  // namespace asw_cli

int main(const int argc, char** argv) {
    try {
        return asw_cli::run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}

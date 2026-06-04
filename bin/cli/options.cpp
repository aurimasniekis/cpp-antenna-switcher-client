#include "cli/options.hpp"

#include <antenna_switcher/version.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace asw_cli {

namespace as = antenna_switcher;

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

}  // namespace

as::Channel CliContext::channel() const {
    return (globals.channel.value_or(1) == 2) ? as::Channel::Two : as::Channel::One;
}

const char* cli_version() {
    return ANTENNA_SWITCHER_VERSION_STRING;
}

std::string client_info() {
    return std::string("antenna-switcher-cli/") + cli_version();
}

void add_global_options(CLI::App& app, GlobalOptions& g) {
    app.add_option("--host", g.host, "Device host/IP or saved alias")
        ->envname("ANTENNA_SWITCHER_CLI_HOST");
    app.add_option("-k,--key", g.key, "Base64 Noise PSK (enables encryption)")
        ->envname("ANTENNA_SWITCHER_CLI_KEY");
    app.add_option("-p,--port", g.port, "API TCP port (default 6053)")
        ->envname("ANTENNA_SWITCHER_CLI_PORT");
    app.add_option("-c,--channel", g.channel, "Target channel for actions (1 or 2)")
        ->envname("ANTENNA_SWITCHER_CLI_CHANNEL")
        ->check(CLI::IsMember({1, 2}));
    app.add_option("-o,--output", g.output, "Output format")
        ->envname("ANTENNA_SWITCHER_CLI_OUTPUT")
        ->check(CLI::IsMember({"text", "json"}));
    app.add_option("--log-level", g.log_level, "CLI log verbosity")
        ->envname("ANTENNA_SWITCHER_CLI_LOG_LEVEL")
        ->check(CLI::IsMember(
            {"trace", "debug", "info", "warn", "warning", "error", "err", "critical", "off"}));
    app.add_option("--config", g.config_path, "Path to the config file")
        ->envname("ANTENNA_SWITCHER_CLI_CONFIG");
    app.add_flag("--save-keys,!--no-save-keys", g.save_keys, "Persist encryption keys to config")
        ->envname("ANTENNA_SWITCHER_CLI_SAVE_KEYS");
    app.add_option("--timeout", g.timeout_ms, "Post-action state settle timeout (ms)")
        ->envname("ANTENNA_SWITCHER_CLI_TIMEOUT");
}

std::shared_ptr<spdlog::logger> make_logger(const std::string& level) {
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("antenna-switcher-cli", sink);

    spdlog::level::level_enum lvl = spdlog::level::warn;
    if (level == "trace")
        lvl = spdlog::level::trace;
    else if (level == "debug")
        lvl = spdlog::level::debug;
    else if (level == "info")
        lvl = spdlog::level::info;
    else if (level == "warn" || level == "warning")
        lvl = spdlog::level::warn;
    else if (level == "error" || level == "err")
        lvl = spdlog::level::err;
    else if (level == "critical")
        lvl = spdlog::level::critical;
    else if (level == "off")
        lvl = spdlog::level::off;

    logger->set_level(lvl);
    logger->set_pattern("%^[%l]%$ %v");
    return logger;
}

as::Options resolve_options(const CliContext& ctx, const std::string& host) {
    as::Options opt;
    std::string address = host;

    if (const DeviceRecord* rec = ctx.config.find(host); rec != nullptr) {
        if (!rec->address.empty())
            address = rec->address;
        opt.port = rec->port;
        opt.noise_psk = rec->psk;
    }

    if (ctx.globals.port)
        opt.port = *ctx.globals.port;
    if (!ctx.globals.key.empty())
        opt.noise_psk = ctx.globals.key;

    opt.host = address;
    opt.client_info = client_info();
    return opt;
}

int report_connect_error(const CliContext& ctx, const std::string& host, const std::exception& e) {
    const std::string what = e.what();
    const std::string lower = to_lower(what);

    int code = 5;
    std::string hint;
    if (lower.find("noise") != std::string::npos || lower.find("encrypt") != std::string::npos ||
        lower.find("handshake") != std::string::npos || lower.find("auth") != std::string::npos) {
        code = 3;
        hint = " (check the encryption key with --key / -k)";
    } else if (lower.find("connect") != std::string::npos ||
               lower.find("timeout") != std::string::npos ||
               lower.find("refused") != std::string::npos ||
               lower.find("resolve") != std::string::npos) {
        code = 4;
    }

    ctx.log->debug("connect to {} failed: {}", host, what);
    std::cerr << "error: failed to connect to " << host << ": " << what << hint << "\n";
    return code;
}

void persist_connection(CliContext& ctx, const std::string& host, const as::Options& opt) {
    const bool save = ctx.save_keys();

    DeviceRecord desired;
    if (const DeviceRecord* cur = ctx.config.find(host); cur != nullptr)
        desired = *cur;
    desired.address = opt.host;
    desired.port = opt.port;
    desired.psk = save ? opt.noise_psk : std::string{};

    const DeviceRecord* cur = ctx.config.find(host);
    const bool changed = cur == nullptr || cur->address != desired.address ||
                         cur->port != desired.port || cur->psk != desired.psk;
    if (!changed)
        return;

    ctx.config.remember(host, desired, save);
    try {
        ctx.config.save();
    } catch (const std::exception& ex) {
        ctx.log->warn("could not save config: {}", ex.what());
    }
}

}  // namespace asw_cli

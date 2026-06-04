#pragma once

/// @file
/// @brief Global CLI options, the shared command context, and resolution of
///        flags/env/config into an antenna_switcher::Options. Precedence is
///        flag > env > config > default.

#include "cli/config.hpp"
#include "cli/output.hpp"

#include <antenna_switcher/client.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>

// NOLINTNEXTLINE(readability-identifier-naming) — CLI11's own namespace spelling.
namespace CLI {
class App;
}
namespace spdlog {
// NOLINTNEXTLINE(readability-identifier-naming) — spdlog's own class spelling.
class logger;
}  // namespace spdlog

namespace asw_cli {

/// Raw global options as parsed from flags/env. Empty / nullopt means "not
/// provided" so config records and built-in defaults can take over.
struct GlobalOptions {
    std::string host;                   ///< <host> / --host / ANTENNA_SWITCHER_CLI_HOST
    std::string key;                    ///< -k,--key / ANTENNA_SWITCHER_CLI_KEY
    std::optional<std::uint16_t> port;  ///< -p,--port / ANTENNA_SWITCHER_CLI_PORT
    std::optional<int> channel;         ///< -c,--channel / ANTENNA_SWITCHER_CLI_CHANNEL
    std::string output;                 ///< -o,--output / ANTENNA_SWITCHER_CLI_OUTPUT
    std::string log_level;              ///< --log-level / ANTENNA_SWITCHER_CLI_LOG_LEVEL
    std::string config_path;            ///< --config / ANTENNA_SWITCHER_CLI_CONFIG
    std::optional<bool> save_keys;      ///< --save-keys/--no-save-keys / ..._SAVE_KEYS
    std::optional<int> timeout_ms;      ///< --timeout / ANTENNA_SWITCHER_CLI_TIMEOUT
};

/// Everything a command needs, assembled after parsing. The references point at
/// objects that outlive every command call.
struct CliContext {
    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    const GlobalOptions& globals;
    Config& config;
    const OutputWriter& out;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::shared_ptr<spdlog::logger> log;

    /// Effective key-persistence setting (flag/env override, else config).
    [[nodiscard]] bool save_keys() const {
        return globals.save_keys.value_or(config.save_keys);
    }

    /// Effective post-action settle timeout in milliseconds.
    [[nodiscard]] int timeout_ms() const {
        return globals.timeout_ms.value_or(1500);
    }

    /// Effective target channel for actions (default channel #1).
    [[nodiscard]] antenna_switcher::Channel channel() const;
};

/// The antenna-switcher-cli version string (mirrors the library version).
const char* cli_version();

/// The HelloRequest client_info reported to devices ("antenna-switcher-cli/<v>").
std::string client_info();

/// Register every global option (with env-var bindings) on `app`.
void add_global_options(CLI::App& app, GlobalOptions& g);

/// Build a stderr logger at the given level name (trace/debug/info/warn/error/
/// critical/off). Unknown names default to warn.
std::shared_ptr<spdlog::logger> make_logger(const std::string& level);

/// Layer CLI flags > config record > defaults into an antenna_switcher::Options
/// for `host`.
antenna_switcher::Options resolve_options(const CliContext& ctx, const std::string& host);

/// Print + log a connection failure and return the matching exit code:
///   3 = auth/handshake/encryption, 4 = connection/timeout, 5 = other.
int report_connect_error(const CliContext& ctx, const std::string& host, const std::exception& e);

/// Persist a successful connection's coordinates (address/port/psk) to the
/// config when they changed. Honors the effective save_keys setting.
void persist_connection(CliContext& ctx,
                        const std::string& host,
                        const antenna_switcher::Options& opt);

}  // namespace asw_cli

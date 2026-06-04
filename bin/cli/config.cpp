#include "cli/config.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <system_error>
#include <utility>

namespace asw_cli {
namespace {

constexpr int config_version = 1;

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return (value != nullptr) ? std::string(value) : std::string{};
}

DeviceRecord record_from_json(const nlohmann::json& j) {
    DeviceRecord rec;
    rec.name = j.value("name", std::string{});
    rec.address = j.value("address", std::string{});
    rec.port = j.value("port", static_cast<std::uint16_t>(6053));
    rec.psk = j.value("psk", std::string{});
    return rec;
}

nlohmann::json record_to_json(const DeviceRecord& rec) {
    nlohmann::json j;
    j["name"] = rec.name;
    j["address"] = rec.address;
    j["port"] = rec.port;
    if (!rec.psk.empty())
        j["psk"] = rec.psk;
    return j;
}

}  // namespace

std::filesystem::path config_dir() {
    if (const std::string xdg = env_or_empty("XDG_CONFIG_HOME"); !xdg.empty())
        return std::filesystem::path(xdg) / "antenna-switcher-cli";

    if (const std::string home = env_or_empty("HOME"); !home.empty())
        return std::filesystem::path(home) / ".config" / "antenna-switcher-cli";

    // Last resort: a relative directory in the current working directory.
    return std::filesystem::path(".antenna-switcher-cli");
}

std::filesystem::path default_config_path() {
    return config_dir() / "config.json";
}

Config Config::load(const std::filesystem::path& path) {
    Config cfg;
    cfg.path_ = path;

    std::ifstream in(path);
    if (!in.is_open())
        return cfg;

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        return cfg;  // Corrupt file: fall back to defaults (do not clobber yet).
    }

    if (const auto it = j.find("defaults"); it != j.end() && it->is_object()) {
        cfg.default_output = it->value("output", cfg.default_output);
        cfg.default_log_level = it->value("log_level", cfg.default_log_level);
    }
    cfg.save_keys = j.value("save_keys", cfg.save_keys);
    cfg.plaintext_warned_ = j.value("plaintext_warned", false);

    if (const auto it = j.find("devices"); it != j.end() && it->is_object())
        for (const auto& [host, rec] : it->items())
            cfg.devices[host] = record_from_json(rec);

    return cfg;
}

void Config::save() {
    nlohmann::json j;
    j["version"] = config_version;
    j["defaults"] = {{"output", default_output}, {"log_level", default_log_level}};
    j["save_keys"] = save_keys;
    j["plaintext_warned"] = plaintext_warned_;

    nlohmann::json devs = nlohmann::json::object();
    for (const auto& [host, rec] : devices)
        devs[host] = record_to_json(rec);
    j["devices"] = std::move(devs);

    if (const std::filesystem::path dir = path_.parent_path(); !dir.empty()) {
        std::filesystem::create_directories(dir);
        std::error_code ec;
        std::filesystem::permissions(
            dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
    }

    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open())
        throw std::runtime_error("cannot write config file: " + path_.string());
    out << j.dump(2) << "\n";
    out.close();

    std::error_code ec;
    std::filesystem::permissions(path_,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 ec);
}

const std::string* Config::resolve_key(const std::string& host) const {
    if (const auto it = devices.find(host); it != devices.end())
        return &it->first;
    for (const auto& [key, rec] : devices)
        if (rec.address == host)
            return &key;
    for (const auto& [key, rec] : devices)
        if (!rec.name.empty() && rec.name == host)
            return &key;
    return nullptr;
}

const DeviceRecord* Config::find(const std::string& host) const {
    const std::string* key = resolve_key(host);
    return (key != nullptr) ? &devices.at(*key) : nullptr;
}

void Config::remember(const std::string& host,
                      DeviceRecord record,
                      const bool save_keys_effective) {
    if (!save_keys_effective)
        record.psk.clear();

    if (save_keys_effective && !record.psk.empty() && !plaintext_warned_) {
        std::cerr << "note: encryption keys are stored in plaintext at " << path_.string()
                  << " (file mode 0600). Use --no-save-keys to avoid storing keys.\n";
        plaintext_warned_ = true;
    }

    const std::string* existing = resolve_key(host);
    const std::string key = (existing != nullptr) ? *existing : host;
    devices[key] = std::move(record);
}

bool Config::forget(const std::string& host) {
    const std::string* key = resolve_key(host);
    if (key == nullptr)
        return false;
    devices.erase(*key);
    return true;
}

}  // namespace asw_cli

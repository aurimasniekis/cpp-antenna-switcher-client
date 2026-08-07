/// @file
/// @brief Capability codecs: entity-id normalization and the 64-bit feature word.
///
/// Pure and device-free — everything here is exercised offline by
/// tests/test_capabilities.cpp.

#include <antenna_switcher/client.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace antenna_switcher {

bool Capabilities::has(const Feature f) const noexcept {
    return (features & static_cast<std::uint64_t>(f)) != 0;
}

namespace detail {

namespace {

/// Explicitly ASCII: entity names carry UTF-8 (the `•` is E2 80 A2), and handing
/// a negative `char` to <cctype> is undefined behaviour.
bool is_ascii_alnum(const char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char to_ascii_lower(const char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool is_ascii_space(const char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// 0..15 for a hex digit, -1 for anything else.
int hex_digit(const char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// Every feature bit this build knows about, in ascending bit order.
constexpr std::array<Feature, 6> known_features{
    Feature::Magnetometer,
    Feature::Auto,
    Feature::Plan,
    Feature::Off,
    Feature::Echo,
    Feature::Led,
};

}  // namespace

std::string normalize_entity_id(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    bool separator_pending = false;
    for (const char ch : raw) {
        const char c = to_ascii_lower(ch);
        if (!is_ascii_alnum(c)) {
            separator_pending = true;
            continue;
        }
        // Leading separators are dropped rather than emitted, which also takes
        // care of trimming; trailing ones simply never get flushed.
        if (separator_pending && !out.empty()) {
            out.push_back('_');
        }
        separator_pending = false;
        out.push_back(c);
    }
    return out;
}

std::optional<std::uint64_t> parse_feature_flags(const std::string& hex) {
    std::size_t begin = 0;
    std::size_t end = hex.size();
    while (begin < end && is_ascii_space(hex[begin])) {
        ++begin;
    }
    while (end > begin && is_ascii_space(hex[end - 1])) {
        --end;
    }
    if (end - begin >= 2 && hex[begin] == '0' && (hex[begin + 1] == 'x' || hex[begin + 1] == 'X')) {
        begin += 2;
    }
    const std::size_t digits = end - begin;
    // 16 nibbles is the whole word; anything longer cannot be represented, and
    // rejecting it leaves the caller's fallback in place.
    if (digits == 0 || digits > 16) {
        return std::nullopt;
    }
    // Accumulated nibble by nibble: std::stoul would truncate to 32 bits on
    // Windows, silently dropping the high half of the feature word.
    std::uint64_t value = 0;
    for (std::size_t i = begin; i < end; ++i) {
        const int d = hex_digit(hex[i]);
        if (d < 0) {
            return std::nullopt;
        }
        value = (value << 4) | static_cast<std::uint64_t>(d);
    }
    return value;
}

std::string format_feature_flags(const std::uint64_t flags) {
    // Nibble extraction rather than std::hex, which is locale- and stream-state
    // dependent.
    static constexpr std::string_view digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(16);
    for (int shift = 60; shift >= 0; shift -= 4) {
        out.push_back(digits[(flags >> static_cast<unsigned>(shift)) & 0xFULL]);
    }
    return out;
}

std::string feature_name(const Feature f) {
    switch (f) {
    case Feature::Magnetometer:
        return "magnetometer";
    case Feature::Auto:
        return "auto";
    case Feature::Plan:
        return "plan";
    case Feature::Off:
        return "off";
    case Feature::Echo:
        return "echo";
    case Feature::Led:
        return "led";
    }
    return "unknown";
}

std::vector<Feature> decode_features(const std::uint64_t flags) {
    std::vector<Feature> out;
    for (const Feature f : known_features) {
        if ((flags & static_cast<std::uint64_t>(f)) != 0) {
            out.push_back(f);
        }
    }
    return out;
}

std::string feature_list(const std::uint64_t flags) {
    std::string out;
    for (const Feature f : decode_features(flags)) {
        if (!out.empty()) {
            out += ',';
        }
        out += feature_name(f);
    }
    return out;
}

}  // namespace detail

}  // namespace antenna_switcher

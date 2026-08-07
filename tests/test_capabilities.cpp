/// @file
/// @brief Offline tests for the capability codecs (no device needed).
///
/// These pin the entity-id normalization that lets the client match a device
/// entity by object_id *or* name, and the 64-bit feature-word parsing that must
/// survive a full-width flags value on every platform.
///
/// The `•` in the device's entity names is written as its UTF-8 bytes: the
/// build passes no `/utf-8` flag, so no source file may carry the literal.

#include <antenna_switcher/client.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using antenna_switcher::Capabilities;
using antenna_switcher::Feature;
using namespace antenna_switcher::detail;

/// `#N Caps • Input Count` with the bullet spelled out.
const std::string caps_input_count_name = "#1 Caps \xE2\x80\xA2 Input Count";

TEST(Normalize, CollapsesAllThreeSpellingsOfOneEntity) {
    // Friendly name, the object_id an API < 1.14 device sends, and the object_id
    // the client derives from the name on API >= 1.14 — all one key.
    EXPECT_EQ(normalize_entity_id(caps_input_count_name), "1_caps_input_count");
    EXPECT_EQ(normalize_entity_id("_1_caps____input_count"), "1_caps_input_count");
    EXPECT_EQ(normalize_entity_id("1_caps__input_count"), "1_caps_input_count");
}

TEST(Normalize, TrimsAndCollapsesSeparators) {
    EXPECT_EQ(normalize_entity_id("_1_status___bearing"), "1_status_bearing");
    EXPECT_EQ(normalize_entity_id("#2 Status \xE2\x80\xA2 Bearing"), "2_status_bearing");
    EXPECT_EQ(normalize_entity_id("  #1  Command  "), "1_command");
}

TEST(Normalize, Lowercases) {
    EXPECT_EQ(normalize_entity_id("#1 Caps \xE2\x80\xA2 Features"), "1_caps_features");
    EXPECT_EQ(normalize_entity_id("ABC123"), "abc123");
}

TEST(Normalize, EmptyAndSeparatorOnlyYieldEmpty) {
    EXPECT_EQ(normalize_entity_id(""), "");
    EXPECT_EQ(normalize_entity_id("___"), "");
    EXPECT_EQ(normalize_entity_id(" \xE2\x80\xA2 "), "");
}

TEST(Normalize, DoubleDigitChannelDoesNotMatchChannelOnePrefix) {
    // Channel matching is `starts_with(id, "1_")`; a hypothetical channel 11
    // must not be mistaken for channel 1.
    const std::string id = normalize_entity_id("_11_command");
    EXPECT_EQ(id, "11_command");
    EXPECT_NE(id.compare(0, 2, "1_"), 0);
}

TEST(FeatureFlags, ParsesFullWidthWordWithoutTruncation) {
    // The proof that nothing narrows to 32 bits along the way.
    EXPECT_EQ(parse_feature_flags("FFFFFFFFFFFFFFFF"), 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(parse_feature_flags("8000000000000000"), 1ULL << 63);
    EXPECT_EQ(parse_feature_flags("0000000100000000"), 1ULL << 32);
}

TEST(FeatureFlags, AcceptsPrefixCaseAndWhitespace) {
    EXPECT_EQ(parse_feature_flags("0x3f"), 0x3FULL);
    EXPECT_EQ(parse_feature_flags("0X3F"), 0x3FULL);
    EXPECT_EQ(parse_feature_flags("3F"), 0x3FULL);
    EXPECT_EQ(parse_feature_flags(" 003e "), 0x3EULL);
    EXPECT_EQ(parse_feature_flags("0"), 0ULL);
}

TEST(FeatureFlags, RejectsMalformedAndOverlongInput) {
    EXPECT_FALSE(parse_feature_flags("").has_value());
    EXPECT_FALSE(parse_feature_flags("   ").has_value());
    EXPECT_FALSE(parse_feature_flags("nan").has_value());
    EXPECT_FALSE(parse_feature_flags("12g4").has_value());
    EXPECT_FALSE(parse_feature_flags("0x").has_value());
    EXPECT_FALSE(parse_feature_flags("00000000000000000").has_value());  // 17 digits
}

TEST(FeatureFlags, FormatIsSixteenUppercaseDigits) {
    EXPECT_EQ(format_feature_flags(0x17ULL), "0000000000000017");
    EXPECT_EQ(format_feature_flags(0xFFFFFFFFFFFFFFFFULL), "FFFFFFFFFFFFFFFF");
    EXPECT_EQ(format_feature_flags(0ULL), "0000000000000000");
}

TEST(FeatureFlags, RoundTrips) {
    for (const std::uint64_t v :
         {0ULL, 0x17ULL, 0x3FULL, 1ULL << 63, 0xDEADBEEFCAFEF00DULL, 0xFFFFFFFFFFFFFFFFULL}) {
        EXPECT_EQ(parse_feature_flags(format_feature_flags(v)), v);
    }
}

TEST(FeatureDecode, LegacyWordDecodesToTheLegacySet) {
    EXPECT_EQ(feature_list(0x0017ULL), "magnetometer,auto,plan,echo");
}

TEST(FeatureDecode, FullWordDecodesToEveryKnownFeature) {
    EXPECT_EQ(feature_list(0x003FULL), "magnetometer,auto,plan,off,echo,led");
}

TEST(FeatureDecode, ClearedMagnetometerBitDropsIt) {
    EXPECT_EQ(feature_list(0x003EULL), "auto,plan,off,echo,led");
}

TEST(FeatureDecode, ReservedBitsAreIgnoredNotRejected) {
    const std::uint64_t with_reserved = 0x17ULL | (1ULL << 40) | (1ULL << 63);
    EXPECT_EQ(feature_list(with_reserved), feature_list(0x17ULL));
    EXPECT_EQ(decode_features(with_reserved), decode_features(0x17ULL));
}

TEST(FeatureDecode, DecodeIsInBitOrder) {
    const std::vector<Feature> expected{Feature::Magnetometer,
                                        Feature::Auto,
                                        Feature::Plan,
                                        Feature::Off,
                                        Feature::Echo,
                                        Feature::Led};
    EXPECT_EQ(decode_features(0x3FULL), expected);
    EXPECT_TRUE(decode_features(0ULL).empty());
}

TEST(FeatureDecode, Names) {
    EXPECT_EQ(feature_name(Feature::Magnetometer), "magnetometer");
    EXPECT_EQ(feature_name(Feature::Auto), "auto");
    EXPECT_EQ(feature_name(Feature::Plan), "plan");
    EXPECT_EQ(feature_name(Feature::Off), "off");
    EXPECT_EQ(feature_name(Feature::Echo), "echo");
    EXPECT_EQ(feature_name(Feature::Led), "led");
}

TEST(CapabilitiesDefaults, AreTheLegacyFallback) {
    const Capabilities caps;
    EXPECT_EQ(caps.inputCount, 10);
    EXPECT_EQ(caps.circleCount, 8);
    EXPECT_EQ(caps.features, 0x0000000000000017ULL);
    EXPECT_FALSE(caps.reported);
    EXPECT_TRUE(caps.has(Feature::Magnetometer));
    EXPECT_TRUE(caps.has(Feature::Auto));
    EXPECT_TRUE(caps.has(Feature::Plan));
    EXPECT_TRUE(caps.has(Feature::Echo));
    EXPECT_FALSE(caps.has(Feature::Off));
    EXPECT_FALSE(caps.has(Feature::Led));
}

TEST(CapabilitiesDefaults, MatchTheNamedConstants) {
    EXPECT_EQ(Capabilities{}.inputCount, antenna_switcher::legacy_input_count);
    EXPECT_EQ(Capabilities{}.circleCount, antenna_switcher::legacy_circle_count);
    EXPECT_EQ(Capabilities{}.features, antenna_switcher::legacy_features);
}

}  // namespace

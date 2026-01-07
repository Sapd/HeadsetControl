/**
 * @file test_utilities.cpp
 * @brief Unit tests for utility functions
 *
 * Tests for:
 * - device_utils.hpp: mapSidetoneToDiscrete, mapSidetoneWithToggle, map, voltageToPercent, etc.
 * - utility.hpp: round_to_multiples, spline_battery_level, parse_byte_data, etc.
 * - result_types.hpp: Result<T>, DeviceError
 * - string_utils.hpp: wstring_to_string
 * - feature_utils.hpp: make_success, make_info, make_error
 * - output/output_data.hpp: statusToString, batteryStatusToString
 */

#include "devices/device_utils.hpp"
#include "feature_utils.hpp"
#include "output/output_data.hpp"
#include "result_types.hpp"
#include "string_utils.hpp"
#include "utility.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace headsetcontrol::testing {

// ============================================================================
// Test Utilities
// ============================================================================

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& msg)
        : std::runtime_error(msg)
    {
    }
};

#define ASSERT_TRUE(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            throw TestFailure(std::string("ASSERT_TRUE failed: ") + (msg)); \
        }                                                                   \
    } while (0)

#define ASSERT_FALSE(cond, msg)                                              \
    do {                                                                     \
        if ((cond)) {                                                        \
            throw TestFailure(std::string("ASSERT_FALSE failed: ") + (msg)); \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(expected, actual, msg)                                  \
    do {                                                                  \
        if ((expected) != (actual)) {                                     \
            throw TestFailure(std::string("ASSERT_EQ failed: ") + (msg)); \
        }                                                                 \
    } while (0)

#define ASSERT_NEAR(expected, actual, tolerance, msg)        \
    do {                                                     \
        if (std::abs((expected) - (actual)) > (tolerance)) { \
            std::ostringstream oss;                          \
            oss << "ASSERT_NEAR failed: " << (msg) << "\n"   \
                << "  Expected: " << (expected) << "\n"      \
                << "  Actual:   " << (actual) << "\n"        \
                << "  Tolerance: " << (tolerance);           \
            throw TestFailure(oss.str());                    \
        }                                                    \
    } while (0)

// ============================================================================
// device_utils.hpp Tests
// ============================================================================

void testMapSidetoneToDiscrete4Levels()
{
    std::cout << "  Testing mapSidetoneToDiscrete<4>..." << std::endl;

    // 4 levels: 0, 1, 2, 3
    // Thresholds at 32, 64, 96, 128
    ASSERT_EQ(0, mapSidetoneToDiscrete<4>(0), "0 should map to 0");
    ASSERT_EQ(0, mapSidetoneToDiscrete<4>(1), "1 should map to 0");
    ASSERT_EQ(0, mapSidetoneToDiscrete<4>(31), "31 should map to 0");
    ASSERT_EQ(1, mapSidetoneToDiscrete<4>(32), "32 should map to 1");
    ASSERT_EQ(1, mapSidetoneToDiscrete<4>(63), "63 should map to 1");
    ASSERT_EQ(2, mapSidetoneToDiscrete<4>(64), "64 should map to 2");
    ASSERT_EQ(2, mapSidetoneToDiscrete<4>(95), "95 should map to 2");
    ASSERT_EQ(3, mapSidetoneToDiscrete<4>(96), "96 should map to 3");
    ASSERT_EQ(3, mapSidetoneToDiscrete<4>(128), "128 should map to 3");

    std::cout << "    ✓ mapSidetoneToDiscrete<4> works correctly" << std::endl;
}

void testMapSidetoneToDiscrete11Levels()
{
    std::cout << "  Testing mapSidetoneToDiscrete<11>..." << std::endl;

    // 11 levels: 0-10
    ASSERT_EQ(0, mapSidetoneToDiscrete<11>(0), "0 should map to 0");
    ASSERT_EQ(10, mapSidetoneToDiscrete<11>(128), "128 should map to 10");
    ASSERT_EQ(5, mapSidetoneToDiscrete<11>(64), "64 should map to ~5");

    std::cout << "    ✓ mapSidetoneToDiscrete<11> works correctly" << std::endl;
}

void testMapSidetoneWithToggle()
{
    std::cout << "  Testing mapSidetoneWithToggle..." << std::endl;

    // Off when level is 0
    auto [off, off_level] = mapSidetoneWithToggle(0, 10, 100);
    ASSERT_FALSE(off, "Level 0 should be off");
    ASSERT_EQ(0, off_level, "Off level should be 0");

    // On when level > 0
    auto [on1, on1_level] = mapSidetoneWithToggle(1, 10, 100);
    ASSERT_TRUE(on1, "Level 1 should be on");
    ASSERT_EQ(10, on1_level, "Level 1 should map to device_min (10)");

    auto [on128, on128_level] = mapSidetoneWithToggle(128, 10, 100);
    ASSERT_TRUE(on128, "Level 128 should be on");
    ASSERT_EQ(100, on128_level, "Level 128 should map to device_max (100)");

    auto [on64, on64_level] = mapSidetoneWithToggle(64, 0, 100);
    ASSERT_TRUE(on64, "Level 64 should be on");
    // (64-1) * 100 / 127 ≈ 49
    ASSERT_TRUE(on64_level >= 45 && on64_level <= 55, "Level 64 should map to ~50");

    std::cout << "    ✓ mapSidetoneWithToggle works correctly" << std::endl;
}

void testDeviceUtilsMap()
{
    std::cout << "  Testing device_utils map..." << std::endl;

    // Basic mapping
    ASSERT_EQ(50, headsetcontrol::map(64, 0, 128, 0, 100), "64/128 should map to 50/100");
    ASSERT_EQ(0, headsetcontrol::map(0, 0, 128, 0, 100), "0 should map to 0");
    ASSERT_EQ(100, headsetcontrol::map(128, 0, 128, 0, 100), "128 should map to 100");

    // Reverse mapping
    ASSERT_EQ(64, headsetcontrol::map(50, 0, 100, 0, 128), "50/100 should map to 64/128");

    // Edge case: same min/max
    ASSERT_EQ(42, headsetcontrol::map(100, 50, 50, 42, 42), "Same range should return out_min");

    // Clamping - map() clamps input to valid range
    ASSERT_EQ(100, headsetcontrol::map(200, 0, 128, 0, 100), "Values above max should clamp to 100");
    ASSERT_EQ(0, headsetcontrol::map(-50, 0, 128, 0, 100), "Values below min should clamp to 0");

    std::cout << "    ✓ device_utils map works correctly" << std::endl;
}

void testBytesToUint16()
{
    std::cout << "  Testing bytes_to_uint16..." << std::endl;

    // Big-endian
    ASSERT_EQ(0x1234, bytes_to_uint16_be(0x12, 0x34), "BE: 0x12, 0x34 -> 0x1234");
    ASSERT_EQ(0xFF00, bytes_to_uint16_be(0xFF, 0x00), "BE: 0xFF, 0x00 -> 0xFF00");
    ASSERT_EQ(0x00FF, bytes_to_uint16_be(0x00, 0xFF), "BE: 0x00, 0xFF -> 0x00FF");

    // Little-endian
    ASSERT_EQ(0x1234, bytes_to_uint16_le(0x34, 0x12), "LE: 0x34, 0x12 -> 0x1234");

    std::cout << "    ✓ bytes_to_uint16 works correctly" << std::endl;
}

void testUint16ToBytes()
{
    std::cout << "  Testing uint16_to_bytes..." << std::endl;

    // Big-endian
    auto [be_hi, be_lo] = uint16_to_bytes_be(0x1234);
    ASSERT_EQ(0x12, be_hi, "BE high byte should be 0x12");
    ASSERT_EQ(0x34, be_lo, "BE low byte should be 0x34");

    // Little-endian
    auto [le_lo, le_hi] = uint16_to_bytes_le(0x1234);
    ASSERT_EQ(0x34, le_lo, "LE low byte should be 0x34");
    ASSERT_EQ(0x12, le_hi, "LE high byte should be 0x12");

    std::cout << "    ✓ uint16_to_bytes works correctly" << std::endl;
}

void testVoltageToPercent()
{
    std::cout << "  Testing voltageToPercent..." << std::endl;

    constexpr std::array<std::pair<uint8_t, uint16_t>, 4> calibration { { { 0, 3500 },
        { 50, 3700 },
        { 80, 3900 },
        { 100, 4200 } } };

    ASSERT_EQ(0, voltageToPercent(3400, calibration), "Below min should be 0%");
    ASSERT_EQ(0, voltageToPercent(3500, calibration), "At min should be 0%");
    ASSERT_EQ(100, voltageToPercent(4200, calibration), "At max should be 100%");
    ASSERT_EQ(100, voltageToPercent(4500, calibration), "Above max should be 100%");
    ASSERT_EQ(50, voltageToPercent(3700, calibration), "At 3700mV should be 50%");

    // Interpolation
    auto pct = voltageToPercent(3800, calibration);
    ASSERT_TRUE(pct >= 60 && pct <= 70, "3800mV should interpolate to ~65%");

    std::cout << "    ✓ voltageToPercent works correctly" << std::endl;
}

void testValidateDiscrete()
{
    std::cout << "  Testing validateDiscrete..." << std::endl;

    constexpr std::array<int, 5> allowed { 0, 5, 10, 15, 30 };

    ASSERT_EQ(0, validateDiscrete(0, allowed), "0 should stay 0");
    ASSERT_EQ(0, validateDiscrete(2, allowed), "2 should round to 0");
    ASSERT_EQ(5, validateDiscrete(3, allowed), "3 should round to 5");
    ASSERT_EQ(10, validateDiscrete(12, allowed), "12 should round to 10");
    ASSERT_EQ(15, validateDiscrete(14, allowed), "14 should round to 15");
    ASSERT_EQ(30, validateDiscrete(25, allowed), "25 should round to 30");
    ASSERT_EQ(30, validateDiscrete(100, allowed), "100 should round to 30");

    std::cout << "    ✓ validateDiscrete works correctly" << std::endl;
}

void testClampInactiveTime()
{
    std::cout << "  Testing clampInactiveTime..." << std::endl;

    ASSERT_EQ(30, clampInactiveTime(30, 90), "30 within range should stay 30");
    ASSERT_EQ(90, clampInactiveTime(100, 90), "100 should clamp to 90");
    ASSERT_EQ(0, clampInactiveTime(0, 90), "0 should stay 0");

    std::cout << "    ✓ clampInactiveTime works correctly" << std::endl;
}

void testMapDiscrete()
{
    std::cout << "  Testing mapDiscrete..." << std::endl;

    // 4 discrete levels (indices 0-3, thresholds at 0, 43, 86, 128)
    constexpr std::array<uint8_t, 4> levels4 { 0x00, 0x04, 0x08, 0x0C };
    ASSERT_EQ(0x00, mapDiscrete(uint8_t { 0 }, levels4), "0 should map to first level");
    ASSERT_EQ(0x00, mapDiscrete(uint8_t { 42 }, levels4), "42 should map to first level");
    ASSERT_EQ(0x04, mapDiscrete(uint8_t { 43 }, levels4), "43 should map to second level");
    ASSERT_EQ(0x04, mapDiscrete(uint8_t { 85 }, levels4), "85 should map to second level");
    ASSERT_EQ(0x08, mapDiscrete(uint8_t { 96 }, levels4), "96 should map to third level");
    ASSERT_EQ(0x0C, mapDiscrete(uint8_t { 128 }, levels4), "128 should map to last level");

    std::cout << "    ✓ mapDiscrete works correctly" << std::endl;
}

void testRoundToNearest()
{
    std::cout << "  Testing roundToNearest..." << std::endl;

    constexpr std::array<int, 5> allowed { 0, 25, 50, 75, 100 };
    ASSERT_EQ(0, roundToNearest(0, allowed), "0 should stay 0");
    ASSERT_EQ(0, roundToNearest(12, allowed), "12 should round to 0");
    ASSERT_EQ(25, roundToNearest(13, allowed), "13 should round to 25");
    ASSERT_EQ(25, roundToNearest(25, allowed), "25 should stay 25");
    ASSERT_EQ(50, roundToNearest(40, allowed), "40 should round to 50");
    ASSERT_EQ(100, roundToNearest(100, allowed), "100 should stay 100");
    ASSERT_EQ(100, roundToNearest(150, allowed), "150 should round to 100");

    std::cout << "    ✓ roundToNearest works correctly" << std::endl;
}

void testMakeCapabilityDetail()
{
    std::cout << "  Testing makeCapabilityDetail..." << std::endl;

    auto detail = makeCapabilityDetail(0xFF00, 0x0001, 3);
    ASSERT_EQ(0xFF00, detail.usagepage, "usagepage should be 0xFF00");
    ASSERT_EQ(0x0001, detail.usageid, "usageid should be 0x0001");
    ASSERT_EQ(3, detail.interface_id, "interface_id should be 3");

    auto default_detail = makeCapabilityDetail(0, 0, 0);
    ASSERT_EQ(0, default_detail.usagepage, "default usagepage should be 0");
    ASSERT_EQ(0, default_detail.usageid, "default usageid should be 0");
    ASSERT_EQ(0, default_detail.interface_id, "default interface_id should be 0");

    std::cout << "    ✓ makeCapabilityDetail works correctly" << std::endl;
}

// ============================================================================
// utility.hpp Tests
// ============================================================================

void testRoundToMultiples()
{
    std::cout << "  Testing round_to_multiples..." << std::endl;

    ASSERT_EQ(15, round_to_multiples(17, 5), "17 rounded to 5s should be 15");
    ASSERT_EQ(20, round_to_multiples(18, 5), "18 rounded to 5s should be 20");
    ASSERT_EQ(0, round_to_multiples(0, 5), "0 rounded to 5s should be 0");
    ASSERT_EQ(10, round_to_multiples(10, 5), "10 rounded to 5s should be 10");
    ASSERT_EQ(100, round_to_multiples(99, 10), "99 rounded to 10s should be 100");

    std::cout << "    ✓ round_to_multiples works correctly" << std::endl;
}

void testSplineBatteryLevel()
{
    std::cout << "  Testing spline_battery_level..." << std::endl;

    // Typical Logitech calibration (descending voltage order)
    const std::array<int, 5> percentages { 100, 75, 50, 25, 0 };
    const std::array<int, 5> voltages { 4200, 4000, 3800, 3600, 3400 };

    ASSERT_EQ(100, spline_battery_level(percentages, voltages, 4200), "4200mV should be 100%");
    ASSERT_EQ(100, spline_battery_level(percentages, voltages, 4500), "Above max should be 100%");
    ASSERT_EQ(0, spline_battery_level(percentages, voltages, 3400), "3400mV should be 0%");
    ASSERT_EQ(0, spline_battery_level(percentages, voltages, 3000), "Below min should be 0%");

    // Interpolation
    auto pct = spline_battery_level(percentages, voltages, 3900);
    ASSERT_TRUE(pct >= 55 && pct <= 70, "3900mV should interpolate to ~62%");

    std::cout << "    ✓ spline_battery_level works correctly" << std::endl;
}

void testPolyBatteryLevel()
{
    std::cout << "  Testing poly_battery_level..." << std::endl;

    // Simple linear polynomial: y = x (terms: 0, 1)
    // At voltage 50, should return 50%
    std::array<double, 2> linear_terms { 0.0, 1.0 };
    auto linear_result = poly_battery_level(linear_terms, 50);
    ASSERT_NEAR(50.0f, linear_result, 1.0f, "Linear poly at 50 should return ~50");

    // Quadratic that gives ~100 at voltage 10: y = x^2
    std::array<double, 3> quad_terms { 0.0, 0.0, 1.0 };
    auto quad_result = poly_battery_level(quad_terms, 10);
    ASSERT_NEAR(100.0f, quad_result, 1.0f, "Quadratic poly at 10 should return 100");

    // Should clamp to 100 max
    auto clamped_high = poly_battery_level(linear_terms, 150);
    ASSERT_NEAR(100.0f, clamped_high, 0.1f, "Should clamp to 100 max");

    // Should clamp to 0 min (negative result)
    std::array<double, 2> negative_terms { -50.0, 0.0 };
    auto clamped_low = poly_battery_level(negative_terms, 10);
    ASSERT_NEAR(0.0f, clamped_low, 0.1f, "Should clamp to 0 min");

    std::cout << "    ✓ poly_battery_level works correctly" << std::endl;
}

void testHexdump()
{
    std::cout << "  Testing hexdump..." << std::endl;

    std::array<uint8_t, 3> data { 0x00, 0xAB, 0xFF };
    auto result = hexdump(data);

    ASSERT_TRUE(result.find("0x00") != std::string::npos, "Should contain 0x00");
    ASSERT_TRUE(result.find("0xAB") != std::string::npos, "Should contain 0xAB");
    ASSERT_TRUE(result.find("0xFF") != std::string::npos, "Should contain 0xFF");

    std::cout << "    ✓ hexdump works correctly" << std::endl;
}

void testParseByteData()
{
    std::cout << "  Testing parse_byte_data..." << std::endl;

    // Hex format
    auto hex_result = parse_byte_data("0xff, 0x00, 0xab");
    ASSERT_EQ(3u, hex_result.size(), "Should parse 3 hex bytes");
    ASSERT_EQ(0xFF, hex_result[0], "First byte should be 0xFF");
    ASSERT_EQ(0x00, hex_result[1], "Second byte should be 0x00");
    ASSERT_EQ(0xAB, hex_result[2], "Third byte should be 0xAB");

    // Decimal format
    auto dec_result = parse_byte_data("255, 0, 128");
    ASSERT_EQ(3u, dec_result.size(), "Should parse 3 decimal bytes");
    ASSERT_EQ(255, dec_result[0], "First byte should be 255");
    ASSERT_EQ(0, dec_result[1], "Second byte should be 0");
    ASSERT_EQ(128, dec_result[2], "Third byte should be 128");

    // Mixed format
    auto mixed_result = parse_byte_data("0xff 123 0xb");
    ASSERT_EQ(3u, mixed_result.size(), "Should parse 3 mixed bytes");

    // With braces (C array format)
    auto brace_result = parse_byte_data("{0x01, 0x02, 0x03}");
    ASSERT_EQ(3u, brace_result.size(), "Should parse bytes in braces");

    // Empty
    auto empty_result = parse_byte_data("");
    ASSERT_EQ(0u, empty_result.size(), "Empty string should give empty result");

    std::cout << "    ✓ parse_byte_data works correctly" << std::endl;
}

void testParseFloatData()
{
    std::cout << "  Testing parse_float_data..." << std::endl;

    auto result = parse_float_data("1.0, 2.5, -3.0, 0.0");
    ASSERT_EQ(4u, result.size(), "Should parse 4 floats");
    ASSERT_NEAR(1.0f, result[0], 0.001f, "First float should be 1.0");
    ASSERT_NEAR(2.5f, result[1], 0.001f, "Second float should be 2.5");
    ASSERT_NEAR(-3.0f, result[2], 0.001f, "Third float should be -3.0");
    ASSERT_NEAR(0.0f, result[3], 0.001f, "Fourth float should be 0.0");

    // Empty
    auto empty_result = parse_float_data("");
    ASSERT_EQ(0u, empty_result.size(), "Empty string should give empty result");

    std::cout << "    ✓ parse_float_data works correctly" << std::endl;
}

void testParseParametricEqualizerSettings()
{
    std::cout << "  Testing parse_parametric_equalizer_settings..." << std::endl;

    // Empty/reset
    auto empty = parse_parametric_equalizer_settings("");
    ASSERT_TRUE(empty.bands.empty(), "Empty string should give empty settings");

    auto reset = parse_parametric_equalizer_settings("reset");
    ASSERT_TRUE(reset.bands.empty(), "Reset should give empty settings");

    // Single band: "frequency,gain,q_factor,filter_type"
    auto single = parse_parametric_equalizer_settings("1000,3.5,1.0,peaking");
    ASSERT_EQ(1, single.size(), "Should have 1 band");
    ASSERT_NEAR(1000.0f, single.bands[0].frequency, 0.1f, "Frequency should be 1000");
    ASSERT_NEAR(3.5f, single.bands[0].gain, 0.1f, "Gain should be 3.5");
    ASSERT_NEAR(1.0f, single.bands[0].q_factor, 0.1f, "Q-factor should be 1.0");

    // Multiple bands separated by semicolon
    auto multi = parse_parametric_equalizer_settings("100,2.0,0.7,lowshelf;5000,-3.0,1.4,highshelf");
    ASSERT_EQ(2, multi.size(), "Should have 2 bands");
    ASSERT_NEAR(100.0f, multi.bands[0].frequency, 0.1f, "First band frequency");
    ASSERT_NEAR(5000.0f, multi.bands[1].frequency, 0.1f, "Second band frequency");

    std::cout << "    ✓ parse_parametric_equalizer_settings works correctly" << std::endl;
}

void testParseTwoIds()
{
    std::cout << "  Testing parse_two_ids..." << std::endl;

    // Hex with colon
    auto hex_colon = parse_two_ids("0x1b1c:0x1b27");
    ASSERT_TRUE(hex_colon.has_value(), "Should parse hex:hex");
    ASSERT_EQ(0x1b1c, hex_colon->first, "First ID should be 0x1b1c");
    ASSERT_EQ(0x1b27, hex_colon->second, "Second ID should be 0x1b27");

    // Decimal with colon
    auto dec_colon = parse_two_ids("1234:5678");
    ASSERT_TRUE(dec_colon.has_value(), "Should parse dec:dec");
    ASSERT_EQ(1234, dec_colon->first, "First ID should be 1234");
    ASSERT_EQ(5678, dec_colon->second, "Second ID should be 5678");

    // With spaces
    auto spaces = parse_two_ids("0x1234 0x5678");
    ASSERT_TRUE(spaces.has_value(), "Should parse with spaces");

    // Invalid
    auto invalid = parse_two_ids("only_one");
    ASSERT_FALSE(invalid.has_value(), "Single value should fail");

    auto empty = parse_two_ids("");
    ASSERT_FALSE(empty.has_value(), "Empty string should fail");

    std::cout << "    ✓ parse_two_ids works correctly" << std::endl;
}

// ============================================================================
// result_types.hpp Tests
// ============================================================================

void testResultSuccess()
{
    std::cout << "  Testing Result<T> success..." << std::endl;

    Result<int> result = 42;

    ASSERT_TRUE(result.hasValue(), "Should have value");
    ASSERT_FALSE(result.hasError(), "Should not have error");
    ASSERT_TRUE(static_cast<bool>(result), "Bool conversion should be true");
    ASSERT_EQ(42, result.value(), "Value should be 42");
    ASSERT_EQ(42, *result, "Dereference should be 42");

    std::cout << "    ✓ Result<T> success works correctly" << std::endl;
}

void testResultError()
{
    std::cout << "  Testing Result<T> error..." << std::endl;

    Result<int> result = DeviceError::timeout("Test timeout");

    ASSERT_FALSE(result.hasValue(), "Should not have value");
    ASSERT_TRUE(result.hasError(), "Should have error");
    ASSERT_FALSE(static_cast<bool>(result), "Bool conversion should be false");
    ASSERT_EQ(DeviceError::Code::Timeout, result.error().code, "Error code should be Timeout");

    std::cout << "    ✓ Result<T> error works correctly" << std::endl;
}

void testResultValueOr()
{
    std::cout << "  Testing Result<T>::valueOr..." << std::endl;

    Result<int> success = 42;
    ASSERT_EQ(42, success.valueOr(0), "valueOr should return actual value on success");

    Result<int> failure = DeviceError::timeout();
    ASSERT_EQ(0, failure.valueOr(0), "valueOr should return default on failure");

    std::cout << "    ✓ Result<T>::valueOr works correctly" << std::endl;
}

void testResultVoid()
{
    std::cout << "  Testing Result<void>..." << std::endl;

    Result<void> success {};
    ASSERT_TRUE(success.hasValue(), "Empty Result<void> should have value");
    ASSERT_FALSE(success.hasError(), "Empty Result<void> should not have error");

    Result<void> failure = DeviceError::hidError("Test error");
    ASSERT_FALSE(failure.hasValue(), "Error Result<void> should not have value");
    ASSERT_TRUE(failure.hasError(), "Error Result<void> should have error");

    std::cout << "    ✓ Result<void> works correctly" << std::endl;
}

void testDeviceErrorFactories()
{
    std::cout << "  Testing DeviceError factories..." << std::endl;

    auto timeout = DeviceError::timeout("Timeout details");
    ASSERT_EQ(DeviceError::Code::Timeout, timeout.code, "Should be Timeout");

    auto offline = DeviceError::deviceOffline("Offline details");
    ASSERT_EQ(DeviceError::Code::DeviceOffline, offline.code, "Should be DeviceOffline");

    auto protocol = DeviceError::protocolError("Protocol details");
    ASSERT_EQ(DeviceError::Code::ProtocolError, protocol.code, "Should be ProtocolError");

    auto invalid = DeviceError::invalidParameter("Invalid details");
    ASSERT_EQ(DeviceError::Code::InvalidParameter, invalid.code, "Should be InvalidParameter");

    auto notSupported = DeviceError::notSupported("Not supported details");
    ASSERT_EQ(DeviceError::Code::NotSupported, notSupported.code, "Should be NotSupported");

    auto hidError = DeviceError::hidError("HID details");
    ASSERT_EQ(DeviceError::Code::HIDError, hidError.code, "Should be HIDError");

    std::cout << "    ✓ DeviceError factories work correctly" << std::endl;
}

// ============================================================================
// string_utils.hpp Tests
// ============================================================================

void testWstringToString()
{
    std::cout << "  Testing wstring_to_string..." << std::endl;

    // Normal string
    const wchar_t* test = L"Hello";
    ASSERT_EQ("Hello", wstring_to_string(test), "Should convert wchar_t* to string");

    // Nullptr
    ASSERT_EQ("Unknown error", wstring_to_string(nullptr), "nullptr should give 'Unknown error'");

    // Empty string
    const wchar_t* empty = L"";
    ASSERT_EQ("", wstring_to_string(empty), "Empty should give empty");

    std::cout << "    ✓ wstring_to_string works correctly" << std::endl;
}

// ============================================================================
// feature_utils.hpp Tests
// ============================================================================

void testMakeSuccess()
{
    std::cout << "  Testing make_success..." << std::endl;

    auto result = headsetcontrol::make_success();
    ASSERT_EQ(FEATURE_SUCCESS, result.status, "Default status should be SUCCESS");
    ASSERT_EQ(0, result.value, "Default value should be 0");
    ASSERT_TRUE(result.message.empty(), "Default message should be empty");

    auto with_value = headsetcontrol::make_success(42, "Test message");
    ASSERT_EQ(42, with_value.value, "Value should be 42");
    ASSERT_EQ("Test message", with_value.message, "Message should match");

    std::cout << "    ✓ make_success works correctly" << std::endl;
}

void testMakeInfo()
{
    std::cout << "  Testing make_info..." << std::endl;

    auto result = headsetcontrol::make_info(50, "Charging");
    ASSERT_EQ(FEATURE_INFO, result.status, "Status should be INFO");
    ASSERT_EQ(50, result.value, "Value should be 50");
    ASSERT_EQ("Charging", result.message, "Message should match");

    std::cout << "    ✓ make_info works correctly" << std::endl;
}

void testMakeError()
{
    std::cout << "  Testing make_error..." << std::endl;

    auto result = headsetcontrol::make_error(-1, "Connection failed");
    ASSERT_EQ(FEATURE_ERROR, result.status, "Status should be ERROR");
    ASSERT_EQ(-1, result.value, "Value should be -1");
    ASSERT_EQ("Connection failed", result.message, "Message should match");

    std::cout << "    ✓ make_error works correctly" << std::endl;
}

// ============================================================================
// output_data.hpp Tests
// ============================================================================

void testStatusToString()
{
    std::cout << "  Testing statusToString..." << std::endl;

    using headsetcontrol::output::statusToString;
    ASSERT_EQ("success", statusToString(STATUS_SUCCESS), "SUCCESS -> success");
    ASSERT_EQ("failure", statusToString(STATUS_FAILURE), "FAILURE -> failure");
    ASSERT_EQ("partial", statusToString(STATUS_PARTIAL), "PARTIAL -> partial");
    ASSERT_EQ("unknown", statusToString(static_cast<Status>(99)), "Unknown -> unknown");

    std::cout << "    ✓ statusToString works correctly" << std::endl;
}

void testBatteryStatusToString()
{
    std::cout << "  Testing batteryStatusToString..." << std::endl;

    using headsetcontrol::output::batteryStatusToString;
    ASSERT_EQ("BATTERY_AVAILABLE", batteryStatusToString(BATTERY_AVAILABLE), "AVAILABLE");
    ASSERT_EQ("BATTERY_CHARGING", batteryStatusToString(BATTERY_CHARGING), "CHARGING");
    ASSERT_EQ("BATTERY_UNAVAILABLE", batteryStatusToString(BATTERY_UNAVAILABLE), "UNAVAILABLE");
    ASSERT_EQ("BATTERY_TIMEOUT", batteryStatusToString(BATTERY_TIMEOUT), "TIMEOUT");
    ASSERT_EQ("BATTERY_ERROR", batteryStatusToString(BATTERY_HIDERROR), "HIDERROR -> ERROR");
    ASSERT_EQ("UNKNOWN", batteryStatusToString(static_cast<battery_status>(99)), "Unknown");

    std::cout << "    ✓ batteryStatusToString works correctly" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllUtilityTests()
{
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║           Utility Function Tests                     ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    ✗ FAILED: " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "    ✗ ERROR: " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== device_utils.hpp Tests ===" << std::endl;
    runTest("mapSidetoneToDiscrete<4>", testMapSidetoneToDiscrete4Levels);
    runTest("mapSidetoneToDiscrete<11>", testMapSidetoneToDiscrete11Levels);
    runTest("mapSidetoneWithToggle", testMapSidetoneWithToggle);
    runTest("map", testDeviceUtilsMap);
    runTest("mapDiscrete", testMapDiscrete);
    runTest("bytes_to_uint16", testBytesToUint16);
    runTest("uint16_to_bytes", testUint16ToBytes);
    runTest("voltageToPercent", testVoltageToPercent);
    runTest("validateDiscrete", testValidateDiscrete);
    runTest("roundToNearest", testRoundToNearest);
    runTest("makeCapabilityDetail", testMakeCapabilityDetail);
    runTest("clampInactiveTime", testClampInactiveTime);

    std::cout << "\n=== utility.hpp Tests ===" << std::endl;
    runTest("round_to_multiples", testRoundToMultiples);
    runTest("spline_battery_level", testSplineBatteryLevel);
    runTest("poly_battery_level", testPolyBatteryLevel);
    runTest("hexdump", testHexdump);
    runTest("parse_byte_data", testParseByteData);
    runTest("parse_float_data", testParseFloatData);
    runTest("parse_parametric_eq", testParseParametricEqualizerSettings);
    runTest("parse_two_ids", testParseTwoIds);

    std::cout << "\n=== result_types.hpp Tests ===" << std::endl;
    runTest("Result<T> success", testResultSuccess);
    runTest("Result<T> error", testResultError);
    runTest("Result<T>::valueOr", testResultValueOr);
    runTest("Result<void>", testResultVoid);
    runTest("DeviceError factories", testDeviceErrorFactories);

    std::cout << "\n=== string_utils.hpp Tests ===" << std::endl;
    runTest("wstring_to_string", testWstringToString);

    std::cout << "\n=== feature_utils.hpp Tests ===" << std::endl;
    runTest("make_success", testMakeSuccess);
    runTest("make_info", testMakeInfo);
    runTest("make_error", testMakeError);

    std::cout << "\n=== output_data.hpp Tests ===" << std::endl;
    runTest("statusToString", testStatusToString);
    runTest("batteryStatusToString", testBatteryStatusToString);

    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Utility Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some utility tests failed");
    }
}

} // namespace headsetcontrol::testing

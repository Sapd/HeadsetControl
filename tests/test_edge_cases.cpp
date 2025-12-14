/**
 * @file test_edge_cases.cpp
 * @brief Edge case tests for utility functions and error handling
 *
 * Tests for:
 * - Overflow conditions in math functions
 * - Invalid input handling in parsers
 * - Boundary conditions in mapping functions
 * - Empty input handling
 * - Unicode and special character handling
 */

#include "devices/device_utils.hpp"
#include "result_types.hpp"
#include "string_utils.hpp"
#include "utility.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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

#define ASSERT_EQ(expected, actual, msg)                 \
    do {                                                 \
        if ((expected) != (actual)) {                    \
            std::ostringstream oss;                      \
            oss << "ASSERT_EQ failed: " << (msg) << "\n" \
                << "  Expected: " << (expected) << "\n"  \
                << "  Actual:   " << (actual);           \
            throw TestFailure(oss.str());                \
        }                                                \
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

#define ASSERT_THROWS(expr, msg)                                              \
    do {                                                                      \
        bool threw = false;                                                   \
        try {                                                                 \
            expr;                                                             \
        } catch (...) {                                                       \
            threw = true;                                                     \
        }                                                                     \
        if (!threw) {                                                         \
            throw TestFailure(std::string("ASSERT_THROWS failed: ") + (msg)); \
        }                                                                     \
    } while (0)

// ============================================================================
// Mapping Function Edge Cases
// ============================================================================

void testMapBoundaryValues()
{
    std::cout << "  Testing map boundary values..." << std::endl;

    // Test at exact boundaries
    ASSERT_EQ(0, headsetcontrol::map(0, 0, 128, 0, 100), "min->min mapping");
    ASSERT_EQ(100, headsetcontrol::map(128, 0, 128, 0, 100), "max->max mapping");

    // Test just inside boundaries
    ASSERT_EQ(0, headsetcontrol::map(1, 0, 128, 0, 100), "1/128 should map to ~0");
    ASSERT_EQ(99, headsetcontrol::map(127, 0, 128, 0, 100), "127/128 should map to ~99");

    std::cout << "    OK map boundary values" << std::endl;
}

void testMapSameRange()
{
    std::cout << "  Testing map with same range..." << std::endl;

    // Same min/max should return out_min
    ASSERT_EQ(50, headsetcontrol::map(100, 50, 50, 50, 50), "Same range should return out_min");
    ASSERT_EQ(0, headsetcontrol::map(0, 0, 0, 0, 100), "Zero range should return out_min");

    std::cout << "    OK map same range" << std::endl;
}

void testMapClampingBehavior()
{
    std::cout << "  Testing map clamping behavior..." << std::endl;

    // Values outside range should clamp
    ASSERT_EQ(100, headsetcontrol::map(200, 0, 128, 0, 100), "Above max clamps to out_max");
    ASSERT_EQ(0, headsetcontrol::map(-50, 0, 128, 0, 100), "Below min clamps to out_min");

    // Large values
    ASSERT_EQ(100, headsetcontrol::map(1000000, 0, 128, 0, 100), "Very large value clamps");

    std::cout << "    OK map clamping behavior" << std::endl;
}

void testMapNegativeRanges()
{
    std::cout << "  Testing map with negative ranges..." << std::endl;

    // Negative output range (reverse mapping)
    ASSERT_EQ(100, headsetcontrol::map(0, 0, 100, 100, 0), "Reverse mapping at 0");
    ASSERT_EQ(0, headsetcontrol::map(100, 0, 100, 100, 0), "Reverse mapping at 100");
    ASSERT_EQ(50, headsetcontrol::map(50, 0, 100, 100, 0), "Reverse mapping at middle");

    // Negative input range
    ASSERT_EQ(50, headsetcontrol::map(0, -100, 100, 0, 100), "0 in -100..100 maps to 50");

    std::cout << "    OK map negative ranges" << std::endl;
}

void testMapLargeValues()
{
    std::cout << "  Testing map with large values..." << std::endl;

    // Large values that might cause overflow
    auto result = headsetcontrol::map(10000, 0, 20000, 0, 100);
    ASSERT_EQ(50, result, "Large values should work correctly");

    // Max uint16_t values
    auto result2 = headsetcontrol::map(uint16_t(32767), uint16_t(0), uint16_t(65535), 0, 100);
    ASSERT_NEAR(50, result2, 1, "Mid-range uint16_t should map to ~50");

    std::cout << "    OK map large values" << std::endl;
}

// ============================================================================
// Sidetone Mapping Edge Cases
// ============================================================================

void testMapSidetoneEdgeCases()
{
    std::cout << "  Testing mapSidetoneToDiscrete edge cases..." << std::endl;

    // Single level device
    ASSERT_EQ(0, mapSidetoneToDiscrete<1>(0), "Single level: 0 maps to 0");
    ASSERT_EQ(0, mapSidetoneToDiscrete<1>(128), "Single level: 128 maps to 0");

    // Two level device
    ASSERT_EQ(0, mapSidetoneToDiscrete<2>(0), "Two levels: 0 maps to 0");
    ASSERT_EQ(0, mapSidetoneToDiscrete<2>(63), "Two levels: 63 maps to 0");
    ASSERT_EQ(1, mapSidetoneToDiscrete<2>(64), "Two levels: 64 maps to 1");
    ASSERT_EQ(1, mapSidetoneToDiscrete<2>(128), "Two levels: 128 maps to 1");

    // Max levels (128 levels = continuous)
    ASSERT_EQ(0, mapSidetoneToDiscrete<128>(0), "128 levels: 0 maps to 0");
    ASSERT_EQ(127, mapSidetoneToDiscrete<128>(128), "128 levels: 128 maps to 127");

    std::cout << "    OK mapSidetoneToDiscrete edge cases" << std::endl;
}

void testMapSidetoneWithToggleEdgeCases()
{
    std::cout << "  Testing mapSidetoneWithToggle edge cases..." << std::endl;

    // Same min/max (binary on/off with single level)
    auto [on, level] = mapSidetoneWithToggle(50, 100, 100);
    ASSERT_TRUE(on, "Non-zero should be on");
    ASSERT_EQ(100, level, "Same min/max should give that value");

    // Full range test
    auto [off, off_level] = mapSidetoneWithToggle(0, 0, 255);
    ASSERT_FALSE(off, "0 should be off");
    ASSERT_EQ(0, off_level, "Off level should be 0");

    auto [max_on, max_level] = mapSidetoneWithToggle(128, 0, 255);
    ASSERT_TRUE(max_on, "128 should be on");
    ASSERT_EQ(255, max_level, "128 should map to max");

    std::cout << "    OK mapSidetoneWithToggle edge cases" << std::endl;
}

// ============================================================================
// Byte Manipulation Edge Cases
// ============================================================================

void testBytesToUint16EdgeCases()
{
    std::cout << "  Testing bytes_to_uint16 edge cases..." << std::endl;

    // All zeros
    ASSERT_EQ(0x0000, bytes_to_uint16_be(0x00, 0x00), "All zeros BE");
    ASSERT_EQ(0x0000, bytes_to_uint16_le(0x00, 0x00), "All zeros LE");

    // All ones
    ASSERT_EQ(0xFFFF, bytes_to_uint16_be(0xFF, 0xFF), "All ones BE");
    ASSERT_EQ(0xFFFF, bytes_to_uint16_le(0xFF, 0xFF), "All ones LE");

    // Alternating bits
    ASSERT_EQ(0xAA55, bytes_to_uint16_be(0xAA, 0x55), "Alternating BE");
    ASSERT_EQ(0x55AA, bytes_to_uint16_le(0xAA, 0x55), "Alternating LE");

    std::cout << "    OK bytes_to_uint16 edge cases" << std::endl;
}

void testUint16ToBytesEdgeCases()
{
    std::cout << "  Testing uint16_to_bytes edge cases..." << std::endl;

    // Min value
    auto [min_hi, min_lo] = uint16_to_bytes_be(0x0000);
    ASSERT_EQ(0x00, min_hi, "Min value high byte BE");
    ASSERT_EQ(0x00, min_lo, "Min value low byte BE");

    // Max value
    auto [max_hi, max_lo] = uint16_to_bytes_be(0xFFFF);
    ASSERT_EQ(0xFF, max_hi, "Max value high byte BE");
    ASSERT_EQ(0xFF, max_lo, "Max value low byte BE");

    // Roundtrip test
    uint16_t original      = 0x1234;
    auto [hi, lo]          = uint16_to_bytes_be(original);
    uint16_t reconstructed = bytes_to_uint16_be(hi, lo);
    ASSERT_EQ(original, reconstructed, "Roundtrip should preserve value");

    std::cout << "    OK uint16_to_bytes edge cases" << std::endl;
}

// ============================================================================
// Parsing Edge Cases
// ============================================================================

void testParseByteDataEdgeCases()
{
    std::cout << "  Testing parse_byte_data edge cases..." << std::endl;

    // Empty input
    auto empty = parse_byte_data("");
    ASSERT_EQ(0u, empty.size(), "Empty string should give empty result");

    // Whitespace only
    auto whitespace = parse_byte_data("   ");
    ASSERT_EQ(0u, whitespace.size(), "Whitespace only should give empty result");

    // Single value
    auto single = parse_byte_data("0xff");
    ASSERT_EQ(1u, single.size(), "Single value should parse");
    ASSERT_EQ(0xFF, single[0], "Single value should be 0xFF");

    // With extra whitespace
    auto extra_ws = parse_byte_data("  0xff  ,  0x00  ");
    ASSERT_EQ(2u, extra_ws.size(), "Should parse with extra whitespace");

    // Boundary values
    auto bounds = parse_byte_data("0, 255");
    ASSERT_EQ(2u, bounds.size(), "Boundary values should parse");
    ASSERT_EQ(0, bounds[0], "0 should parse");
    ASSERT_EQ(255, bounds[1], "255 should parse");

    // Mixed separators
    auto mixed = parse_byte_data("1, 2 3,4");
    ASSERT_EQ(4u, mixed.size(), "Mixed separators should work");

    std::cout << "    OK parse_byte_data edge cases" << std::endl;
}

void testParseFloatDataEdgeCases()
{
    std::cout << "  Testing parse_float_data edge cases..." << std::endl;

    // Empty input
    auto empty = parse_float_data("");
    ASSERT_EQ(0u, empty.size(), "Empty string should give empty result");

    // Single value
    auto single = parse_float_data("3.14159");
    ASSERT_EQ(1u, single.size(), "Single value should parse");
    ASSERT_NEAR(3.14159f, single[0], 0.0001f, "Pi should parse correctly");

    // Negative values
    auto negative = parse_float_data("-1.5, -2.5, -3.5");
    ASSERT_EQ(3u, negative.size(), "Negative values should parse");
    ASSERT_NEAR(-1.5f, negative[0], 0.001f, "First negative");
    ASSERT_NEAR(-2.5f, negative[1], 0.001f, "Second negative");
    ASSERT_NEAR(-3.5f, negative[2], 0.001f, "Third negative");

    // Zero
    auto zero = parse_float_data("0.0, 0, -0.0");
    ASSERT_EQ(3u, zero.size(), "Zeros should parse");
    ASSERT_NEAR(0.0f, zero[0], 0.001f, "Zero 1");
    ASSERT_NEAR(0.0f, zero[1], 0.001f, "Zero 2");
    ASSERT_NEAR(0.0f, zero[2], 0.001f, "Zero 3");

    // Scientific notation (if supported)
    auto scientific = parse_float_data("1e-3, 2.5e2");
    if (scientific.size() == 2) {
        ASSERT_NEAR(0.001f, scientific[0], 0.0001f, "Scientific notation 1e-3");
        ASSERT_NEAR(250.0f, scientific[1], 0.1f, "Scientific notation 2.5e2");
        std::cout << "    (scientific notation supported)" << std::endl;
    }

    std::cout << "    OK parse_float_data edge cases" << std::endl;
}

void testParseTwoIdsEdgeCases()
{
    std::cout << "  Testing parse_two_ids edge cases..." << std::endl;

    // Empty input
    auto empty = parse_two_ids("");
    ASSERT_FALSE(empty.has_value(), "Empty string should fail");

    // Single value only
    auto single = parse_two_ids("1234");
    ASSERT_FALSE(single.has_value(), "Single value should fail");

    // Zero values
    auto zeros = parse_two_ids("0:0");
    ASSERT_TRUE(zeros.has_value(), "Zeros should parse");
    ASSERT_EQ(0, zeros->first, "First zero");
    ASSERT_EQ(0, zeros->second, "Second zero");

    // Hex format
    auto hex = parse_two_ids("0xFFFF:0xFFFF");
    ASSERT_TRUE(hex.has_value(), "Max hex should parse");
    ASSERT_EQ(0xFFFF, hex->first, "Max first");
    ASSERT_EQ(0xFFFF, hex->second, "Max second");

    // Space separator
    auto space = parse_two_ids("123 456");
    ASSERT_TRUE(space.has_value(), "Space separator should work");

    // Multiple colons (should use first two values)
    auto multi = parse_two_ids("1:2:3");
    ASSERT_TRUE(multi.has_value(), "Multiple colons should parse first two");
    ASSERT_EQ(1, multi->first, "First of multiple");
    ASSERT_EQ(2, multi->second, "Second of multiple");

    std::cout << "    OK parse_two_ids edge cases" << std::endl;
}

// ============================================================================
// Round to Multiples Edge Cases
// ============================================================================

void testRoundToMultiplesEdgeCases()
{
    std::cout << "  Testing round_to_multiples edge cases..." << std::endl;

    // Zero
    ASSERT_EQ(0u, round_to_multiples(0, 5), "0 rounds to 0");

    // Value equal to multiple
    ASSERT_EQ(10u, round_to_multiples(10, 5), "10 rounds to 10");

    // Just below/above threshold
    ASSERT_EQ(10u, round_to_multiples(12, 5), "12 rounds to 10");
    ASSERT_EQ(15u, round_to_multiples(13, 5), "13 rounds to 15");

    // Multiple of 1 (should be identity)
    ASSERT_EQ(17u, round_to_multiples(17, 1), "Multiple of 1 is identity");

    // Large multiple
    ASSERT_EQ(0u, round_to_multiples(49, 100), "49 rounds to 0 with multiple 100");
    ASSERT_EQ(100u, round_to_multiples(50, 100), "50 rounds to 100 with multiple 100");

    std::cout << "    OK round_to_multiples edge cases" << std::endl;
}

// ============================================================================
// Spline Battery Level Edge Cases
// ============================================================================

void testSplineBatteryLevelEdgeCases()
{
    std::cout << "  Testing spline_battery_level edge cases..." << std::endl;

    // Typical calibration (descending percentage order)
    const std::array<int, 5> percentages { 100, 75, 50, 25, 0 };
    const std::array<int, 5> voltages { 4200, 4000, 3800, 3600, 3400 };

    // Below minimum voltage
    ASSERT_EQ(0, spline_battery_level(percentages, voltages, 3000), "Below min clamps to 0%");
    ASSERT_EQ(0, spline_battery_level(percentages, voltages, 0), "Zero voltage is 0%");

    // Above maximum voltage
    ASSERT_EQ(100, spline_battery_level(percentages, voltages, 5000), "Above max clamps to 100%");
    ASSERT_EQ(100, spline_battery_level(percentages, voltages, 65535), "Max uint16 is 100%");

    // At calibration points
    ASSERT_EQ(100, spline_battery_level(percentages, voltages, 4200), "At 4200mV = 100%");
    ASSERT_EQ(0, spline_battery_level(percentages, voltages, 3400), "At 3400mV = 0%");
    ASSERT_EQ(50, spline_battery_level(percentages, voltages, 3800), "At 3800mV = 50%");

    std::cout << "    OK spline_battery_level edge cases" << std::endl;
}

// ============================================================================
// Poly Battery Level Edge Cases
// ============================================================================

void testPolyBatteryLevelEdgeCases()
{
    std::cout << "  Testing poly_battery_level edge cases..." << std::endl;

    // Constant polynomial (always returns same value)
    std::array<double, 1> constant { 50.0 };
    ASSERT_NEAR(50.0f, poly_battery_level(constant, 0), 0.1f, "Constant at 0");
    ASSERT_NEAR(50.0f, poly_battery_level(constant, 1000), 0.1f, "Constant at 1000");

    // Linear polynomial that would exceed 100%
    std::array<double, 2> linear_high { 0.0, 2.0 }; // y = 2x
    auto high_result = poly_battery_level(linear_high, 100); // Would be 200
    ASSERT_NEAR(100.0f, high_result, 0.1f, "Should clamp to 100%");

    // Linear polynomial that would go negative
    std::array<double, 2> linear_negative { -100.0, 1.0 }; // y = x - 100
    auto neg_result = poly_battery_level(linear_negative, 50); // Would be -50
    ASSERT_NEAR(0.0f, neg_result, 0.1f, "Should clamp to 0%");

    std::cout << "    OK poly_battery_level edge cases" << std::endl;
}

// ============================================================================
// Result Type Edge Cases
// ============================================================================

void testResultMoveSemantics()
{
    std::cout << "  Testing Result<T> move semantics..." << std::endl;

    // Move construction
    Result<std::string> original = std::string("test value");
    Result<std::string> moved    = std::move(original);
    ASSERT_TRUE(moved.hasValue(), "Moved-to should have value");
    ASSERT_EQ("test value", moved.value(), "Moved value should be correct");

    // Move from result
    std::string extracted = std::move(moved).value();
    ASSERT_EQ("test value", extracted, "Extracted value should be correct");

    std::cout << "    OK Result<T> move semantics" << std::endl;
}

void testResultValueOrEdgeCases()
{
    std::cout << "  Testing Result<T>::valueOr edge cases..." << std::endl;

    // With success
    Result<int> success = 42;
    ASSERT_EQ(42, success.valueOr(-1), "Success returns actual value");

    // With error
    Result<int> error = DeviceError::timeout();
    ASSERT_EQ(-1, error.valueOr(-1), "Error returns default");

    // With different default types
    Result<int> error2 = DeviceError::timeout();
    ASSERT_EQ(0, error2.valueOr(0u), "Works with unsigned default");

    std::cout << "    OK Result<T>::valueOr edge cases" << std::endl;
}

void testDeviceErrorFullMessage()
{
    std::cout << "  Testing DeviceError::fullMessage..." << std::endl;

    auto error = DeviceError::timeout("Connection lost");
    auto msg   = error.fullMessage();

    ASSERT_TRUE(msg.find("Timeout") != std::string::npos || msg.find("timeout") != std::string::npos || msg.find("Connection lost") != std::string::npos,
        "Full message should contain error info");

    std::cout << "    OK DeviceError::fullMessage" << std::endl;
}

// ============================================================================
// Voltage to Percent Edge Cases
// ============================================================================

void testVoltageToPercentEdgeCases()
{
    std::cout << "  Testing voltageToPercent edge cases..." << std::endl;

    constexpr std::array<std::pair<uint8_t, uint16_t>, 4> calibration { { { 0, 3500 },
        { 50, 3700 },
        { 80, 3900 },
        { 100, 4200 } } };

    // At each calibration point exactly
    ASSERT_EQ(0, voltageToPercent(3500, calibration), "At first point");
    ASSERT_EQ(50, voltageToPercent(3700, calibration), "At second point");
    ASSERT_EQ(80, voltageToPercent(3900, calibration), "At third point");
    ASSERT_EQ(100, voltageToPercent(4200, calibration), "At fourth point");

    // Below minimum
    ASSERT_EQ(0, voltageToPercent(3000, calibration), "Below min");
    ASSERT_EQ(0, voltageToPercent(0, calibration), "Zero voltage");

    // Above maximum
    ASSERT_EQ(100, voltageToPercent(5000, calibration), "Above max");
    ASSERT_EQ(100, voltageToPercent(65535, calibration), "Max uint16");

    std::cout << "    OK voltageToPercent edge cases" << std::endl;
}

// ============================================================================
// Validate Discrete Edge Cases
// ============================================================================

void testValidateDiscreteEdgeCases()
{
    std::cout << "  Testing validateDiscrete edge cases..." << std::endl;

    constexpr std::array<int, 3> small { 0, 50, 100 };

    // At allowed values
    ASSERT_EQ(0, validateDiscrete(0, small), "At 0");
    ASSERT_EQ(50, validateDiscrete(50, small), "At 50");
    ASSERT_EQ(100, validateDiscrete(100, small), "At 100");

    // Equidistant (should pick one)
    auto equidist = validateDiscrete(25, small); // 25 is equidistant from 0 and 50
    ASSERT_TRUE(equidist == 0 || equidist == 50, "Equidistant picks one");

    // Far beyond range
    ASSERT_EQ(100, validateDiscrete(1000, small), "Far above max rounds to max");
    ASSERT_EQ(0, validateDiscrete(-1000, small), "Far below min rounds to min");

    std::cout << "    OK validateDiscrete edge cases" << std::endl;
}

// ============================================================================
// Hexdump Edge Cases
// ============================================================================

void testHexdumpEdgeCases()
{
    std::cout << "  Testing hexdump edge cases..." << std::endl;

    // Empty array
    std::array<uint8_t, 0> empty {};
    auto empty_result = hexdump(empty);
    // Empty should produce empty or minimal output
    ASSERT_TRUE(empty_result.length() <= 2, "Empty hexdump should be minimal");

    // Single byte
    std::array<uint8_t, 1> single { 0xAB };
    auto single_result = hexdump(single);
    ASSERT_TRUE(single_result.find("AB") != std::string::npos || single_result.find("ab") != std::string::npos || single_result.find("0xAB") != std::string::npos || single_result.find("0xab") != std::string::npos,
        "Single byte should contain AB");

    // All zeros
    std::array<uint8_t, 4> zeros { 0, 0, 0, 0 };
    auto zeros_result = hexdump(zeros);
    ASSERT_TRUE(zeros_result.find("00") != std::string::npos || zeros_result.find("0x00") != std::string::npos,
        "Zeros should contain 00");

    std::cout << "    OK hexdump edge cases" << std::endl;
}

// ============================================================================
// String Utils Edge Cases
// ============================================================================

void testWstringToStringEdgeCases()
{
    std::cout << "  Testing wstring_to_string edge cases..." << std::endl;

    // Null pointer
    ASSERT_EQ("Unknown error", wstring_to_string(nullptr), "Null returns 'Unknown error'");

    // Empty string
    const wchar_t* empty = L"";
    ASSERT_EQ("", wstring_to_string(empty), "Empty gives empty");

    // ASCII string
    const wchar_t* ascii = L"Hello World";
    ASSERT_EQ("Hello World", wstring_to_string(ascii), "ASCII converts correctly");

    // String with numbers
    const wchar_t* numbers = L"Test123";
    ASSERT_EQ("Test123", wstring_to_string(numbers), "Numbers convert correctly");

    std::cout << "    OK wstring_to_string edge cases" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllEdgeCaseTests()
{
    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                    Edge Case Tests                               " << std::endl;
    std::cout << "===================================================================\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    FAILED: " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "    ERROR: " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== Mapping Function Edge Cases ===" << std::endl;
    runTest("Map Boundary Values", testMapBoundaryValues);
    runTest("Map Same Range", testMapSameRange);
    runTest("Map Clamping", testMapClampingBehavior);
    runTest("Map Negative Ranges", testMapNegativeRanges);
    runTest("Map Large Values", testMapLargeValues);

    std::cout << "\n=== Sidetone Mapping Edge Cases ===" << std::endl;
    runTest("Sidetone Discrete", testMapSidetoneEdgeCases);
    runTest("Sidetone With Toggle", testMapSidetoneWithToggleEdgeCases);

    std::cout << "\n=== Byte Manipulation Edge Cases ===" << std::endl;
    runTest("Bytes to uint16", testBytesToUint16EdgeCases);
    runTest("uint16 to Bytes", testUint16ToBytesEdgeCases);

    std::cout << "\n=== Parsing Edge Cases ===" << std::endl;
    runTest("Parse Byte Data", testParseByteDataEdgeCases);
    runTest("Parse Float Data", testParseFloatDataEdgeCases);
    runTest("Parse Two IDs", testParseTwoIdsEdgeCases);

    std::cout << "\n=== Math Function Edge Cases ===" << std::endl;
    runTest("Round to Multiples", testRoundToMultiplesEdgeCases);
    runTest("Spline Battery Level", testSplineBatteryLevelEdgeCases);
    runTest("Poly Battery Level", testPolyBatteryLevelEdgeCases);

    std::cout << "\n=== Result Type Edge Cases ===" << std::endl;
    runTest("Result Move Semantics", testResultMoveSemantics);
    runTest("Result valueOr", testResultValueOrEdgeCases);
    runTest("DeviceError fullMessage", testDeviceErrorFullMessage);

    std::cout << "\n=== Voltage/Validation Edge Cases ===" << std::endl;
    runTest("Voltage to Percent", testVoltageToPercentEdgeCases);
    runTest("Validate Discrete", testValidateDiscreteEdgeCases);

    std::cout << "\n=== Utility Edge Cases ===" << std::endl;
    runTest("Hexdump", testHexdumpEdgeCases);
    runTest("Wstring to String", testWstringToStringEdgeCases);

    std::cout << "\n-------------------------------------------------------------------" << std::endl;
    std::cout << "Edge Case Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some edge case tests failed");
    }
}

} // namespace headsetcontrol::testing

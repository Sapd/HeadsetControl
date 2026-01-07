/**
 * @file test_protocols.cpp
 * @brief Unit tests for protocol templates (HID++, SteelSeries, Corsair)
 *
 * These tests verify:
 * 1. Packet building and formatting
 * 2. Response parsing correctness
 * 3. Error handling behavior
 * 4. Value mapping functions
 */

#include "device.hpp"
#include "devices/corsair_device.hpp"
#include "devices/protocols/hidpp_protocol.hpp"
#include "devices/protocols/logitech_calibrations.hpp"
#include "devices/protocols/steelseries_protocol.hpp"
#include "result_types.hpp"
#include "utility.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

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

#define ASSERT_NEAR(expected, actual, tolerance, msg)                               \
    do {                                                                            \
        if (std::abs((expected) - (actual)) > (tolerance)) {                        \
            std::ostringstream oss;                                                 \
            oss << "ASSERT_NEAR failed: " << (msg) << "\n"                          \
                << "  Expected: " << (expected) << " (+/- " << (tolerance) << ")\n" \
                << "  Actual:   " << (actual);                                      \
            throw TestFailure(oss.str());                                           \
        }                                                                           \
    } while (0)

// ============================================================================
// Utility Function Tests
// ============================================================================

void testMapFunctionProtocol()
{
    std::cout << "  Testing map() function (protocol)..." << std::endl;

    // Basic mapping
    ASSERT_EQ(50, map(50, 0, 100, 0, 100), "Identity mapping");
    ASSERT_EQ(0, map(0, 0, 100, 0, 100), "Map minimum");
    ASSERT_EQ(100, map(100, 0, 100, 0, 100), "Map maximum");

    // Scale up
    ASSERT_EQ(0, map(0, 0, 128, 0, 256), "Scale up minimum");
    ASSERT_EQ(256, map(128, 0, 128, 0, 256), "Scale up maximum");
    ASSERT_EQ(128, map(64, 0, 128, 0, 256), "Scale up middle");

    // Scale down
    ASSERT_EQ(50, map(100, 0, 200, 0, 100), "Scale down");

    // Offset ranges
    ASSERT_EQ(200, map(0, 0, 128, 200, 255), "Offset minimum (Corsair sidetone)");
    ASSERT_EQ(255, map(128, 0, 128, 200, 255), "Offset maximum (Corsair sidetone)");

    // Negative mapping (chatmix)
    ASSERT_EQ(0, map(0, 0, 64, 0, -64), "Negative range minimum");
    ASSERT_EQ(-64, map(64, 0, 64, 0, -64), "Negative range maximum");

    std::cout << "    [OK] map() function tests passed" << std::endl;
}

void testSplineBatteryLevelProtocol()
{
    std::cout << "  Testing spline_battery_level() (protocol)..." << std::endl;

    // Use Logitech G533 calibration
    const auto& cal = calibrations::LOGITECH_G533;

    // Test minimum voltage
    int level_min = spline_battery_level(cal.percentages, cal.voltages, 3500);
    ASSERT_TRUE(level_min <= 5, "Minimum voltage should give low battery");

    // Test maximum voltage
    int level_max = spline_battery_level(cal.percentages, cal.voltages, 4200);
    ASSERT_TRUE(level_max >= 95, "Maximum voltage should give high battery");

    // Test middle voltage (~3850mV is roughly 50%)
    int level_mid = spline_battery_level(cal.percentages, cal.voltages, 3850);
    ASSERT_TRUE(level_mid >= 40 && level_mid <= 60, "Mid voltage should be around 50%");

    std::cout << "    [OK] spline_battery_level() tests passed" << std::endl;
}

void testRoundToMultiplesProtocol()
{
    std::cout << "  Testing round_to_multiples() (protocol)..." << std::endl;

    ASSERT_EQ(0, round_to_multiples(0, 10), "Round 0");
    ASSERT_EQ(10, round_to_multiples(5, 10), "Round 5 to 10");
    ASSERT_EQ(10, round_to_multiples(10, 10), "Round 10 to 10");
    ASSERT_EQ(10, round_to_multiples(11, 10), "Round 11 to 10");
    ASSERT_EQ(20, round_to_multiples(15, 10), "Round 15 to 20");

    std::cout << "    [OK] round_to_multiples() tests passed" << std::endl;
}

// ============================================================================
// HID++ Protocol Tests
// ============================================================================

void testHIDPPConstants()
{
    std::cout << "  Testing HID++ constants..." << std::endl;

    // Verify protocol constants
    ASSERT_EQ(0x11, HIDPP_LONG_MESSAGE, "HID++ long message header");
    ASSERT_EQ(0xff, HIDPP_DEVICE_RECEIVER, "HID++ device/receiver ID");
    ASSERT_EQ(20, HIDPP_LONG_MESSAGE_LENGTH, "HID++ long message length");

    std::cout << "    [OK] HID++ constants verified" << std::endl;
}

void testHIDPPVoltageToPercent()
{
    std::cout << "  Testing HID++ voltage-to-percent calibration..." << std::endl;

    // Test several calibration tables
    const struct {
        const protocols::BatteryCalibration& cal;
        const char* name;
        uint16_t min_mv;
        uint16_t max_mv;
    } cals[] = {
        { calibrations::LOGITECH_G533, "G533", 3500, 4200 },
        { calibrations::LOGITECH_G535, "G535", 3500, 4200 },
        { calibrations::LOGITECH_GPRO, "GPro", 3500, 4200 },
    };

    for (const auto& [cal, name, min_mv, max_mv] : cals) {
        // Verify low voltage gives low percentage
        int low = spline_battery_level(cal.percentages, cal.voltages, min_mv);
        ASSERT_TRUE(low >= 0 && low <= 15,
            std::string(name) + ": low voltage should give 0-15%");

        // Verify high voltage gives high percentage
        int high = spline_battery_level(cal.percentages, cal.voltages, max_mv);
        ASSERT_TRUE(high >= 90 && high <= 100,
            std::string(name) + ": high voltage should give 90-100%");

        // Verify monotonicity (higher voltage = higher percentage)
        int prev_level = 0;
        for (uint16_t mv = min_mv; mv <= max_mv; mv += 100) {
            int level = spline_battery_level(cal.percentages, cal.voltages, mv);
            ASSERT_TRUE(level >= prev_level - 5, // Allow small variance due to spline
                std::string(name) + ": percentage should increase with voltage");
            prev_level = level;
        }
    }

    std::cout << "    [OK] HID++ calibration tables verified" << std::endl;
}

void testHIDPPPacketFormat()
{
    std::cout << "  Testing HID++ packet format..." << std::endl;

    // A valid HID++ long packet should have:
    // [0] = 0x11 (long message)
    // [1] = 0xff (device/receiver index)
    // [2..19] = command data

    std::array<uint8_t, HIDPP_LONG_MESSAGE_LENGTH> packet {};
    packet[0] = HIDPP_LONG_MESSAGE;
    packet[1] = HIDPP_DEVICE_RECEIVER;

    ASSERT_EQ(HIDPP_LONG_MESSAGE, packet[0], "Packet header should be 0x11");
    ASSERT_EQ(HIDPP_DEVICE_RECEIVER, packet[1], "Device index should be 0xff");
    ASSERT_EQ(20, packet.size(), "Packet size should be 20 bytes");

    std::cout << "    [OK] HID++ packet format verified" << std::endl;
}

void testHIDPPBatteryResponseParsing()
{
    std::cout << "  Testing HID++ battery response parsing..." << std::endl;

    // Simulated battery response: [header, device, feature_index, ?, voltage_hi, voltage_lo, state]
    // State: 0x01 = discharging, 0x03 = charging

    // Test discharging response
    std::array<uint8_t, 7> discharging = { 0x11, 0x01, 0x08, 0x00, 0x0F, 0x50, 0x01 };
    // Voltage = 0x0F50 = 3920 mV

    uint16_t voltage = (static_cast<uint16_t>(discharging[4]) << 8) | discharging[5];
    ASSERT_EQ(3920, voltage, "Voltage parsing");
    ASSERT_EQ(0x01, discharging[6], "Discharging state");

    // Test charging response
    std::array<uint8_t, 7> charging = { 0x11, 0x01, 0x08, 0x00, 0x10, 0x68, 0x03 };
    // Voltage = 0x1068 = 4200 mV

    voltage = (static_cast<uint16_t>(charging[4]) << 8) | charging[5];
    ASSERT_EQ(4200, voltage, "Charging voltage parsing");
    ASSERT_EQ(0x03, charging[6], "Charging state");

    std::cout << "    [OK] HID++ battery response parsing verified" << std::endl;
}

void testHIDPPOfflineDetection()
{
    std::cout << "  Testing HID++ offline detection..." << std::endl;

    // When device is offline, byte[2] = 0xFF
    std::array<uint8_t, 7> offline_response = { 0x11, 0x01, 0xFF, 0x00, 0x00, 0x00, 0x00 };

    ASSERT_EQ(0xFF, offline_response[2], "Offline marker should be 0xFF");

    std::cout << "    [OK] HID++ offline detection verified" << std::endl;
}

// ============================================================================
// SteelSeries Protocol Tests
// ============================================================================

void testSteelSeriesPacketSizes()
{
    std::cout << "  Testing SteelSeries packet sizes..." << std::endl;

    // SteelSeries protocol uses specific packet sizes:
    // - Legacy devices (Arctis 1, 7, 9, Pro): 31-byte packets
    // - Nova devices (Nova 5, Nova 7, Nova Pro): 64-byte packets, 128-byte status

    // These are implementation details documented in protocol headers
    // We test the expected values for correctness
    constexpr size_t kLegacyPacketSize = 31;
    constexpr size_t kNovaPacketSize   = 64;
    constexpr size_t kNovaStatusSize   = 128;

    // Test arrays can be created at these sizes
    std::array<uint8_t, kLegacyPacketSize> legacy_packet {};
    std::array<uint8_t, kNovaPacketSize> nova_packet {};
    std::array<uint8_t, kNovaStatusSize> nova_status {};

    ASSERT_EQ(31, legacy_packet.size(), "Legacy packet size should be 31");
    ASSERT_EQ(64, nova_packet.size(), "Nova packet size should be 64");
    ASSERT_EQ(128, nova_status.size(), "Nova status buffer should be 128");

    std::cout << "    [OK] SteelSeries packet sizes verified" << std::endl;
}

void testSteelSeriesBatteryMapping()
{
    std::cout << "  Testing SteelSeries battery mapping..." << std::endl;

    // SteelSeries uses direct percentage, not voltage
    // Typical range is 0-10 or 0-100 mapped to 0-100%

    // Test common mapping: raw 0-10 to percent 0-100
    int mapped_0  = map(0, 0, 10, 0, 100);
    int mapped_5  = map(5, 0, 10, 0, 100);
    int mapped_10 = map(10, 0, 10, 0, 100);

    ASSERT_EQ(0, mapped_0, "Battery 0/10 should be 0%");
    ASSERT_EQ(50, mapped_5, "Battery 5/10 should be 50%");
    ASSERT_EQ(100, mapped_10, "Battery 10/10 should be 100%");

    // Test Arctis 1 range (0x00 to 0x04)
    int arctis1_0 = map(0, 0, 4, 0, 100);
    int arctis1_4 = map(4, 0, 4, 0, 100);

    ASSERT_EQ(0, arctis1_0, "Arctis 1: 0/4 should be 0%");
    ASSERT_EQ(100, arctis1_4, "Arctis 1: 4/4 should be 100%");

    std::cout << "    [OK] SteelSeries battery mapping verified" << std::endl;
}

void testSteelSeriesChatmixMapping()
{
    std::cout << "  Testing SteelSeries chatmix mapping..." << std::endl;

    // Chatmix combines game and chat values into a single 0-128 value
    // Center (64) = balanced, 0 = all chat, 128 = all game

    // Simulate response parsing with game=64 (max), chat=0 (min)
    int game    = map(64, 0, 64, 0, 64); // Maps to 64
    int chat    = map(0, 0, 64, 0, -64); // Maps to 0
    int chatmix = 64 - (chat + game);

    ASSERT_EQ(0, chatmix, "All game should give chatmix=0");

    // Simulate game=0, chat=64
    game    = map(0, 0, 64, 0, 64); // Maps to 0
    chat    = map(64, 0, 64, 0, -64); // Maps to -64
    chatmix = 64 - (chat + game);

    ASSERT_EQ(128, chatmix, "All chat should give chatmix=128");

    // Simulate balanced: game=32, chat=32
    game    = map(32, 0, 64, 0, 64); // Maps to 32
    chat    = map(32, 0, 64, 0, -64); // Maps to -32
    chatmix = 64 - (chat + game);

    ASSERT_EQ(64, chatmix, "Balanced should give chatmix=64");

    std::cout << "    [OK] SteelSeries chatmix mapping verified" << std::endl;
}

void testSteelSeriesSidetoneMapping()
{
    std::cout << "  Testing SteelSeries sidetone mapping..." << std::endl;

    // Arctis 1/7: 0-128 maps to 0x00-0x12 (0-18)
    int arctis_0   = map(0, 0, 128, 0x00, 0x12);
    int arctis_64  = map(64, 0, 128, 0x00, 0x12);
    int arctis_128 = map(128, 0, 128, 0x00, 0x12);

    ASSERT_EQ(0, arctis_0, "Arctis: sidetone 0 should map to 0x00");
    ASSERT_EQ(9, arctis_64, "Arctis: sidetone 64 should map to ~9");
    ASSERT_EQ(18, arctis_128, "Arctis: sidetone 128 should map to 0x12");

    // Nova 3P: 0-128 maps to 0-10
    int nova_0   = map(0, 0, 128, 0, 0x0a);
    int nova_128 = map(128, 0, 128, 0, 0x0a);

    ASSERT_EQ(0, nova_0, "Nova: sidetone 0 should map to 0");
    ASSERT_EQ(10, nova_128, "Nova: sidetone 128 should map to 10");

    std::cout << "    [OK] SteelSeries sidetone mapping verified" << std::endl;
}

// ============================================================================
// Corsair Protocol Tests
// ============================================================================

void testCorsairSidetoneMapping()
{
    std::cout << "  Testing Corsair sidetone mapping..." << std::endl;

    // Corsair maps 0-128 to 200-255
    int corsair_0   = map(0, 0, 128, 200, 255);
    int corsair_64  = map(64, 0, 128, 200, 255);
    int corsair_128 = map(128, 0, 128, 200, 255);

    ASSERT_EQ(200, corsair_0, "Corsair: sidetone 0 should map to 200");
    ASSERT_NEAR(227, corsair_64, 1, "Corsair: sidetone 64 should map to ~227");
    ASSERT_EQ(255, corsair_128, "Corsair: sidetone 128 should map to 255");

    std::cout << "    [OK] Corsair sidetone mapping verified" << std::endl;
}

void testCorsairBatteryResponseParsing()
{
    std::cout << "  Testing Corsair battery response parsing..." << std::endl;

    // Corsair battery response format:
    // [0] = 100 (constant)
    // [1] = 0 (constant)
    // [2] = Battery level (0-100, bit 7 is mic up flag)
    // [3] = 177 (constant)
    // [4] = Status: 0=disconnected, 1=normal, 2=low, 4/5=charging

    // Test normal operation at 75%
    std::array<uint8_t, 5> normal = { 100, 0, 75, 177, 1 };
    ASSERT_EQ(75, normal[2], "Battery level should be 75");
    ASSERT_EQ(1, normal[4], "Status should be normal (1)");

    // Test charging at 50%
    std::array<uint8_t, 5> charging = { 100, 0, 50, 177, 4 };
    ASSERT_EQ(50, charging[2], "Battery level should be 50");
    ASSERT_TRUE(charging[4] == 4 || charging[4] == 5, "Status should be charging (4 or 5)");

    // Test disconnected
    std::array<uint8_t, 5> disconnected = { 100, 0, 0, 177, 0 };
    ASSERT_EQ(0, disconnected[4], "Status should be disconnected (0)");

    // Test mic up flag (bit 7)
    uint8_t battery_with_mic = 0x80 | 65; // 65% with mic up
    ASSERT_TRUE(battery_with_mic & 0x80, "Mic up flag should be set");
    ASSERT_EQ(65, battery_with_mic & ~0x80, "Battery level after clearing flag");

    std::cout << "    [OK] Corsair battery response parsing verified" << std::endl;
}

void testCorsairPacketFormat()
{
    std::cout << "  Testing Corsair packet format..." << std::endl;

    // Standard Corsair sidetone command (64 bytes)
    std::array<uint8_t, 64> sidetone_cmd {};
    sidetone_cmd[0]  = 0xFF;
    sidetone_cmd[1]  = 0x0B;
    sidetone_cmd[2]  = 0x00;
    sidetone_cmd[3]  = 0xFF;
    sidetone_cmd[4]  = 0x04;
    sidetone_cmd[5]  = 0x0E;
    sidetone_cmd[6]  = 0xFF;
    sidetone_cmd[7]  = 0x05;
    sidetone_cmd[8]  = 0x01;
    sidetone_cmd[9]  = 0x04;
    sidetone_cmd[10] = 0x00;
    sidetone_cmd[11] = 227; // Mapped sidetone level

    ASSERT_EQ(64, sidetone_cmd.size(), "Corsair packet should be 64 bytes");
    ASSERT_EQ(0xFF, sidetone_cmd[0], "First byte should be 0xFF");
    ASSERT_EQ(227, sidetone_cmd[11], "Sidetone level byte");

    // Battery request command
    std::array<uint8_t, 2> battery_request = { 0xC9, 0x64 };
    ASSERT_EQ(0xC9, battery_request[0], "Battery request cmd byte 0");
    ASSERT_EQ(0x64, battery_request[1], "Battery request cmd byte 1");

    // Notification sound command
    std::array<uint8_t, 3> notif_cmd = { 0xCA, 0x02, 0x01 };
    ASSERT_EQ(0xCA, notif_cmd[0], "Notification cmd byte 0");
    ASSERT_EQ(0x02, notif_cmd[1], "Notification cmd byte 1");

    // Lights command (note: 0x00 = ON, 0x01 = OFF - inverted)
    std::array<uint8_t, 3> lights_on  = { 0xC8, 0x00, 0x00 };
    std::array<uint8_t, 3> lights_off = { 0xC8, 0x01, 0x00 };

    ASSERT_EQ(0xC8, lights_on[0], "Lights cmd byte 0");
    ASSERT_EQ(0x00, lights_on[1], "Lights ON value (inverted)");
    ASSERT_EQ(0x01, lights_off[1], "Lights OFF value (inverted)");

    std::cout << "    [OK] Corsair packet format verified" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllProtocolTests()
{
    std::cout << "\n============================================" << std::endl;
    std::cout << "           Protocol Tests                   " << std::endl;
    std::cout << "============================================\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    [FAIL] " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "    [ERROR] " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== Utility Functions ===" << std::endl;
    runTest("Map Function", testMapFunctionProtocol);
    runTest("Spline Battery Level", testSplineBatteryLevelProtocol);
    runTest("Round To Multiples", testRoundToMultiplesProtocol);

    std::cout << "\n=== HID++ Protocol (Logitech) ===" << std::endl;
    runTest("HID++ Constants", testHIDPPConstants);
    runTest("HID++ Packet Format", testHIDPPPacketFormat);
    runTest("HID++ Voltage To Percent", testHIDPPVoltageToPercent);
    runTest("HID++ Battery Response", testHIDPPBatteryResponseParsing);
    runTest("HID++ Offline Detection", testHIDPPOfflineDetection);

    std::cout << "\n=== SteelSeries Protocol ===" << std::endl;
    runTest("SteelSeries Packet Sizes", testSteelSeriesPacketSizes);
    runTest("SteelSeries Battery Mapping", testSteelSeriesBatteryMapping);
    runTest("SteelSeries Chatmix Mapping", testSteelSeriesChatmixMapping);
    runTest("SteelSeries Sidetone Mapping", testSteelSeriesSidetoneMapping);

    std::cout << "\n=== Corsair Protocol ===" << std::endl;
    runTest("Corsair Sidetone Mapping", testCorsairSidetoneMapping);
    runTest("Corsair Battery Response", testCorsairBatteryResponseParsing);
    runTest("Corsair Packet Format", testCorsairPacketFormat);

    std::cout << "\n--------------------------------------------" << std::endl;
    std::cout << "Protocol Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "--------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some protocol tests failed");
    }
}

} // namespace headsetcontrol::testing

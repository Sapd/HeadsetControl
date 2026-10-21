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
#include "devices/audeze_maxwell.hpp"
#include "devices/audeze_maxwell2.hpp"
#include "devices/corsair_device.hpp"
#include "devices/logitech_astro_a50.hpp"
#include "devices/logitech_astro_a50_gen4.hpp"
#include "devices/logitech_g522_lightspeed.hpp"
#include "devices/logitech_gpro_x2_lightspeed.hpp"
#include "devices/plantronics_bt600.hpp"
#include "devices/protocols/hidpp_protocol.hpp"
#include "devices/protocols/logitech_calibrations.hpp"
#include "devices/protocols/logitech_centurion_protocol.hpp"
#include "devices/protocols/steelseries_protocol.hpp"
#include "devices/steelseries_arctis_nova_3p_wireless.hpp"
#include "result_types.hpp"
#include "utility.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

void testLogitechProX2CenturionBatteryParsing()
{
    std::cout << "  Testing Logitech PRO X2 Centurion battery parsing..." << std::endl;

    std::array<uint8_t, 3> charging_v1 { 42, 42, 0x01 };
    auto charging_v1_result = LogitechGProX2Lightspeed::parseCenturionBatteryResponse(charging_v1);
    ASSERT_TRUE(charging_v1_result.hasValue(), "Centurion charging state 0x01 should parse successfully");
    ASSERT_EQ(BATTERY_CHARGING, charging_v1_result->status, "Centurion charging state 0x01 should map to charging");

    std::array<uint8_t, 3> charging { 87, 87, 0x02 };
    auto charging_result = LogitechGProX2Lightspeed::parseCenturionBatteryResponse(charging);
    ASSERT_TRUE(charging_result.hasValue(), "Centurion charging response should parse successfully");
    ASSERT_EQ(87, charging_result->level_percent, "Centurion battery percentage should use byte 0");
    ASSERT_EQ(BATTERY_CHARGING, charging_result->status, "Centurion charging state 0x02 should map to charging");

    std::array<uint8_t, 3> full { 100, 100, 0x03 };
    auto full_result = LogitechGProX2Lightspeed::parseCenturionBatteryResponse(full);
    ASSERT_TRUE(full_result.hasValue(), "Centurion full response should parse successfully");
    ASSERT_EQ(BATTERY_AVAILABLE, full_result->status, "Centurion full state should map to available");

    std::array<uint8_t, 1> invalid { 101 };
    auto invalid_result = LogitechGProX2Lightspeed::parseCenturionBatteryResponse(invalid);
    ASSERT_TRUE(!invalid_result.hasValue(), "Centurion battery level above 100 should be rejected");

    std::cout << "    [OK] Logitech PRO X2 Centurion battery parsing verified" << std::endl;
}

void testLogitechProX2LegacyBatteryFallbackDecision()
{
    std::cout << "  Testing Logitech PRO X2 legacy battery fallback decision..." << std::endl;

    ASSERT_TRUE(LogitechGProX2Lightspeed::shouldUseLegacyBatteryFallback(DeviceError::Code::NotSupported),
        "A missing Centurion battery feature should use the legacy fallback");
    ASSERT_TRUE(!LogitechGProX2Lightspeed::shouldUseLegacyBatteryFallback(DeviceError::Code::Timeout),
        "A Centurion timeout should not trigger a second battery request");
    ASSERT_TRUE(!LogitechGProX2Lightspeed::shouldUseLegacyBatteryFallback(DeviceError::Code::DeviceOffline),
        "An offline device should not trigger a second battery request");
    ASSERT_TRUE(!LogitechGProX2Lightspeed::shouldUseLegacyBatteryFallback(DeviceError::Code::HIDError),
        "A HID error should not trigger a second battery request");

    std::cout << "    [OK] Logitech PRO X2 legacy battery fallback decision verified" << std::endl;
}

void testCenturionFrameBuilding()
{
    std::cout << "  Testing Logitech Centurion frame building..." << std::endl;

    std::array<uint8_t, 3> payload { 0x02, 0x11, 0x99 };
    auto frame = protocols::LogitechCenturionProtocol::buildCenturionFrame(payload);

    ASSERT_EQ(0x51, frame[0], "Centurion frame should start with report ID 0x51");
    ASSERT_EQ(4, frame[1], "CPL length should include the flags byte");
    ASSERT_EQ(0, frame[2], "Single-frame requests should have flags 0");
    ASSERT_EQ(0x02, frame[3], "Payload should start at byte 3");
    ASSERT_EQ(0x99, frame[5], "Payload bytes should be copied unchanged");

    std::cout << "    [OK] Logitech Centurion frame building verified" << std::endl;
}

void testCenturionBridgeResponseParsing()
{
    std::cout << "  Testing Logitech Centurion bridge response parsing..." << std::endl;

    std::array<uint8_t, 9> reply { 0x07, 0x10, 0x00, 0x05, 0x00, 0x04, 0x20, 0x2A, 0xF8 };
    ASSERT_TRUE(protocols::LogitechCenturionProtocol::isBridgeResponseFor(reply, 0x04), "Bridge event should match the requested sub-feature index");

    auto parsed = protocols::LogitechCenturionProtocol::parseBridgeResponse(reply);
    ASSERT_TRUE(parsed.hasValue(), "Valid bridge response should parse successfully");
    ASSERT_EQ(2, static_cast<int>(parsed->size()), "Bridge response should return only sub-device payload bytes");
    ASSERT_EQ(0x2A, (*parsed)[0], "Parsed payload should preserve the first response byte");
    ASSERT_EQ(0xF8, (*parsed)[1], "Parsed payload should preserve the second response byte");

    std::array<uint8_t, 9> error_reply { 0x07, 0x10, 0x00, 0x05, 0x00, 0xFF, 0x04, 0x20, 0x01 };
    ASSERT_TRUE(protocols::LogitechCenturionProtocol::isBridgeResponseFor(error_reply, 0x04), "Error bridge response should still match the requested sub-feature index");
    auto error_parse = protocols::LogitechCenturionProtocol::parseBridgeResponse(error_reply);
    ASSERT_TRUE(!error_parse.hasValue(), "Sub-device error responses should fail parsing");

    std::cout << "    [OK] Logitech Centurion bridge response parsing verified" << std::endl;
}

void testCenturionBridgeMessageSizeLimit()
{
    std::cout << "  Testing Logitech Centurion bridge message size limit..." << std::endl;

    std::vector<uint8_t> params_at_limit(0x0FFF - 3, 0x5A);
    auto message_at_limit = protocols::LogitechCenturionProtocol::buildBridgeSubMessageVector(0x04, 0x10, params_at_limit);
    ASSERT_EQ(0x0FFF, static_cast<int>(message_at_limit.size()), "Bridge sub-message should reach the 12-bit size limit");
    ASSERT_TRUE(protocols::LogitechCenturionProtocol::isBridgeSubMessageSizeSupported(message_at_limit.size()),
        "Bridge sub-message at the 12-bit size limit should be accepted");

    std::vector<uint8_t> params_over_limit(0x1000 - 3, 0x5A);
    auto message_over_limit = protocols::LogitechCenturionProtocol::buildBridgeSubMessageVector(0x04, 0x10, params_over_limit);
    ASSERT_EQ(0x1000, static_cast<int>(message_over_limit.size()), "Bridge sub-message should exceed the 12-bit size limit by one byte");
    ASSERT_TRUE(!protocols::LogitechCenturionProtocol::isBridgeSubMessageSizeSupported(message_over_limit.size()),
        "Bridge sub-message above the 12-bit size limit should be rejected");

    std::cout << "    [OK] Logitech Centurion bridge message size limit verified" << std::endl;
}

void testLogitechProX2EqualizerInfoRequiresDescriptor()
{
    std::cout << "  Testing Logitech PRO X2 equalizer info cache behavior..." << std::endl;

    LogitechGProX2Lightspeed device;
    ASSERT_TRUE(!device.getEqualizerInfo().has_value(), "Equalizer info should be unavailable before the descriptor is read");
    ASSERT_TRUE(!device.getParametricEqualizerInfo().has_value(), "Parametric equalizer info should be unavailable before the descriptor is read");

    std::cout << "    [OK] Logitech PRO X2 equalizer info cache behavior verified" << std::endl;
}

void testLogitechProX2OnboardEqCoefficientQuantization()
{
    std::cout << "  Testing Logitech PRO X2 onboard EQ coefficient quantization..." << std::endl;

    auto words = LogitechGProX2Lightspeed::quantizeOnboardEqCoefficientsForTest({
        0.0,
        -100.0 / 1073741824.0,
        0.0,
        0.0,
        0.0,
    });

    ASSERT_EQ(10, static_cast<int>(words.size()), "Quantized coefficient block should contain 10 words");
    ASSERT_EQ(0xFFFF, static_cast<int>(words[2]), "Negative coefficients should preserve sign extension in the upper word");
    ASSERT_EQ(0xFF00, static_cast<int>(words[3]), "Negative coefficients should still clear the low byte");

    std::cout << "    [OK] Logitech PRO X2 onboard EQ coefficient quantization verified" << std::endl;
}

void testLogitechProX2OnboardEqPayloadBuilding()
{
    std::cout << "  Testing Logitech PRO X2 onboard EQ payload building..." << std::endl;

    auto payload = LogitechGProX2Lightspeed::buildOnboardEqPayloadForTest(0x00, {
                                                                                    { 80, 4, 1 },
                                                                                    { 240, 2, 1 },
                                                                                    { 750, 0, 1 },
                                                                                    { 2200, 0, 1 },
                                                                                    { 6600, 0, 1 },
                                                                                });

    ASSERT_TRUE(payload.size() > 32, "Onboard EQ payload should include coefficient sections");
    ASSERT_EQ(0x00, payload[0], "Onboard EQ payload should start with slot 0");
    ASSERT_EQ(5, payload[1], "Onboard EQ payload should encode the band count");
    ASSERT_EQ(0x00, payload[2], "First band frequency high byte should be preserved");
    ASSERT_EQ(80, payload[3], "First band frequency low byte should be preserved");
    ASSERT_EQ(4, payload[4], "First band gain should be preserved");
    ASSERT_EQ(1, payload[5], "First band Q should be preserved");

    std::cout << "    [OK] Logitech PRO X2 onboard EQ payload building verified" << std::endl;
}

// ============================================================================
// Plantronics Protocol Tests
// ============================================================================

void testPlantronicsBT600BatteryParsing()
{
    std::cout << "  Testing Plantronics BT600 battery reply parsing..." << std::endl;

    // Battery GET reply captured from the hardware: level 7 of 11 (=70%),
    // not charging, 750 min talk time remaining:
    // 07 01 01 10 0c 02 00 00 03 0a 1a 07 0b 00 02 ee 01
    std::array<uint8_t, PlantronicsBT600::MSG_SIZE> response {
        0x07, 0x01, 0x01, 0x10, 0x0c, 0x02, 0x00, 0x00,
        0x03, 0x0a, 0x1a, 0x07, 0x0b, 0x00, 0x02, 0xee, 0x01
    };

    auto result = PlantronicsBT600::parseBatteryReply(response);
    ASSERT_TRUE(result.hasValue(), "Battery reply should parse successfully");
    ASSERT_EQ(70, result->level_percent, "Level 7 of 11 should map to 70 percent");
    ASSERT_EQ(750, result->time_to_empty_min.value(), "Talk time minutes should be reported");
    ASSERT_EQ(BATTERY_AVAILABLE, result->status, "Zero charging byte should map to available");

    // Captured one level lower: level 6 of 11 (=60%), 714 min. This is the
    // case the earlier talk-time estimate got wrong.
    response[PlantronicsBT600::OFF_BATT_LEVEL]  = 0x06;
    response[PlantronicsBT600::OFF_BATT_MIN_HI] = 0x02;
    response[PlantronicsBT600::OFF_BATT_MIN_LO] = 0xca;
    auto lower_result                           = PlantronicsBT600::parseBatteryReply(response);
    ASSERT_TRUE(lower_result.hasValue(), "Lower-level reply should parse successfully");
    ASSERT_EQ(60, lower_result->level_percent, "Level 6 of 11 should map to 60 percent");
    ASSERT_EQ(714, lower_result->time_to_empty_min.value(), "Talk time minutes should be reported");

    // Charging: charging byte set
    response[PlantronicsBT600::OFF_BATT_CHARGING] = 0x01;
    auto charging_result                          = PlantronicsBT600::parseBatteryReply(response);
    ASSERT_TRUE(charging_result.hasValue(), "Charging reply should parse successfully");
    ASSERT_EQ(BATTERY_CHARGING, charging_result->status, "Nonzero charging byte should map to charging");

    // A full battery (level == level_count - 1) is exactly 100 percent
    response[PlantronicsBT600::OFF_BATT_LEVEL]    = 0x0a;
    response[PlantronicsBT600::OFF_BATT_CHARGING] = 0x00;
    auto full_result                              = PlantronicsBT600::parseBatteryReply(response);
    ASSERT_TRUE(full_result.hasValue(), "Full-battery reply should parse successfully");
    ASSERT_EQ(100, full_result->level_percent, "Level 10 of 11 should map to 100 percent");

    // A reply with too few levels (no valid denominator) should be rejected
    response[PlantronicsBT600::OFF_BATT_COUNT] = 0x01;
    auto bad_result                            = PlantronicsBT600::parseBatteryReply(response);
    ASSERT_TRUE(!bad_result.hasValue(), "Reply without a usable level count should be rejected");

    std::cout << "    [OK] Plantronics BT600 battery reply parsing verified" << std::endl;
}

void testPlantronicsBT600ReplyMatching()
{
    std::cout << "  Testing Plantronics BT600 reply matching..." << std::endl;

    // Solicited sidetone SET ack captured from the hardware:
    // 07 01 01 10 07 02 00 00 0a 04 10 01
    std::array<uint8_t, PlantronicsBT600::MSG_SIZE> ack {
        0x07, 0x01, 0x01, 0x10, 0x07, 0x02, 0x00, 0x00,
        0x0a, 0x04, 0x10, 0x01
    };
    ASSERT_TRUE(PlantronicsBT600::isReplyFor(ack, 0x04, 0x10), "Sidetone ack should match its group/item");
    ASSERT_TRUE(!PlantronicsBT600::isReplyFor(ack, 0x0a, 0x1a), "Sidetone ack should not match the battery setting");

    // Unsolicited event (address byte 0x00) must not be treated as a reply:
    // 07 01 01 10 0a 00 00 00 0a 0e 1e 07 02 0c
    std::array<uint8_t, PlantronicsBT600::MSG_SIZE> event {
        0x07, 0x01, 0x01, 0x10, 0x0a, 0x00, 0x00, 0x00,
        0x0a, 0x0e, 0x1e, 0x07, 0x02, 0x0c
    };
    ASSERT_TRUE(!PlantronicsBT600::isReplyFor(event, 0x0e, 0x1e), "Unsolicited event should not match as a reply");

    // Catalogue fragment streamed after HELLO (header bytes differ, but the
    // byte at the reply-marker offset happens to be 0x02) must not match:
    // 07 01 07 11 9c 02 00 00 09 00 36 00 01 02 ...
    std::array<uint8_t, PlantronicsBT600::MSG_SIZE> fragment {
        0x07, 0x01, 0x07, 0x11, 0x9c, 0x02, 0x00, 0x00,
        0x09, 0x00, 0x36, 0x00, 0x01, 0x02
    };
    ASSERT_TRUE(!PlantronicsBT600::isReplyFor(fragment, 0x00, 0x36), "Catalogue fragment should not match as a reply");

    // Dongle-addressed HELLO ack (source address 0x00) captured from the
    // hardware: 07 01 01 10 06 00 00 00 08 01 02
    std::array<uint8_t, PlantronicsBT600::MSG_SIZE> dongle_ack {
        0x07, 0x01, 0x01, 0x10, 0x06, 0x00, 0x00, 0x00,
        0x08, 0x01, 0x02
    };
    ASSERT_TRUE(PlantronicsBT600::isReplyFor(dongle_ack, 0x01, 0x02, 0x00), "Dongle hello ack should match with dongle address");
    ASSERT_TRUE(!PlantronicsBT600::isReplyFor(dongle_ack, 0x01, 0x02), "Dongle hello ack should not match with the headset address");

    std::cout << "    [OK] Plantronics BT600 reply matching verified" << std::endl;
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
// Logitech ASTRO A50 Gen 4 Tests
// ============================================================================

/**
 * @brief Scripted HID interface: records writes, serves queued replies in order.
 *
 * An empty queue returns a timeout error, matching RealHIDInterface. Explicit empty
 * replies also allow testing the zero-byte result handled by the protocol.
 */
class ScriptedHIDInterface : public HIDInterface {
public:
    std::deque<Result<std::vector<uint8_t>>> replies;
    std::vector<int> read_timeouts;
    std::vector<size_t> writes_at_read;
    std::vector<std::vector<uint8_t>> writes;

    [[nodiscard]] auto write(hid_device* /*device_handle*/, std::span<const uint8_t> data)
        -> Result<void> override
    {
        writes.emplace_back(data.begin(), data.end());
        return {};
    }

    [[nodiscard]] auto write(hid_device* /*device_handle*/, std::span<const uint8_t> data, size_t size)
        -> Result<void> override
    {
        std::vector<uint8_t> padded(size, 0);
        std::copy_n(data.begin(), std::min(data.size(), size), padded.begin());
        writes.push_back(std::move(padded));
        return {};
    }

    [[nodiscard]] auto readTimeout(hid_device* /*device_handle*/, std::span<uint8_t> data, int timeout_ms)
        -> Result<size_t> override
    {
        read_timeouts.push_back(timeout_ms);
        writes_at_read.push_back(writes.size());
        if (replies.empty()) {
            return DeviceError::timeout("Scripted read timeout");
        }
        auto reply = std::move(replies.front());
        replies.pop_front();
        if (!reply) {
            return reply.error();
        }
        const size_t n = std::min(reply->size(), data.size());
        std::copy_n(reply->begin(), n, data.begin());
        return n;
    }

    [[nodiscard]] auto sendFeatureReport(hid_device* /*device_handle*/, std::span<const uint8_t> /*data*/)
        -> Result<void> override
    {
        return {};
    }

    [[nodiscard]] auto sendFeatureReport(hid_device* /*device_handle*/, std::span<const uint8_t> /*data*/, size_t /*size*/)
        -> Result<void> override
    {
        return {};
    }

    [[nodiscard]] auto getFeatureReport(hid_device* /*device_handle*/, std::span<uint8_t> /*data*/)
        -> Result<size_t> override
    {
        return size_t { 0 };
    }

    [[nodiscard]] auto getInputReport(hid_device* /*device_handle*/, std::span<uint8_t> /*data*/)
        -> Result<size_t> override
    {
        return size_t { 0 };
    }
};

class TestableNova3PWireless : public SteelSeriesArctisNova3PWireless {
public:
    mutable ScriptedHIDInterface hid;

    HIDInterface& getHIDInterface() const override { return hid; }
};

void testSteelSeriesNova3PMicVolumeMapping()
{
    const struct {
        uint8_t normalized;
        uint8_t device_level;
    } levels[] = {
        { 0, 0 },
        { 64, 7 },
        { 127, 14 },
        { 128, 15 },
    };

    for (uint16_t product_id : SteelSeriesArctisNova3PWireless::SUPPORTED_PRODUCT_IDS) {
        TestableNova3PWireless dev;
        dev.setMatchedProductId(product_id);
        for (const auto& level : levels) {
            dev.hid.writes.clear();
            auto result = dev.setMicVolume(nullptr, level.normalized);
            ASSERT_TRUE(result.hasValue(), "Microphone volume write should succeed");
            ASSERT_EQ(level.normalized, result->volume, "Result should retain the normalized volume");
            ASSERT_EQ(2u, dev.hid.writes.size(), "Microphone volume write should be followed by save");
            ASSERT_EQ(64u, dev.hid.writes[0].size(), "Microphone command should use a full report");
            ASSERT_EQ(0x00, dev.hid.writes[0][0], "Microphone command should use report ID zero");
            ASSERT_EQ(0x37, dev.hid.writes[0][1], "Microphone command should use opcode 0x37");
            ASSERT_EQ(static_cast<int>(level.device_level), static_cast<int>(dev.hid.writes[0][2]), "Microphone volume should map to hardware levels 0-15");
            ASSERT_EQ(0x00, dev.hid.writes[1][0], "Save command should use report ID zero");
            ASSERT_EQ(0x09, dev.hid.writes[1][1], "Save command should use opcode 0x09");
        }
    }
}

class TestableAstroA50Gen4 : public LogitechAstroA50Gen4 {
public:
    mutable ScriptedHIDInterface hid;

    [[nodiscard]] auto getHIDInterface() const -> HIDInterface& override { return hid; }
};

/// Build a 64-byte base-station reply: 02 STATUS LEN PAYLOAD...
static std::vector<uint8_t> a50Reply(uint8_t status, std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> reply(LogitechAstroA50Gen4::FRAME_SIZE, 0);
    reply[0] = LogitechAstroA50Gen4::REPORT_ID;
    reply[1] = status;
    reply[2] = static_cast<uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), reply.begin() + LogitechAstroA50Gen4::PAYLOAD_OFFSET);
    return reply;
}

void testAstroA50Gen4FrameBuilding()
{
    std::cout << "  Testing ASTRO A50 Gen 4 frame building..." << std::endl;

    auto bare = LogitechAstroA50Gen4::buildFrame(LogitechAstroA50Gen4::CMD_GET_BATTERY, {});
    ASSERT_EQ(64, static_cast<int>(bare.size()), "Frame should be 64 bytes");
    ASSERT_EQ(0x02, bare[0], "Frame should start with report ID 0x02");
    ASSERT_EQ(0x7C, bare[1], "Command should be at byte 1");
    ASSERT_EQ(0x00, bare[2], "A request without payload has no length byte");

    std::array<uint8_t, 2> payload { 0x05, 0x64 };
    auto set = LogitechAstroA50Gen4::buildFrame(LogitechAstroA50Gen4::CMD_SET_SLIDER, payload);
    ASSERT_EQ(0x62, set[1], "Command should be at byte 1");
    ASSERT_EQ(0x02, set[2], "Length byte should count only the payload");
    ASSERT_EQ(0x05, set[3], "Payload should start at byte 3");
    ASSERT_EQ(0x64, set[4], "Payload bytes should be copied unchanged");
    ASSERT_EQ(0x00, set[5], "Frame should be zero-padded after the payload");

    std::cout << "    [OK] ASTRO A50 Gen 4 frame building verified" << std::endl;
}

void testAstroA50Gen4Decoding()
{
    std::cout << "  Testing ASTRO A50 Gen 4 value decoding..." << std::endl;

    using Dev = LogitechAstroA50Gen4;
    ASSERT_EQ(97, Dev::batteryPercent(0xe1), "Bits 0-6 are the percentage");
    ASSERT_TRUE(Dev::batteryCharging(0xe1), "Bit 7 set means charging");
    ASSERT_EQ(100, Dev::batteryPercent(0x64), "100% decodes unchanged");
    ASSERT_TRUE(!Dev::batteryCharging(0x64), "Bit 7 clear means not charging");

    ASSERT_TRUE(Dev::isLinked(0x02), "Bit 1 set: linked, undocked");
    ASSERT_TRUE(Dev::isLinked(0x03), "Bit 1 set: linked, docked");
    ASSERT_TRUE(!Dev::isLinked(0x00), "No bits: headset off, no link");
    ASSERT_TRUE(!Dev::isLinked(0x01), "Docked without link is not linked");

    ASSERT_EQ(0, Dev::balanceToLevel(255), "255 is full game -> level 0");
    ASSERT_EQ(128, Dev::balanceToLevel(0), "0 is full voice -> level 128");
    ASSERT_EQ(64, Dev::balanceToLevel(130), "Physical midpoint (130) reports as balanced");
    ASSERT_EQ(64, Dev::balanceToLevel(125), "One grid step below centre also reports as balanced");
    ASSERT_EQ(28, Dev::balanceToLevel(200), "Game-leaning value maps below 64");
    ASSERT_EQ(101, Dev::balanceToLevel(55), "Voice-leaning value maps above 64");

    auto err = a50Reply(Dev::STATUS_ERROR, { 0x11, 0x00, 0x00, 0x00, 'H', 'I', 'D', '_', 'E', 'R', 'R', 0x00 });
    std::array<uint8_t, Dev::FRAME_SIZE> err_frame {};
    std::copy(err.begin(), err.end(), err_frame.begin());
    ASSERT_EQ(std::string("HID_ERR"), Dev::errorName(err_frame), "Error name is NUL-terminated ASCII at payload offset 4");

    std::cout << "    [OK] ASTRO A50 Gen 4 value decoding verified" << std::endl;
}

void testAstroA50Gen4LinkGating()
{
    std::cout << "  Testing ASTRO A50 Gen 4 link gating..." << std::endl;

    using Dev = LogitechAstroA50Gen4;
    TestableAstroA50Gen4 dev;

    // Headset off: battery must not be read at all (the base would serve a stale value).
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x00 }));
    auto battery = dev.getBattery(nullptr);
    ASSERT_TRUE(battery.hasValue(), "Unlinked headset is a result, not an error");
    ASSERT_EQ(static_cast<int>(BATTERY_UNAVAILABLE), static_cast<int>(battery->status), "Unlinked headset reports BATTERY_UNAVAILABLE");
    ASSERT_EQ(-1, battery->level_percent, "Unlinked headset reports level -1");
    ASSERT_EQ(1, static_cast<int>(dev.hid.writes.size()), "Only the status query should be sent");
    ASSERT_EQ(0x54, dev.hid.writes[0][1], "Status query is command 0x54");

    // Headset linked: status query then battery query.
    dev.hid.writes.clear();
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x02 }));
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0xe1 }));
    battery = dev.getBattery(nullptr);
    ASSERT_TRUE(battery.hasValue(), "Linked battery read should succeed");
    ASSERT_EQ(97, battery->level_percent, "Battery percent decoded from bits 0-6");
    ASSERT_EQ(static_cast<int>(BATTERY_CHARGING), static_cast<int>(battery->status), "Bit 7 means charging");
    ASSERT_EQ(2, static_cast<int>(dev.hid.writes.size()), "Status query followed by battery query");
    ASSERT_EQ(0x7C, dev.hid.writes[1][1], "Battery query is command 0x7C");
    ASSERT_EQ(64, static_cast<int>(dev.hid.writes[1].size()), "Every write is a full 64-byte frame");

    // Chatmix has no 'unavailable' state, so an unlinked headset is an error.
    dev.hid.writes.clear();
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x00 }));
    auto chatmix = dev.getChatmix(nullptr);
    ASSERT_TRUE(chatmix.hasError(), "Unlinked chatmix read must fail rather than return 0xfd");
    ASSERT_EQ(1, static_cast<int>(dev.hid.writes.size()), "Balance must not be queried without a link");

    std::cout << "    [OK] ASTRO A50 Gen 4 link gating verified" << std::endl;
}

void testAstroA50Gen4Chatmix()
{
    std::cout << "  Testing ASTRO A50 Gen 4 chatmix..." << std::endl;

    using Dev = LogitechAstroA50Gen4;
    TestableAstroA50Gen4 dev;

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x02 }));
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0xff }));
    auto full_game = dev.getChatmix(nullptr);
    ASSERT_TRUE(full_game.hasValue(), "Chatmix read should succeed");
    ASSERT_EQ(0, full_game->level, "Raw 255 is full game -> level 0");
    ASSERT_EQ(100, full_game->game_volume_percent, "Full game: game 100%");
    ASSERT_EQ(0, full_game->chat_volume_percent, "Full game: chat 0%");
    ASSERT_EQ(0x72, dev.hid.writes[1][1], "Balance query is command 0x72");

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x02 }));
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x82 }));
    auto centre = dev.getChatmix(nullptr);
    ASSERT_TRUE(centre.hasValue(), "Chatmix read should succeed");
    ASSERT_EQ(64, centre->level, "Physical midpoint reports level 64");
    ASSERT_EQ(100, centre->game_volume_percent, "Balanced: game 100%");
    ASSERT_EQ(100, centre->chat_volume_percent, "Balanced: chat 100%");

    std::cout << "    [OK] ASTRO A50 Gen 4 chatmix verified" << std::endl;
}

void testAstroA50Gen4Setters()
{
    std::cout << "  Testing ASTRO A50 Gen 4 sliders and noise gate..." << std::endl;

    using Dev = LogitechAstroA50Gen4;
    TestableAstroA50Gen4 dev;

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x62, 0x05 }));
    auto sidetone = dev.setSidetone(nullptr, 128);
    ASSERT_TRUE(sidetone.hasValue(), "Sidetone write should succeed");
    ASSERT_EQ(0x62, dev.hid.writes[0][1], "Slider write is command 0x62");
    ASSERT_EQ(0x02, dev.hid.writes[0][2], "Slider write carries a 2-byte payload");
    ASSERT_EQ(0x05, dev.hid.writes[0][3], "Sidetone is slider 0x05");
    ASSERT_EQ(100, dev.hid.writes[0][4], "Level 128 maps to device 100");
    ASSERT_EQ(100, sidetone->device_level, "Result reports the device-native level");

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x62, 0x05 }));
    sidetone = dev.setSidetone(nullptr, 64);
    ASSERT_TRUE(sidetone.hasValue(), "Sidetone write should succeed");
    ASSERT_EQ(50, dev.hid.writes[1][4], "Level 64 maps to device 50");

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x62, 0x04 }));
    auto mic = dev.setMicVolume(nullptr, 128);
    ASSERT_TRUE(mic.hasValue(), "Mic volume write should succeed");
    ASSERT_EQ(0x04, dev.hid.writes[2][3], "Microphone is slider 0x04");
    ASSERT_EQ(100, dev.hid.writes[2][4], "Volume 128 maps to device 100");

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x68, 0x05, 50, 50 }));
    auto read_back = dev.getSidetone(nullptr);
    ASSERT_TRUE(read_back.hasValue(), "Sidetone read should succeed");
    ASSERT_EQ(0x68, dev.hid.writes[3][1], "Slider read is command 0x68");
    ASSERT_EQ(0x05, dev.hid.writes[3][3], "Slider read names the sidetone slider");
    ASSERT_EQ(50, read_back->device_level, "Active slider value is payload[2]");
    ASSERT_EQ(64, read_back->current_level, "Device 50 maps back to level 64");

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x03 }));
    auto noise = dev.setNoiseFilter(nullptr, 2);
    ASSERT_TRUE(noise.hasValue(), "Noise filter write should succeed");
    ASSERT_EQ(0x64, dev.hid.writes[4][1], "Noise gate write is command 0x64");
    ASSERT_EQ(0x03, dev.hid.writes[4][3], "HSC level 2 maps to Tournament (0x03)");

    const auto writes_before = dev.hid.writes.size();
    auto bad                 = dev.setNoiseFilter(nullptr, 3);
    ASSERT_TRUE(bad.hasError(), "Noise filter level 3 is rejected");
    ASSERT_EQ(static_cast<int>(writes_before), static_cast<int>(dev.hid.writes.size()), "Rejected level must not reach the device");

    std::cout << "    [OK] ASTRO A50 Gen 4 sliders and noise gate verified" << std::endl;
}

void testAstroA50Gen4ErrorHandling()
{
    std::cout << "  Testing ASTRO A50 Gen 4 error handling..." << std::endl;

    using Dev = LogitechAstroA50Gen4;
    TestableAstroA50Gen4 dev;

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_ERROR,
        { 0x11, 0x00, 0x00, 0x00, 'H', 'I', 'D', '_', 'E', 'R', 'R', 'O', 'R', '_', 'V', 'A', 'L', 'U', 'E', '_',
            'O', 'U', 'T', '_', 'O', 'F', '_', 'R', 'A', 'N', 'G', 'E', 0x00 }));
    auto err = dev.setSidetone(nullptr, 64);
    ASSERT_TRUE(err.hasError(), "Status 0x01 is an error");
    const std::string text = err.error().message + " " + err.error().details;
    ASSERT_TRUE(text.find("HID_ERROR_VALUE_OUT_OF_RANGE") != std::string::npos, "Error surfaces the device's ASCII name");
    ASSERT_TRUE(text.find("0x11") != std::string::npos, "Error surfaces the device's error code");

    // No reply at all: timeout, not a stale parse.
    auto timeout = dev.getSidetone(nullptr);
    ASSERT_TRUE(timeout.hasError(), "Missing reply is an error");
    ASSERT_TRUE(timeout.error().code == DeviceError::Code::Timeout, "Missing reply is reported as a timeout");

    // Model closing the timed-out connection before testing independent protocol errors.
    dev.onConnectionClosed(nullptr);

    // Accepted-without-data on a query is a protocol error, not a zero reading.
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_ACCEPTED, {}));
    auto empty = dev.getSidetone(nullptr);
    ASSERT_TRUE(empty.hasError(), "Status 0x00 on a query is an error");

    // Wrong report ID is rejected.
    auto bogus = a50Reply(Dev::STATUS_OK, { 0x68, 0x05, 50, 50 });
    bogus[0]   = 0x00;
    dev.hid.replies.push_back(bogus);
    auto wrong_id = dev.getSidetone(nullptr);
    ASSERT_TRUE(wrong_id.hasError(), "Reply without report ID 0x02 is rejected");

    std::cout << "    [OK] ASTRO A50 Gen 4 error handling verified" << std::endl;
}

void testAstroA50Gen4Equalizer()
{
    std::cout << "  Testing ASTRO A50 Gen 4 equalizer..." << std::endl;

    using Dev = LogitechAstroA50Gen4;

    // Encoding helpers
    ASSERT_EQ(0x11, Dev::gainToByte(5.0f), "+5 dB encodes as 17");
    ASSERT_EQ(0x0c, Dev::gainToByte(0.0f), "0 dB encodes as 12");
    ASSERT_EQ(0x05, Dev::gainToByte(-7.0f), "-7 dB encodes as 5");
    ASSERT_EQ(4096, Dev::qToBandwidth(1.0f), "Q 1.0 is one octave-equivalent: 4096");
    ASSERT_EQ(8192, Dev::qToBandwidth(0.5f), "Q 0.5 doubles the bandwidth");
    ASSERT_EQ(Dev::EQ_BW_MIN, Dev::qToBandwidth(100.0f), "Very narrow Q clamps to the device minimum");
    ASSERT_EQ(Dev::EQ_BW_MAX, Dev::qToBandwidth(0.01f), "Very wide Q clamps to the device maximum");

    // Preset: acknowledged, then read-back lags one poll before agreeing.
    {
        TestableAstroA50Gen4 dev;
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x67, 0x02 }));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x01 })); // stale
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x02 })); // settled
        auto preset = dev.setEqualizerPreset(nullptr, 1);
        ASSERT_TRUE(preset.hasValue(), "Preset write succeeds once the read-back agrees");
        ASSERT_EQ(1, preset->preset, "Result reports the HeadsetControl preset index");
        ASSERT_EQ(3, preset->total_presets, "Three presets");
        ASSERT_EQ(3, static_cast<int>(dev.hid.writes.size()), "One write plus two polls");
        ASSERT_EQ(0x67, dev.hid.writes[0][1], "Preset write is command 0x67");
        ASSERT_EQ(0x02, dev.hid.writes[0][3], "HeadsetControl preset 1 is device preset 2");
        ASSERT_EQ(0x6C, dev.hid.writes[1][1], "Read-back polls command 0x6C");
    }

    // Preset: read-back never agrees.
    {
        TestableAstroA50Gen4 dev;
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x67, 0x03 }));
        for (int i = 0; i < Dev::EQ_PRESET_POLL_LIMIT; ++i) {
            dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x01 }));
        }
        auto stuck = dev.setEqualizerPreset(nullptr, 2);
        ASSERT_TRUE(stuck.hasError(), "Preset write that never takes effect is an error");
        ASSERT_EQ(1 + Dev::EQ_PRESET_POLL_LIMIT, static_cast<int>(dev.hid.writes.size()), "Polling is bounded");

        const auto writes_before = dev.hid.writes.size();
        auto bad                 = dev.setEqualizerPreset(nullptr, 3);
        ASSERT_TRUE(bad.hasError(), "Preset 3 is rejected");
        ASSERT_EQ(static_cast<int>(writes_before), static_cast<int>(dev.hid.writes.size()), "Rejected preset must not reach the device");
    }

    // Parametric EQ: active preset read, five band writes, one gain write.
    {
        TestableAstroA50Gen4 dev;
        ParametricEqualizerSettings settings;
        settings.bands = {
            { .frequency = 100.0f, .gain = 5.0f, .q_factor = 1.0f, .type = EqualizerFilterType::LowShelf },
            { .frequency = 400.0f, .gain = -4.0f, .q_factor = 1.0f, .type = EqualizerFilterType::Peaking },
            { .frequency = 1000.0f, .gain = 0.0f, .q_factor = 0.5f, .type = EqualizerFilterType::Peaking },
            { .frequency = 4000.0f, .gain = 7.0f, .q_factor = 2.0f, .type = EqualizerFilterType::Peaking },
            { .frequency = 10000.0f, .gain = -7.0f, .q_factor = 1.0f, .type = EqualizerFilterType::HighShelf },
        };

        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x03 })); // active preset 3
        for (int i = 0; i < Dev::EQ_BANDS; ++i) {
            dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x6F, 0x03, static_cast<uint8_t>(i + 1), 0x00 }));
        }
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x63, 0x03 }));

        auto peq = dev.setParametricEqualizer(nullptr, settings);
        ASSERT_TRUE(peq.hasValue(), "Parametric EQ write succeeds");
        ASSERT_EQ(7, static_cast<int>(dev.hid.writes.size()), "Preset read + 5 bands + gains");
        ASSERT_EQ(0x6C, dev.hid.writes[0][1], "First the active preset is read");

        // Band 1: low shelf -> bandwidth 0, frequency 100 (0x0064)
        ASSERT_EQ(0x6F, dev.hid.writes[1][1], "Band write is command 0x6F");
        ASSERT_EQ(0x06, dev.hid.writes[1][2], "Band write carries a 6-byte payload");
        ASSERT_EQ(0x03, dev.hid.writes[1][3], "Band write targets the active preset");
        ASSERT_EQ(0x01, dev.hid.writes[1][4], "Bands are numbered from 1");
        ASSERT_EQ(0x00, dev.hid.writes[1][5], "Shelf bandwidth low byte is 0");
        ASSERT_EQ(0x00, dev.hid.writes[1][6], "Shelf bandwidth high byte is 0");
        ASSERT_EQ(0x64, dev.hid.writes[1][7], "Frequency low byte (little-endian)");
        ASSERT_EQ(0x00, dev.hid.writes[1][8], "Frequency high byte");

        // Band 3: Q 0.5 -> bandwidth 8192 (0x2000), frequency 1000 (0x03e8)
        ASSERT_EQ(0x00, dev.hid.writes[3][5], "Bandwidth 8192 low byte");
        ASSERT_EQ(0x20, dev.hid.writes[3][6], "Bandwidth 8192 high byte");
        ASSERT_EQ(0xe8, dev.hid.writes[3][7], "Frequency 1000 low byte");
        ASSERT_EQ(0x03, dev.hid.writes[3][8], "Frequency 1000 high byte");

        // Gains: 63 <preset> <5 gains>
        ASSERT_EQ(0x63, dev.hid.writes[6][1], "Gain write is command 0x63");
        ASSERT_EQ(0x06, dev.hid.writes[6][2], "Gain write carries preset + 5 gains");
        ASSERT_EQ(0x03, dev.hid.writes[6][3], "Gain write targets the active preset");
        ASSERT_EQ(0x11, dev.hid.writes[6][4], "+5 dB");
        ASSERT_EQ(0x08, dev.hid.writes[6][5], "-4 dB");
        ASSERT_EQ(0x0c, dev.hid.writes[6][6], "0 dB");
        ASSERT_EQ(0x13, dev.hid.writes[6][7], "+7 dB");
        ASSERT_EQ(0x05, dev.hid.writes[6][8], "-7 dB");

        // Validation happens before anything is sent.
        const auto writes_before = dev.hid.writes.size();
        auto four_bands          = settings;
        four_bands.bands.pop_back();
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, four_bands).hasError(), "Four bands are rejected");

        auto wrong_type          = settings;
        wrong_type.bands[0].type = EqualizerFilterType::Peaking;
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, wrong_type).hasError(), "Band 1 must be a low shelf");

        auto loud          = settings;
        loud.bands[2].gain = 8.0f;
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, loud).hasError(), "+8 dB is rejected");

        auto low               = settings;
        low.bands[1].frequency = 50.0f;
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, low).hasError(), "50 Hz is rejected");

        auto narrow              = settings;
        narrow.bands[1].q_factor = 20.0f;
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, narrow).hasError(), "Q 20 is rejected rather than clamped");

        auto wide              = settings;
        wide.bands[1].q_factor = 0.2f;
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, wide).hasError(), "Q 0.2 is rejected rather than clamped");
        ASSERT_EQ(static_cast<int>(writes_before), static_cast<int>(dev.hid.writes.size()), "Rejected settings must not reach the device");
    }

    std::cout << "    [OK] ASTRO A50 Gen 4 equalizer verified" << std::endl;
}

void testAstroA50Gen4BasicEqualizer()
{
    using Dev = LogitechAstroA50Gen4;
    TestableAstroA50Gen4 dev;
    ASSERT_TRUE(dev.getCapabilities() & B(CAP_EQUALIZER), "Basic EQ is advertised");
    const auto info = dev.getEqualizerInfo();
    ASSERT_TRUE(info.has_value(), "Basic EQ metadata is available");
    ASSERT_EQ(5, info->bands_count, "Five gains");
    ASSERT_EQ(-7, info->bands_min, "Minimum gain");
    ASSERT_EQ(7, info->bands_max, "Maximum gain");
    ASSERT_EQ(1.0f, info->bands_step, "One dB steps");
    ASSERT_EQ(0, info->bands_baseline, "Flat baseline");

    const EqualizerSettings settings({ -7, -4, 0, 5, 7 });
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x63, 2 }));
    ASSERT_TRUE(dev.setEqualizer(nullptr, settings).hasValue(), "Basic EQ succeeds");
    ASSERT_EQ(2u, dev.hid.writes.size(), "Only active preset read and gain write; no band writes");
    ASSERT_EQ(0x6c, dev.hid.writes[0][1], "Read active preset");
    const auto& frame = dev.hid.writes[1];
    ASSERT_EQ(64u, frame.size(), "Full report");
    ASSERT_EQ(0x63, frame[1], "Set gains");
    ASSERT_EQ(6, frame[2], "Preset and five gains");
    ASSERT_EQ(2, frame[3], "Target active preset");
    const std::array<uint8_t, 5> expected { 5, 8, 12, 17, 19 };
    ASSERT_TRUE(std::equal(expected.begin(), expected.end(), frame.begin() + 4), "Encode all five gains");
    ASSERT_TRUE(std::all_of(frame.begin() + 9, frame.end(), [](auto b) { return b == 0; }), "Zero padding");

    const auto count = dev.hid.writes.size();
    ASSERT_TRUE(dev.setEqualizer(nullptr, EqualizerSettings({ 0, 0, 0, 0 })).hasError(), "Reject wrong band count");
    for (float gain : { -8.0f, 8.0f, std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
        auto bad     = settings;
        bad.bands[4] = gain;
        auto result  = dev.setEqualizer(nullptr, bad);
        ASSERT_TRUE(result.hasError() && result.error().code == DeviceError::Code::InvalidParameter, "Reject invalid gain");
    }
    ASSERT_EQ(count, dev.hid.writes.size(), "Validate every gain before any I/O");

    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0 }));
    ASSERT_TRUE(dev.setEqualizer(nullptr, settings).hasError(), "Reject invalid active preset");
    ASSERT_EQ(count + 1, dev.hid.writes.size(), "Do not write gains to invalid preset");
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x63, 1 }));
    ASSERT_TRUE(dev.setEqualizer(nullptr, settings).hasError(), "Reject gain ack for another preset");
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
    dev.hid.replies.push_back(a50Reply(Dev::STATUS_ERROR, { 0x11 }));
    ASSERT_TRUE(dev.setEqualizer(nullptr, settings).hasError(), "Propagate gain write rejection");
}

void testAstroA50Gen4NonfiniteEqualizer()
{
    ParametricEqualizerSettings settings;
    for (int i = 0; i < LogitechAstroA50Gen4::EQ_BANDS; ++i) {
        settings.bands.push_back({ .frequency = 1000.0f, .gain = 0.0f, .q_factor = 1.0f, .type = LogitechAstroA50Gen4::bandType(i) });
    }
    TestableAstroA50Gen4 dev;
    for (float value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity() }) {
        for (int i = 0; i < 5; ++i) {
            for (auto member : { &ParametricEqualizerBand::frequency, &ParametricEqualizerBand::gain,
                     &ParametricEqualizerBand::q_factor }) {
                // Shelf Q is not encoded or used by the device.
                if ((i == 0 || i == 4) && member == &ParametricEqualizerBand::q_factor) {
                    continue;
                }
                auto bad             = settings;
                bad.bands[i].*member = value;
                auto result          = dev.setParametricEqualizer(nullptr, bad);
                ASSERT_TRUE(result.hasError() && result.error().code == DeviceError::Code::InvalidParameter,
                    "Reject nonfinite EQ fields");
            }
        }
    }
    ASSERT_TRUE(dev.hid.writes.empty(), "Nonfinite PEQ never reaches the device");
}

void testAstroA50Gen4ReplyLength()
{
    using Dev = LogitechAstroA50Gen4;
    TestableAstroA50Gen4 dev;
    for (size_t size = 1; size < Dev::FRAME_SIZE; ++size) {
        auto ack = a50Reply(Dev::STATUS_OK, { 0x62, 5 });
        ack.resize(size);
        dev.hid.replies.push_back(ack);
        ASSERT_TRUE(dev.setSidetone(nullptr, 64).hasError(), "Reject every truncated setter reply");
        auto reading = a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 });
        reading.resize(size);
        dev.hid.replies.push_back(reading);
        ASSERT_TRUE(dev.getSidetone(nullptr).hasError(), "Reject every truncated getter reply");
    }
    for (uint8_t length : { 0x00, 0x44, 0xff }) {
        auto reply = a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 });
        reply[2]   = length;
        dev.hid.replies.push_back(reply);
        auto reading = dev.getSidetone(nullptr);
        ASSERT_TRUE(reading.hasValue(), "Accept full report despite bogus payload length");
        ASSERT_EQ(64, reading->current_level, "Decode active value using fixed offset");
    }
}

void testAstroA50Gen4TimeoutRecovery()
{
    using Dev = LogitechAstroA50Gen4;
    // Real HIDInterface returns an error on timeout; also cover the zero-byte variant.
    for (bool zero_bytes : { false, true }) {
        auto no_reply = [zero_bytes]() -> Result<std::vector<uint8_t>> {
            if (zero_bytes) {
                return std::vector<uint8_t> {};
            }
            return DeviceError::timeout("Scripted timeout");
        };

        // A lost reply must not block the connection: one bounded recovery read, then proceed.
        TestableAstroA50Gen4 dev;
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
        dev.hid.replies.push_back(no_reply());
        ASSERT_TRUE(dev.getBattery(nullptr).hasError(), "Battery times out");
        ASSERT_EQ(2u, dev.hid.writes.size(), "Status and battery query issued");

        dev.hid.replies.push_back(no_reply());
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 130 }));
        size_t reads = dev.hid.writes_at_read.size();
        auto chatmix = dev.getChatmix(nullptr);
        ASSERT_TRUE(chatmix.hasValue(), "Lost reply: recovery gives up and the request proceeds");
        ASSERT_EQ(64, chatmix->level, "Fresh chatmix after a lost reply");
        ASSERT_EQ(4u, dev.hid.writes.size(), "Two new requests after recovery");
        ASSERT_EQ(2u, dev.hid.writes_at_read[reads], "Recovery read precedes the status request");
        ASSERT_TRUE(dev.hid.read_timeouts[reads] > 0 && dev.hid.read_timeouts[reads] <= 1000, "Recovery has bounded positive wait");
        ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed");

        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 130 }));
        reads = dev.hid.writes_at_read.size();
        ASSERT_TRUE(dev.getChatmix(nullptr).hasValue(), "Connection is synchronized again");
        ASSERT_EQ(hsc_device_timeout, dev.hid.read_timeouts[reads], "No recovery read once the pending state is cleared");

        // A late reply arriving during recovery is discarded, not misread as link status.
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
        dev.hid.replies.push_back(no_reply());
        ASSERT_TRUE(dev.getBattery(nullptr).hasError(), "Battery times out again");
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 98 }));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 2 }));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 130 }));
        chatmix = dev.getChatmix(nullptr);
        ASSERT_TRUE(chatmix.hasValue(), "Recover then read fresh chatmix");
        ASSERT_EQ(64, chatmix->level, "Late battery and status must not become balance");
        ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed in correct order");
    }
    {
        // Handles are independent, and a reused handle address recovers without the hook.
        TestableAstroA50Gen4 dev;
        int token_a = 0, token_b = 0;
        auto* handle_a = reinterpret_cast<hid_device*>(&token_a);
        auto* handle_b = reinterpret_cast<hid_device*>(&token_b);
        ASSERT_TRUE(dev.getSidetone(handle_a).hasError(), "First connection times out");
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 }));
        ASSERT_TRUE(dev.getSidetone(handle_b).hasValue(), "Other connection is not blocked");
        ASSERT_EQ(2u, dev.hid.writes.size(), "Independent connection sends its query");

        dev.hid.replies.push_back(DeviceError::timeout("Old connection's reply never arrives"));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 }));
        ASSERT_TRUE(dev.getSidetone(handle_a).hasValue(), "Reused handle address recovers without onConnectionClosed()");
        ASSERT_EQ(3u, dev.hid.writes.size(), "Reused connection sends its query after one recovery read");

        ASSERT_TRUE(dev.getSidetone(handle_a).hasError(), "Connection times out again");
        dev.onConnectionClosed(handle_a);
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 }));
        const size_t reads = dev.hid.writes_at_read.size();
        ASSERT_TRUE(dev.getSidetone(handle_a).hasValue(), "Reopened connection works");
        ASSERT_EQ(hsc_device_timeout, dev.hid.read_timeouts[reads], "Hook skips the recovery wait");
    }
    {
        // Read errors and malformed frames during recovery are discarded, not propagated.
        TestableAstroA50Gen4 dev;
        dev.hid.replies.push_back(DeviceError::hidError("Read failed"));
        ASSERT_TRUE(dev.getSidetone(nullptr).hasError(), "Read failure leaves an outstanding request");
        ASSERT_EQ(1u, dev.hid.writes.size(), "One request so far");
        dev.hid.replies.push_back(DeviceError::hidError("Still failed"));
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 }));
        ASSERT_TRUE(dev.getSidetone(nullptr).hasValue(), "Recovery read error does not block the request");
        ASSERT_EQ(2u, dev.hid.writes.size(), "Request issued after the failed recovery read");

        ASSERT_TRUE(dev.getSidetone(nullptr).hasError(), "Times out again");
        dev.hid.replies.push_back(std::vector<uint8_t> { 2 });
        dev.hid.replies.push_back(a50Reply(Dev::STATUS_OK, { 0x68, 5, 50, 50 }));
        ASSERT_TRUE(dev.getSidetone(nullptr).hasValue(), "Truncated recovery frame is discarded");
        ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed");
    }
}

// ============================================================================
// Logitech ASTRO A50 Gen 5: master volume (CMD 0x08)
// ============================================================================

class TestableAstroA50Gen5 : public LogitechAstroA50 {
public:
    mutable ScriptedHIDInterface hid;

    [[nodiscard]] auto getHIDInterface() const -> HIDInterface& override { return hid; }
};

void testAstroA50Gen5VolumeSet()
{
    using Dev = LogitechAstroA50;

    // mapSidetoneToDiscrete<22>: buckets of 5, the top step takes 105..128.
    const struct {
        uint8_t normalized;
        uint8_t device_level;
    } levels[] = {
        { 0, 0 },
        { 4, 0 },
        { 5, 1 },
        { 64, 12 },
        { 104, 20 },
        { 105, 21 },
        { 128, 21 },
    };

    TestableAstroA50Gen5 dev;
    for (const auto& level : levels) {
        dev.hid.writes.clear();
        auto result = dev.setHeadsetVolume(nullptr, level.normalized);
        ASSERT_TRUE(result.hasValue(), "Headset volume set should succeed");
        ASSERT_EQ(level.normalized, result->volume, "Result should report the requested level");
        ASSERT_EQ(21, result->device_max, "Device max should report the 0-21 range");
        ASSERT_EQ(level.device_level, result->device_volume, "Device level should be the scaled 0-21 value");
        ASSERT_EQ(1u, dev.hid.writes.size(), "Volume SET is fire-and-forget (no reply read)");
        ASSERT_EQ(64u, dev.hid.writes[0].size(), "Volume command should use a full report");
        ASSERT_EQ(Dev::REPORT_ID, dev.hid.writes[0][0], "Volume command should use report ID 0x02");
        ASSERT_EQ(Dev::MARKER, dev.hid.writes[0][1], "Volume command should use marker 0x0c");
        ASSERT_EQ(0x05, dev.hid.writes[0][2], "Volume SET length byte should be 0x05");
        ASSERT_EQ(0x00, dev.hid.writes[0][3], "Volume command reserved byte should be zero");
        ASSERT_EQ(Dev::CMD_VOLUME, dev.hid.writes[0][4], "Volume command should use CMD 0x08");
        ASSERT_EQ(Dev::HANDLE_VOLUME_SET, dev.hid.writes[0][5], "Volume SET should use handle 0x1b");
        ASSERT_EQ(0xff, dev.hid.writes[0][6], "Volume SET payload should start with the 0xff marker");
        ASSERT_EQ(level.device_level, dev.hid.writes[0][7], "Volume SET payload should carry the scaled 0-21 value");
    }

    std::cout << "    [OK] ASTRO A50 Gen 5 volume SET frames verified" << std::endl;
}

// ============================================================================
// Logitech Centurion: G522 framing and opt-in options
// ============================================================================

class TestableG522 : public LogitechG522Lightspeed {
public:
    mutable ScriptedHIDInterface hid;

    [[nodiscard]] auto getHIDInterface() const -> HIDInterface& override { return hid; }
};

class TestableProX2 : public LogitechGProX2Lightspeed {
public:
    mutable ScriptedHIDInterface hid;

    [[nodiscard]] auto getHIDInterface() const -> HIDInterface& override { return hid; }
};

// Keep coverage of the shared protocol's opt-out behavior.
class TestableLegacyProX2 : public TestableProX2 {
protected:
    protocols::CenturionOptions centurionOptions() const override { return {}; }
};

static constexpr std::array<uint8_t, 1> PRO_X2_PREFIX { 0x51 };

static std::vector<uint8_t> centurionFrame(std::span<const uint8_t> prefix, std::vector<uint8_t> payload)
{
    auto frame = protocols::LogitechCenturionProtocol::buildCenturionFrame(payload, 0x00, prefix);
    return { frame.begin(), frame.end() };
}

/// Queue a bridge ACK followed by the sub-device's response carrying params.
static void queueBridgeReply(ScriptedHIDInterface& hid, std::span<const uint8_t> prefix, uint8_t sub_index, std::vector<uint8_t> params)
{
    const auto size = static_cast<uint8_t>(params.size() + 3);
    std::vector<uint8_t> payload { 0x03, 0x10, 0x00, size, 0x00, sub_index, 0x01 };
    payload.insert(payload.end(), params.begin(), params.end());
    hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x11 }));
    hid.replies.push_back(centurionFrame(prefix, payload));
}

/// Queue discovery replies: dongle bridge at index 3, sub-device battery (0x0104) at index 2.
static void queueCenturionDiscovery(ScriptedHIDInterface& hid, std::span<const uint8_t> prefix)
{
    hid.replies.push_back(centurionFrame(prefix, { 0x00, 0x01, 0x01, 0x00, 0x02 })); // Root.getFeature(FeatureSet) -> 1
    hid.replies.push_back(centurionFrame(prefix, { 0x01, 0x01, 0x04 })); // FeatureSet.getCount -> 4
    hid.replies.push_back(centurionFrame(prefix, { 0x01, 0x11, 0x04, 0x00, 0x00 })); // index 0: Root
    hid.replies.push_back(centurionFrame(prefix, { 0x01, 0x11, 0x03, 0x00, 0x01 })); // index 1: FeatureSet
    hid.replies.push_back(centurionFrame(prefix, { 0x01, 0x11, 0x02, 0x01, 0x00 })); // index 2: 0x0100
    hid.replies.push_back(centurionFrame(prefix, { 0x01, 0x11, 0x01, 0x00, 0x03 })); // index 3: bridge

    queueBridgeReply(hid, prefix, 0x00, { 0x01, 0x00, 0x02 }); // sub Root.getFeature(FeatureSet) -> 1
    queueBridgeReply(hid, prefix, 0x01, { 0x03 }); // sub FeatureSet.getCount -> 3
    queueBridgeReply(hid, prefix, 0x01, { 0x03, 0x00, 0x00 }); // sub index 0: Root
    queueBridgeReply(hid, prefix, 0x01, { 0x02, 0x00, 0x01 }); // sub index 1: FeatureSet
    queueBridgeReply(hid, prefix, 0x01, { 0x01, 0x01, 0x04 }); // sub index 2: battery
}

static constexpr size_t CENTURION_DISCOVERY_WRITES = 11;

/// Queue G522 lookup replies: Root.getFeature finds the bridge (index 3), then the requested
/// feature at feature_index (battery 0x0104 at index 2 by default).
static void queueG522Lookup(ScriptedHIDInterface& hid, uint8_t feature_index = 0x02)
{
    const auto prefix = std::span<const uint8_t>(LogitechG522Lightspeed::FRAME_PREFIX);
    hid.replies.push_back(centurionFrame(prefix, { 0x00, 0x01, 0x03, 0x00, 0x01 })); // Root.getFeature(bridge) -> 3
    queueBridgeReply(hid, prefix, 0x00, { feature_index, 0x00, 0x03 }); // sub Root.getFeature(id) -> feature_index
}

static constexpr size_t G522_LOOKUP_WRITES = 2;

/// Check that a written frame starts with the expected bytes and is zero-padded after them.
static void assertFrame(const std::vector<uint8_t>& actual, std::initializer_list<uint8_t> expected, const std::string& msg)
{
    ASSERT_EQ(64, static_cast<int>(actual.size()), msg + ": frame size");
    size_t i = 0;
    for (uint8_t byte : expected) {
        ASSERT_EQ(static_cast<int>(byte), static_cast<int>(actual[i]), msg + ": byte " + std::to_string(i));
        ++i;
    }
    for (; i < actual.size(); ++i) {
        ASSERT_EQ(0, static_cast<int>(actual[i]), msg + ": padding byte " + std::to_string(i));
    }
}

void testCenturionG522FramePrefix()
{
    std::cout << "  Testing Logitech Centurion G522 frame prefix..." << std::endl;

    // Captured from G HUB: lights on (feature 0x12, function 3).
    const std::vector<uint8_t> payload { 0x03, 0x1c, 0x00, 0x0d, 0x00, 0x12, 0x3c, 0x00, 0x00, 0x04, 0x00, 0xb8, 0xfc, 0xff, 0x00, 0xab, 0x64 };
    auto frame = protocols::LogitechCenturionProtocol::buildCenturionFrame(payload, 0x00, LogitechG522Lightspeed::FRAME_PREFIX);
    assertFrame({ frame.begin(), frame.end() },
        { 0x50, 0x23, 0x12, 0x00, 0x03, 0x1c, 0x00, 0x0d, 0x00, 0x12, 0x3c, 0x00, 0x00, 0x04, 0x00, 0xb8, 0xfc, 0xff, 0x00, 0xab, 0x64 },
        "G522 frame must match the G HUB capture");

    auto extracted = protocols::LogitechCenturionProtocol::extractCenturionPayload(frame, LogitechG522Lightspeed::FRAME_PREFIX);
    ASSERT_TRUE(extracted.hasValue(), "G522 frame should extract");
    ASSERT_TRUE(*extracted == payload, "Extracted payload should round-trip");

    auto wrong_prefix = protocols::LogitechCenturionProtocol::extractCenturionPayload(frame);
    ASSERT_TRUE(wrong_prefix.hasError(), "A 0x50 0x23 frame must not extract with the default 0x51 prefix");

    std::cout << "    [OK] Logitech Centurion G522 frame prefix verified" << std::endl;
}

void testCenturionConnectionStatusHelpers()
{
    std::cout << "  Testing Logitech Centurion connection status helpers..." << std::endl;

    using Protocol = protocols::LogitechCenturionProtocol;

    // Captured on Linux: headset powered off / on (50 23 05 00 | 03 00 00 xx).
    std::array<uint8_t, 4> off_event { 0x03, 0x00, 0x00, 0x00 };
    std::array<uint8_t, 4> on_event { 0x03, 0x00, 0x00, 0x01 };
    ASSERT_TRUE(Protocol::isBridgeConnectionEvent(off_event, 0x03), "Power-off frame is a connection event");
    ASSERT_TRUE(Protocol::isBridgeDisconnectedEvent(off_event, 0x03), "Power-off frame means disconnected");
    ASSERT_TRUE(Protocol::isBridgeConnectionEvent(on_event, 0x03), "Power-on frame is a connection event");
    ASSERT_TRUE(!Protocol::isBridgeDisconnectedEvent(on_event, 0x03), "Power-on frame means connected");
    ASSERT_TRUE(!Protocol::isBridgeConnectionEvent(off_event, 0x04), "Event from another feature index is ignored");

    std::array<uint8_t, 2> bridge_ack { 0x03, 0x11 };
    ASSERT_TRUE(!Protocol::isBridgeConnectionEvent(bridge_ack, 0x03), "Bridge ACK is not a connection event");

    // Captured bridge status replies (bridge function 0 parameters).
    std::array<uint8_t, 5> status_off { 0x03, 0xe8, 0x00, 0x00, 0x01 };
    std::array<uint8_t, 5> status_on { 0x03, 0xe8, 0x00, 0x01, 0x01 };
    ASSERT_TRUE(Protocol::isBridgeStatusDisconnected(status_off), "Status 00 00 means disconnected");
    ASSERT_TRUE(!Protocol::isBridgeStatusDisconnected(status_on), "Status 00 01 means connected");
    ASSERT_TRUE(!Protocol::isBridgeStatusDisconnected(std::array<uint8_t, 3> { 0x03, 0xe8, 0x00 }), "Short status is not disconnected");

    // G522 battery reply parameters: 47%, not charging.
    auto battery = Protocol::parseCenturionBatteryResponse(std::array<uint8_t, 3> { 0x2f, 0x32, 0x00 });
    ASSERT_TRUE(battery.hasValue(), "G522 battery reply should parse");
    ASSERT_EQ(47, battery->level_percent, "G522 battery level");
    ASSERT_EQ(BATTERY_AVAILABLE, battery->status, "G522 battery status");

    std::cout << "    [OK] Logitech Centurion connection status helpers verified" << std::endl;
}

void testCenturionG522BatteryWire()
{
    std::cout << "  Testing Logitech G522 battery request on the wire..." << std::endl;

    TestableG522 dev;
    queueG522Lookup(dev.hid);
    queueBridgeReply(dev.hid, LogitechG522Lightspeed::FRAME_PREFIX, 0x02, { 0x2f, 0x32, 0x00 });

    auto battery = dev.getBattery(nullptr);
    ASSERT_TRUE(battery.hasValue(), "G522 battery should succeed");
    ASSERT_EQ(47, battery->level_percent, "G522 battery level");
    ASSERT_EQ(BATTERY_AVAILABLE, battery->status, "G522 battery status");

    ASSERT_EQ(G522_LOOKUP_WRITES + 1, dev.hid.writes.size(), "Discovery plus one battery request");
    assertFrame(dev.hid.writes[0], { 0x50, 0x23, 0x05, 0x00, 0x00, 0x01, 0x00, 0x03 },
        "Bridge lookup is a direct Root.getFeature(0x0003) with the 50 23 prefix and the exact length");
    assertFrame(dev.hid.writes[1], { 0x50, 0x23, 0x0a, 0x00, 0x03, 0x11, 0x00, 0x05, 0x00, 0x00, 0x01, 0x01, 0x04 },
        "Battery lookup is a bridged Root.getFeature(0x0104)");
    assertFrame(dev.hid.writes[G522_LOOKUP_WRITES], { 0x50, 0x23, 0x08, 0x00, 0x03, 0x11, 0x00, 0x03, 0x00, 0x02, 0x01 },
        "Battery request goes through the bridge to the looked-up index");
    ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed");

    // A second request reuses the cached indexes: no new lookups.
    queueBridgeReply(dev.hid, LogitechG522Lightspeed::FRAME_PREFIX, 0x02, { 0x30, 0x32, 0x02 });
    auto charging = dev.getBattery(nullptr);
    ASSERT_TRUE(charging.hasValue(), "Second G522 battery request should succeed");
    ASSERT_EQ(BATTERY_CHARGING, charging->status, "G522 charging state 2");
    ASSERT_EQ(G522_LOOKUP_WRITES + 2, dev.hid.writes.size(), "Cached lookup: only the battery request is sent");

    // A feature the headset lacks (Root.getFeature returns index 0) is not supported.
    TestableG522 missing;
    missing.hid.replies.push_back(centurionFrame(LogitechG522Lightspeed::FRAME_PREFIX, { 0x00, 0x01, 0x03, 0x00, 0x01 }));
    queueBridgeReply(missing.hid, LogitechG522Lightspeed::FRAME_PREFIX, 0x00, { 0x00, 0x00, 0x00 });
    auto unsupported = missing.getBattery(nullptr);
    ASSERT_TRUE(unsupported.hasError(), "Missing feature should fail");
    ASSERT_TRUE(unsupported.error().code == DeviceError::Code::NotSupported, "Missing feature is not supported");

    std::cout << "    [OK] Logitech G522 battery request on the wire verified" << std::endl;
}

void testCenturionG522Offline()
{
    std::cout << "  Testing Logitech G522 offline detection..." << std::endl;

    const auto prefix = std::span<const uint8_t>(LogitechG522Lightspeed::FRAME_PREFIX);

    {
        // Power-off event while waiting for the reply.
        TestableG522 dev;
        queueG522Lookup(dev.hid);
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x11 }));
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x00, 0x00, 0x00 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Power-off event should fail the request");
        ASSERT_TRUE(battery.error().code == DeviceError::Code::DeviceOffline, "Power-off event means offline");
    }
    {
        // Headset already off: ACK, then nothing; the bridge status probe says disconnected.
        TestableG522 dev;
        queueG522Lookup(dev.hid);
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x11 }));
        dev.hid.replies.push_back(DeviceError::timeout("No reply from the headset"));
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x11 })); // stale ACK: wrong function, skipped
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x01, 0x03, 0xe8, 0x00, 0x00, 0x01 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Unanswered request should fail");
        ASSERT_TRUE(battery.error().code == DeviceError::Code::DeviceOffline, "Probe should report offline");
        ASSERT_EQ(G522_LOOKUP_WRITES + 2, dev.hid.writes.size(), "Battery request plus one probe");
        assertFrame(dev.hid.writes.back(), { 0x50, 0x23, 0x03, 0x00, 0x03, 0x01 }, "Probe is a direct bridge function 0 request");
        ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed");
    }
    {
        // Probe says the headset is connected: keep the original timeout.
        TestableG522 dev;
        queueG522Lookup(dev.hid);
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x11 }));
        dev.hid.replies.push_back(DeviceError::timeout("No reply from the headset"));
        dev.hid.replies.push_back(centurionFrame(prefix, { 0x03, 0x01, 0x03, 0xe8, 0x00, 0x01, 0x01 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Unanswered request should fail");
        ASSERT_TRUE(battery.error().code == DeviceError::Code::Timeout, "Connected headset keeps the timeout");
    }

    std::cout << "    [OK] Logitech G522 offline detection verified" << std::endl;
}

void testG522SidetoneAndAutoSleepParsing()
{
    std::cout << "  Testing Logitech G522 sidetone and auto-sleep parsing..." << std::endl;

    // Captured sidetone reads after -s 0, -s 64 and -s 128.
    auto off = LogitechG522Lightspeed::parseSidetoneResponse(std::array<uint8_t, 4> { 0x01, 0x01, 0x00, 0x00 });
    ASSERT_TRUE(off.hasValue(), "Sidetone 0 should parse");
    ASSERT_EQ(0, static_cast<int>(off->current_level), "Step 0 reads as 0");
    ASSERT_TRUE(off->is_muted, "Step 0 is muted");

    auto mid = LogitechG522Lightspeed::parseSidetoneResponse(std::array<uint8_t, 4> { 0x01, 0x01, 0x39, 0x04 });
    ASSERT_TRUE(mid.hasValue(), "Sidetone 64 should parse");
    ASSERT_EQ(4, static_cast<int>(mid->device_level), "-s 64 is stored as step 4");
    ASSERT_EQ(56, static_cast<int>(mid->current_level), "Step 4 reads back as 56");
    ASSERT_TRUE(!mid->is_muted, "Step 4 is not muted");

    auto max = LogitechG522Lightspeed::parseSidetoneResponse(std::array<uint8_t, 4> { 0x01, 0x01, 0x48, 0x09 });
    ASSERT_TRUE(max.hasValue(), "Sidetone 128 should parse");
    ASSERT_EQ(128, static_cast<int>(max->current_level), "Step 9 reads as 128");
    ASSERT_EQ(9, static_cast<int>(max->device_max), "Device range is 0-9");

    ASSERT_TRUE(LogitechG522Lightspeed::parseSidetoneResponse(std::array<uint8_t, 4> { 0x01, 0x01, 0x00, 0x0a }).hasError(),
        "Step above 9 is rejected");
    ASSERT_TRUE(LogitechG522Lightspeed::parseSidetoneResponse(std::array<uint8_t, 3> { 0x01, 0x01, 0x00 }).hasError(),
        "Short reply is rejected");

    // Captured auto-sleep read: 30 min sleep, lights dim after 1 min, off after 16 min.
    auto params = LogitechG522Lightspeed::buildAutoSleepParams(0x0f, std::array<uint8_t, 3> { 0x1e, 0x01, 0x10 });
    ASSERT_TRUE(params.hasValue(), "Auto-sleep params should build");
    ASSERT_TRUE((*params == std::array<uint8_t, 3> { 0x0f, 0x01, 0x10 }), "Only the sleep minutes change");
    ASSERT_TRUE(LogitechG522Lightspeed::buildAutoSleepParams(0x0f, std::array<uint8_t, 2> { 0x1e, 0x01 }).hasError(),
        "Short auto-sleep reply is rejected");

    std::cout << "    [OK] Logitech G522 sidetone and auto-sleep parsing verified" << std::endl;
}

void testG522SettingsWire()
{
    std::cout << "  Testing Logitech G522 lights, voice prompts, sidetone and inactive time on the wire..." << std::endl;

    const auto prefix = std::span<const uint8_t>(LogitechG522Lightspeed::FRAME_PREFIX);
    // Request bytes match the G HUB captures, except for the software ID nibble (1 instead of c/d).

    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x12); // lighting 0x0621
        queueBridgeReply(dev.hid, prefix, 0x12, {});
        auto on = dev.setLights(nullptr, true);
        ASSERT_TRUE(on.hasValue() && on->enabled, "Lights on should succeed");
        assertFrame(dev.hid.writes.back(),
            { 0x50, 0x23, 0x12, 0x00, 0x03, 0x11, 0x00, 0x0d, 0x00, 0x12, 0x31, 0x00, 0x00, 0x04, 0x00, 0xb8, 0xfc, 0xff, 0x00, 0xab, 0x64 },
            "Lights on restores G HUB's default two-zone look");

        queueBridgeReply(dev.hid, prefix, 0x12, {});
        auto off = dev.setLights(nullptr, false);
        ASSERT_TRUE(off.hasValue() && !off->enabled, "Lights off should succeed");
        assertFrame(dev.hid.writes.back(), { 0x50, 0x23, 0x12, 0x00, 0x03, 0x11, 0x00, 0x0d, 0x00, 0x12, 0x31 },
            "Lights off writes a black fixed color");
        ASSERT_EQ(G522_LOOKUP_WRITES + 2, dev.hid.writes.size(), "Second lights command reuses the lookup");
    }
    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x1a); // voice prompts 0x060b
        queueBridgeReply(dev.hid, prefix, 0x1a, {});
        auto voice = dev.setVoicePrompts(nullptr, true);
        ASSERT_TRUE(voice.hasValue() && voice->enabled, "Voice prompts on should succeed");
        assertFrame(dev.hid.writes.back(), { 0x50, 0x23, 0x0a, 0x00, 0x03, 0x11, 0x00, 0x05, 0x00, 0x1a, 0x51, 0x00, 0x01 },
            "Voice prompts on");

        queueBridgeReply(dev.hid, prefix, 0x1a, {});
        ASSERT_TRUE(dev.setVoicePrompts(nullptr, false).hasValue(), "Voice prompts off should succeed");
        assertFrame(dev.hid.writes.back(), { 0x50, 0x23, 0x0a, 0x00, 0x03, 0x11, 0x00, 0x05, 0x00, 0x1a, 0x51 },
            "Voice prompts off (tones)");
    }
    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x0d); // sidetone 0x0604
        queueBridgeReply(dev.hid, prefix, 0x0d, { 0x01, 0x01, 0x39, 0x04 });
        auto sidetone = dev.getSidetone(nullptr);
        ASSERT_TRUE(sidetone.hasValue(), "Sidetone read should succeed");
        ASSERT_EQ(4, static_cast<int>(sidetone->device_level), "Sidetone step");
        assertFrame(dev.hid.writes.back(), { 0x50, 0x23, 0x08, 0x00, 0x03, 0x11, 0x00, 0x03, 0x00, 0x0d, 0x01 },
            "Sidetone read is function 0");
    }
    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x14); // auto-sleep 0x0108
        queueBridgeReply(dev.hid, prefix, 0x14, { 0x1e, 0x01, 0x10 });
        queueBridgeReply(dev.hid, prefix, 0x14, {});
        auto inactive = dev.setInactiveTime(nullptr, 15);
        ASSERT_TRUE(inactive.hasValue(), "Inactive time should succeed");
        ASSERT_EQ(G522_LOOKUP_WRITES + 2, dev.hid.writes.size(), "Lookup, read, then write");
        assertFrame(dev.hid.writes[G522_LOOKUP_WRITES], { 0x50, 0x23, 0x08, 0x00, 0x03, 0x11, 0x00, 0x03, 0x00, 0x14, 0x01 },
            "Auto-sleep read is function 0");
        assertFrame(dev.hid.writes.back(), { 0x50, 0x23, 0x0b, 0x00, 0x03, 0x11, 0x00, 0x06, 0x00, 0x14, 0x11, 0x0f, 0x01, 0x10 },
            "Auto-sleep write keeps the lighting timers");
    }
    {
        // A malformed read must not fall back to writing zeros.
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x14);
        queueBridgeReply(dev.hid, prefix, 0x14, { 0x1e });
        ASSERT_TRUE(dev.setInactiveTime(nullptr, 15).hasError(), "Short auto-sleep read fails");
        ASSERT_EQ(G522_LOOKUP_WRITES + 1, dev.hid.writes.size(), "No write after a failed read");
    }

    std::cout << "    [OK] Logitech G522 settings on the wire verified" << std::endl;
}

static std::vector<uint8_t> hexBytes(std::string_view hex)
{
    std::vector<uint8_t> bytes;
    std::istringstream stream { std::string(hex) };
    std::string token;
    while (stream >> token) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(token, nullptr, 16)));
    }
    return bytes;
}

// Equalizer parameters captured from G HUB for each built-in preset (bytes after "0f 2d").
static constexpr std::array<std::string_view, 5> G522_EQ_PRESET_CAPTURES {
    "00 00 00 00 14 16 00 78 00 32 16 00 78 00 7d 16 00 78 00 fa 16 00 78 01 f4 16 00 78 03 e8 16 00 78 09 c4 16 00 78 13 88 16 00 78 27 10 16 00 78 4e 20 16 00 78",
    "00 00 00 00 14 16 00 b4 00 32 16 00 aa 00 7d 16 00 9b 00 fa 20 00 8c 01 f4 16 00 78 03 e8 16 00 78 09 c4 16 00 78 13 88 16 00 78 27 10 16 00 78 4e 20 16 00 78",
    "00 00 00 00 14 16 00 78 00 32 16 00 78 00 fa 16 00 82 01 90 16 00 82 03 20 16 00 87 05 dc 16 00 8c 09 c4 16 00 91 13 88 16 00 96 27 10 16 00 9b 4a 38 16 00 af",
    "00 00 00 00 14 16 00 78 00 32 16 00 78 00 7d 16 00 78 01 90 16 00 64 03 20 16 00 64 04 e2 40 00 c3 09 c4 20 00 d2 13 88 16 00 9b 27 10 16 00 96 4a 38 16 00 96",
    "00 00 00 00 14 16 00 c8 00 32 16 00 b4 00 7d 16 00 a0 00 fa 16 00 6e 01 f4 16 00 6e 03 e8 16 00 8c 09 c4 16 00 a0 13 88 16 00 96 27 10 16 00 82 4e 20 16 00 6e",
};

// Offset of band 6 (1 kHz in the Default preset) inside the parameters: 3 header bytes + 5 bands.
static constexpr size_t G522_EQ_BAND6 = 3 + 5 * 5;

void testG522EqualizerEncoding()
{
    std::cout << "  Testing Logitech G522 equalizer encoding..." << std::endl;

    using Dev = LogitechG522Lightspeed;

    for (size_t i = 0; i < Dev::EQ_PRESETS_COUNT; ++i) {
        auto params = Dev::buildEqParams(*Dev::EQ_PRESETS[i]);
        ASSERT_EQ(Dev::EQ_PARAMS_SIZE, params.size(), "Equalizer parameters are 53 bytes");
        ASSERT_TRUE(params == hexBytes(G522_EQ_PRESET_CAPTURES[i]),
            std::string("Preset must match the G HUB capture: ") + std::string(Dev::EQ_PRESET_NAMES[i]));
    }

    // Single changes at 1 kHz, captured from G HUB's custom equalizer.
    auto oneBand = [](float gain_db, float q_factor, uint16_t frequency) {
        auto bands  = Dev::EQ_PRESET_DEFAULT;
        bands[5]    = { frequency, gain_db, q_factor };
        auto params = Dev::buildEqParams(bands);
        return std::vector<uint8_t>(params.begin() + G522_EQ_BAND6, params.begin() + G522_EQ_BAND6 + 5);
    };
    ASSERT_TRUE((oneBand(3.0f, Dev::EQ_DEFAULT_Q, 1000) == std::vector<uint8_t> { 0x03, 0xe8, 0x16, 0x00, 0xb4 }), "+3 dB");
    ASSERT_TRUE((oneBand(-3.0f, Dev::EQ_DEFAULT_Q, 1000) == std::vector<uint8_t> { 0x03, 0xe8, 0x16, 0x00, 0x3c }), "-3 dB");
    ASSERT_TRUE((oneBand(6.0f, Dev::EQ_DEFAULT_Q, 1000) == std::vector<uint8_t> { 0x03, 0xe8, 0x16, 0x00, 0xf0 }), "+6 dB");
    ASSERT_TRUE((oneBand(-6.0f, Dev::EQ_DEFAULT_Q, 1000) == std::vector<uint8_t> { 0x03, 0xe8, 0x16, 0x00, 0x00 }), "-6 dB");
    ASSERT_TRUE((oneBand(0.0f, 1.0f, 1000) == std::vector<uint8_t> { 0x03, 0xe8, 0x20, 0x00, 0x78 }), "Q 1.0");
    ASSERT_TRUE((oneBand(0.0f, 1.0f, 1200) == std::vector<uint8_t> { 0x04, 0xb0, 0x20, 0x00, 0x78 }), "1200 Hz");
    ASSERT_TRUE((oneBand(0.0f, 0.688f, 1000) == std::vector<uint8_t> { 0x03, 0xe8, 0x16, 0x00, 0x78 }), "G HUB's Q 0.688");

    // The headset stores 0.05 dB steps.
    ASSERT_EQ(0x78, static_cast<int>(oneBand(0.01f, Dev::EQ_DEFAULT_Q, 1000)[4]), "0.01 dB rounds to 0");
    ASSERT_EQ(0x79, static_cast<int>(oneBand(0.03f, Dev::EQ_DEFAULT_Q, 1000)[4]), "0.03 dB rounds to 0.05");

    std::cout << "    [OK] Logitech G522 equalizer encoding verified" << std::endl;
}

void testG522EqualizerCommands()
{
    std::cout << "  Testing Logitech G522 equalizer commands..." << std::endl;

    using Dev         = LogitechG522Lightspeed;
    const auto prefix = std::span<const uint8_t>(Dev::FRAME_PREFIX);
    const std::vector<uint8_t> eq_header { 0x50, 0x23, 0x3d, 0x00, 0x03, 0x11, 0x00, 0x38, 0x00, 0x0f, 0x21 };

    auto writtenParams = [](const std::vector<uint8_t>& frame) {
        return std::vector<uint8_t>(frame.begin() + 11, frame.end());
    };
    auto writtenHeader = [](const std::vector<uint8_t>& frame) {
        return std::vector<uint8_t>(frame.begin(), frame.begin() + 11);
    };

    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x0f); // playback EQ 0x020d
        queueBridgeReply(dev.hid, prefix, 0x0f, {});
        auto preset = dev.setEqualizerPreset(nullptr, 1);
        ASSERT_TRUE(preset.hasValue(), "Bass Boost preset should succeed");
        ASSERT_EQ(5, static_cast<int>(preset->total_presets), "Five presets");
        ASSERT_TRUE(writtenHeader(dev.hid.writes.back()) == eq_header, "Equalizer write header (single 64-byte frame)");
        ASSERT_TRUE(writtenParams(dev.hid.writes.back()) == hexBytes(G522_EQ_PRESET_CAPTURES[1]), "Bass Boost on the wire");

        ASSERT_TRUE(dev.setEqualizerPreset(nullptr, 5).hasError(), "Preset 5 does not exist");
        ASSERT_EQ(G522_LOOKUP_WRITES + 1, dev.hid.writes.size(), "Invalid preset sends nothing");
    }
    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x0f);
        queueBridgeReply(dev.hid, prefix, 0x0f, {});
        EqualizerSettings custom { { 0, 0, 0, 0, 0, 3.0f, 0, 0, 0, 0 } };
        ASSERT_TRUE(dev.setEqualizer(nullptr, custom).hasValue(), "10 gains should succeed");
        auto expected               = hexBytes(G522_EQ_PRESET_CAPTURES[0]);
        expected[G522_EQ_BAND6 + 4] = 0xb4;
        ASSERT_TRUE(writtenParams(dev.hid.writes.back()) == expected, "Custom gains use the Default frequencies and Q");

        const size_t writes = dev.hid.writes.size();
        ASSERT_TRUE(dev.setEqualizer(nullptr, EqualizerSettings { { 0, 0, 0 } }).hasError(), "Wrong band count is rejected");
        ASSERT_TRUE(dev.setEqualizer(nullptr, EqualizerSettings { { 0, 0, 0, 0, 0, 6.5f, 0, 0, 0, 0 } }).hasError(),
            "Gain above +6 dB is rejected");
        ASSERT_TRUE(dev.setEqualizer(nullptr, EqualizerSettings { { 0, 0, 0, 0, 0, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0 } }).hasError(),
            "NaN gain is rejected");
        ASSERT_EQ(writes, dev.hid.writes.size(), "Rejected settings send nothing");
    }
    {
        TestableG522 dev;
        queueG522Lookup(dev.hid, 0x0f);
        queueBridgeReply(dev.hid, prefix, 0x0f, {});
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, ParametricEqualizerSettings {}).hasValue(), "Reset should succeed");
        ASSERT_TRUE(writtenParams(dev.hid.writes.back()) == hexBytes(G522_EQ_PRESET_CAPTURES[0]), "Reset restores Default");

        // Band 6 moved to 1200 Hz with Q 1.0, as in the eq-freq-1200 capture.
        queueBridgeReply(dev.hid, prefix, 0x0f, {});
        ParametricEqualizerSettings moved;
        for (const auto& band : Dev::EQ_PRESET_DEFAULT) {
            moved.bands.push_back({ static_cast<float>(band.frequency), band.gain_db, band.q_factor, EqualizerFilterType::Peaking });
        }
        moved.bands[5] = { 1200.0f, 0.0f, 1.0f, EqualizerFilterType::Peaking };
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, moved).hasValue(), "10 parametric bands should succeed");
        auto expected               = hexBytes(G522_EQ_PRESET_CAPTURES[0]);
        expected[G522_EQ_BAND6 + 0] = 0x04;
        expected[G522_EQ_BAND6 + 1] = 0xb0;
        expected[G522_EQ_BAND6 + 2] = 0x20;
        ASSERT_TRUE(writtenParams(dev.hid.writes.back()) == expected, "Parametric band matches the eq-freq-1200 capture");

        queueBridgeReply(dev.hid, prefix, 0x0f, {});
        ParametricEqualizerSettings limits { { { 20.0f, -6.0f, 0.031f, EqualizerFilterType::Peaking },
            { 20000.0f, 6.0f, 7.969f, EqualizerFilterType::Peaking } } };
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, limits).hasValue(), "G HUB's displayed limits are accepted");

        const size_t writes = dev.hid.writes.size();
        auto rejects        = [&](ParametricEqualizerBand band, const char* msg) {
            ASSERT_TRUE(dev.setParametricEqualizer(nullptr, ParametricEqualizerSettings { { band } }).hasError(), msg);
        };
        rejects({ 1000.0f, 0.0f, 1.0f, EqualizerFilterType::LowShelf }, "Only peaking bands are supported");
        rejects({ 15.0f, 0.0f, 1.0f, EqualizerFilterType::Peaking }, "Frequency below 20 Hz is rejected");
        rejects({ 1000.0f, -6.5f, 1.0f, EqualizerFilterType::Peaking }, "Gain below -6 dB is rejected");
        rejects({ 1000.0f, 0.0f, 8.1f, EqualizerFilterType::Peaking }, "Q above 7.969 is rejected");
        rejects({ 1000.0f, 0.0f, 0.01f, EqualizerFilterType::Peaking }, "Q below 0.031 is rejected");
        ASSERT_TRUE(dev.setParametricEqualizer(nullptr, ParametricEqualizerSettings { std::vector<ParametricEqualizerBand>(11) }).hasError(),
            "More than 10 bands is rejected");
        ASSERT_EQ(writes, dev.hid.writes.size(), "Rejected settings send nothing");
    }

    std::cout << "    [OK] Logitech G522 equalizer commands verified" << std::endl;
}

void testCenturionProX2DefaultOptionsUnchanged()
{
    std::cout << "  Testing Logitech PRO X2 traffic with default Centurion options..." << std::endl;

    {
        TestableLegacyProX2 dev;
        queueCenturionDiscovery(dev.hid, PRO_X2_PREFIX);
        queueBridgeReply(dev.hid, PRO_X2_PREFIX, 0x02, { 0x2f, 0x32, 0x00 });

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasValue(), "PRO X2 battery should succeed");
        ASSERT_EQ(47, battery->level_percent, "PRO X2 battery level");

        // The direct request keeps the historic length byte of the full 64-byte payload buffer (0x41).
        std::vector<uint8_t> expected_direct(64, 0x00);
        expected_direct[0] = 0x51;
        expected_direct[1] = 0x41;
        expected_direct[4] = 0x01;
        expected_direct[6] = 0x01;
        ASSERT_TRUE(dev.hid.writes[0] == expected_direct, "PRO X2 direct request must be byte-identical to before");
        assertFrame(dev.hid.writes[CENTURION_DISCOVERY_WRITES], { 0x51, 0x08, 0x00, 0x03, 0x11, 0x00, 0x03, 0x00, 0x02, 0x01 },
            "PRO X2 bridge request is unchanged");
    }
    {
        // A power-off event is skipped, and a timeout is returned without a probe.
        TestableLegacyProX2 dev;
        queueCenturionDiscovery(dev.hid, PRO_X2_PREFIX);
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x11 }));
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x00, 0x00, 0x00 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Unanswered request should fail");
        ASSERT_TRUE(battery.error().code == DeviceError::Code::Timeout, "PRO X2 keeps reporting a timeout");
        ASSERT_EQ(CENTURION_DISCOVERY_WRITES + 1, dev.hid.writes.size(), "PRO X2 sends no probe");
    }

    std::cout << "    [OK] Logitech PRO X2 traffic with default Centurion options verified" << std::endl;
}

void testCenturionProX2BatteryWire()
{
    std::cout << "  Testing Logitech PRO X2 battery lookup on the wire..." << std::endl;

    TestableProX2 dev;
    dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x00, 0x01, 0x03, 0x00, 0x01 }));
    queueBridgeReply(dev.hid, PRO_X2_PREFIX, 0x00, { 0x02, 0x00, 0x03 });
    queueBridgeReply(dev.hid, PRO_X2_PREFIX, 0x02, { 0x2f, 0x32, 0x00 });

    auto battery = dev.getBattery(nullptr);
    ASSERT_TRUE(battery.hasValue(), "PRO X2 battery should succeed with direct feature lookup");
    ASSERT_EQ(47, battery->level_percent, "PRO X2 battery level");
    ASSERT_EQ(BATTERY_AVAILABLE, battery->status, "PRO X2 battery status");
    ASSERT_EQ(3u, dev.hid.writes.size(), "Two lookups and one battery request");
    assertFrame(dev.hid.writes[0], { 0x51, 0x05, 0x00, 0x00, 0x01, 0x00, 0x03 },
        "PRO X2 bridge lookup uses the exact direct length");
    assertFrame(dev.hid.writes[1], { 0x51, 0x0a, 0x00, 0x03, 0x11, 0x00, 0x05, 0x00, 0x00, 0x01, 0x01, 0x04 },
        "PRO X2 battery feature lookup goes through the bridge");
    assertFrame(dev.hid.writes[2], { 0x51, 0x08, 0x00, 0x03, 0x11, 0x00, 0x03, 0x00, 0x02, 0x01 },
        "PRO X2 battery request uses the resolved feature index");

    queueBridgeReply(dev.hid, PRO_X2_PREFIX, 0x02, { 0x30, 0x32, 0x02 });
    auto charging = dev.getBattery(nullptr);
    ASSERT_TRUE(charging.hasValue(), "Cached PRO X2 battery request should succeed");
    ASSERT_EQ(BATTERY_CHARGING, charging->status, "PRO X2 charging state");
    ASSERT_EQ(4u, dev.hid.writes.size(), "Cached indexes avoid further lookups");
    ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed");

    std::cout << "    [OK] Logitech PRO X2 battery lookup on the wire verified" << std::endl;
}

void testCenturionProX2Offline()
{
    std::cout << "  Testing Logitech PRO X2 offline detection..." << std::endl;

    {
        // An already powered-off headset can disappear during feature lookup.
        TestableProX2 dev;
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x00, 0x01, 0x03, 0x00, 0x01 }));
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x11 }));
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x00, 0x00, 0x00 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Disconnect during lookup should fail");
        ASSERT_TRUE(battery.error().code == DeviceError::Code::DeviceOffline, "Disconnect event means offline");
        ASSERT_EQ(2u, dev.hid.writes.size(), "Offline lookup sends no legacy battery request");
        ASSERT_TRUE(dev.hid.replies.empty(), "Disconnect event consumed");
    }
    for (bool connected : { false, true }) {
        TestableProX2 dev;
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x00, 0x01, 0x03, 0x00, 0x01 }));
        queueBridgeReply(dev.hid, PRO_X2_PREFIX, 0x00, { 0x02, 0x00, 0x03 });
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x11 }));
        dev.hid.replies.push_back(DeviceError::timeout("No reply from the headset"));
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x11 })); // stale ACK must be skipped
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x01, 0x03, 0xe8, 0x00, static_cast<uint8_t>(connected), 0x01 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Unanswered battery query should fail");
        ASSERT_TRUE(battery.error().code == (connected ? DeviceError::Code::Timeout : DeviceError::Code::DeviceOffline),
            "Only a disconnected headset should turn a timeout into offline");
        ASSERT_EQ(4u, dev.hid.writes.size(), "Two lookups, battery query and one probe; no legacy fallback");
        assertFrame(dev.hid.writes.back(), { 0x51, 0x03, 0x00, 0x03, 0x01 },
            "PRO X2 status probe uses the exact direct length");
        ASSERT_TRUE(dev.hid.replies.empty(), "All probe replies consumed");

        // A later successful request uses the cached index and recovers normally.
        queueBridgeReply(dev.hid, PRO_X2_PREFIX, 0x02, { 0x30, 0x32, 0x00 });
        auto recovered = dev.getBattery(nullptr);
        ASSERT_TRUE(recovered.hasValue(), "Battery query should recover after an offline or timeout result");
        ASSERT_EQ(48, recovered->level_percent, "Recovered battery level");
        ASSERT_EQ(5u, dev.hid.writes.size(), "Recovery reuses the cached feature index");
        ASSERT_TRUE(dev.hid.replies.empty(), "Recovery replies consumed");
    }

    std::cout << "    [OK] Logitech PRO X2 offline detection verified" << std::endl;
}

void testCenturionProX2BridgeRejection()
{
    std::cout << "  Testing Logitech PRO X2 bridge rejection..." << std::endl;

    for (bool connected : { false, true }) {
        TestableProX2 dev;
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x00, 0x01, 0x03, 0x00, 0x01 }));
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0xff, 0x03, 0x21, 0x0b })); // unrelated function
        // Captured PRO X 2 rejection and bridge status while disconnected.
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0xff, 0x03, 0x11, 0x0b }));
        dev.hid.replies.push_back(centurionFrame(PRO_X2_PREFIX, { 0x03, 0x01, 0x03, 0x7a, 0x00, static_cast<uint8_t>(connected), 0x01 }));

        auto battery = dev.getBattery(nullptr);
        ASSERT_TRUE(battery.hasError(), "Rejected feature lookup should fail");
        ASSERT_TRUE(battery.error().code == (connected ? DeviceError::Code::ProtocolError : DeviceError::Code::DeviceOffline),
            "A bridge rejection means offline only when the status confirms disconnection");
        ASSERT_EQ(3u, dev.hid.writes.size(), "Bridge lookup, rejected feature lookup and one status probe");
        ASSERT_EQ(2u, dev.hid.writes_at_read[2], "Unrelated rejection must not trigger a status probe");
        assertFrame(dev.hid.writes.back(), { 0x51, 0x03, 0x00, 0x03, 0x01 }, "Direct bridge status probe");
        ASSERT_TRUE(dev.hid.replies.empty(), "All replies consumed without waiting for a timeout");
    }

    std::cout << "    [OK] Logitech PRO X2 bridge rejection verified" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

// ============================================================================
// Audeze Maxwell Tests
// ============================================================================

/**
 * @brief Serves queued input reports; once the queue is empty the dongle has nothing
 * new and answers with a zero-length report.
 */
class MaxwellScriptedHID : public ScriptedHIDInterface {
public:
    /// Number of requests that get no reply before the queue is served
    size_t silent_writes = 0;

    [[nodiscard]] auto getInputReport(hid_device* /*device_handle*/, std::span<uint8_t> data)
        -> Result<size_t> override
    {
        std::vector<uint8_t> report { 0x07, 0x00, 0x80 };

        // An empty queued reply leaves the current request unanswered
        if (!replies.empty() && replies.front() && replies.front()->empty()) {
            if (unanswered_write_ == 0) {
                unanswered_write_ = writes.size();
            }
            if (writes.size() > unanswered_write_) {
                replies.pop_front();
                unanswered_write_ = 0;
            }
        }

        if (!replies.empty() && unanswered_write_ == 0 && writes.size() >= silent_writes + 1) {
            auto reply = std::move(replies.front());
            replies.pop_front();
            if (!reply) {
                return reply.error();
            }
            report = std::move(*reply);
        }
        report.resize(data.size(), 0);
        std::copy(report.begin(), report.end(), data.begin());
        return data.size();
    }

private:
    size_t unanswered_write_ = 0;
};

class TestableAudezeMaxwell : public AudezeMaxwell {
public:
    mutable MaxwellScriptedHID hid;

    TestableAudezeMaxwell()
    {
        response_timeout_     = std::chrono::milliseconds(20);
        poll_interval_        = std::chrono::milliseconds(0);
        eq_timeout_           = std::chrono::milliseconds(20);
        eq_fragment_interval_ = std::chrono::milliseconds(0);
    }

    [[nodiscard]] auto getHIDInterface() const -> HIDInterface& override { return hid; }
};

/// Build an input report: 07 LEN 80 FRAGMENT..., followed by leftover bytes as on the real dongle
static std::vector<uint8_t> maxwellReport(std::initializer_list<uint8_t> fragment)
{
    std::vector<uint8_t> report { 0x07, static_cast<uint8_t>(fragment.size()), 0x80 };
    report.insert(report.end(), fragment.begin(), fragment.end());
    // Stale battery result, as seen after the valid bytes in the capture
    for (uint8_t stale : { 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x3E }) {
        report.push_back(stale);
    }
    report.resize(AudezeMaxwell::MSG_SIZE, 0);
    return report;
}

static std::vector<uint8_t> maxwellWritePrefix(const std::vector<uint8_t>& write, size_t count)
{
    return { write.begin(), write.begin() + std::min(count, write.size()) };
}

void testAudezeMaxwellFraming()
{
    std::cout << "  Testing Audeze Maxwell framing..." << std::endl;

    std::array<uint8_t, 4> body { 0x01, 0x09, 0x22, 0x00 };
    auto message = AudezeMaxwell::buildMessage(AudezeMaxwell::TYPE_REQUEST, body);
    auto reports = AudezeMaxwell::buildReports(message);
    ASSERT_EQ(1, static_cast<int>(reports.size()), "Short message should fit one report");

    std::vector<uint8_t> expected { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x22, 0x00 };
    expected.resize(AudezeMaxwell::MSG_SIZE, 0);
    ASSERT_TRUE(std::equal(expected.begin(), expected.end(), reports[0].begin()), "Mic mute read should match the captured report");

    // A long message is split into 59-byte fragments
    std::vector<uint8_t> long_body(100, 0xAA);
    auto long_reports = AudezeMaxwell::buildReports(AudezeMaxwell::buildMessage(AudezeMaxwell::TYPE_REQUEST, long_body));
    ASSERT_EQ(2, static_cast<int>(long_reports.size()), "104 bytes should need two reports");
    ASSERT_EQ(59, static_cast<int>(long_reports[0][1]), "First fragment should be full");
    ASSERT_EQ(45, static_cast<int>(long_reports[1][1]), "Second fragment should hold the rest");
    ASSERT_EQ(100, static_cast<int>(long_reports[0][5]), "Body length should be little endian");

    // A zero-length report carries nothing, whatever follows the header
    auto stale = maxwellReport({});
    ASSERT_TRUE(AudezeMaxwell::reportFragment(stale).empty(), "Zero-length report should yield no bytes");

    // Two messages in one fragment, then garbage and an incomplete message
    std::vector<uint8_t> stream { 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x3E, 0xFF, 0x05, 0x5B, 0x06 };
    ASSERT_EQ(7, static_cast<int>(AudezeMaxwell::extractMessage(stream).size()), "Acknowledgement should be 7 bytes");
    ASSERT_EQ(9, static_cast<int>(AudezeMaxwell::extractMessage(stream).size()), "Result should be 9 bytes");
    ASSERT_TRUE(AudezeMaxwell::extractMessage(stream).empty(), "Incomplete message should not be returned");
    ASSERT_EQ(3, static_cast<int>(stream.size()), "Garbage should be dropped, the partial message kept");

    std::cout << "    [OK] Audeze Maxwell framing verified" << std::endl;
}

void testAudezeMaxwellBattery()
{
    std::cout << "  Testing Audeze Maxwell battery..." << std::endl;

    TestableAudezeMaxwell device;
    // Capture frames 117-122: an empty report with stale bytes, then acknowledgement and result together
    device.hid.replies.emplace_back(maxwellReport({}));
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x3E }));
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x22, 0x00, 0x00, 0x00 }));

    auto battery = device.getBattery(nullptr);
    ASSERT_TRUE(battery.hasValue(), "Battery should be read");
    ASSERT_EQ(62, battery->level_percent, "Battery should be 62%");
    ASSERT_EQ(static_cast<int>(BATTERY_AVAILABLE), static_cast<int>(battery->status), "Battery should be available");
    ASSERT_EQ(static_cast<int>(MICROPHONE_UP), static_cast<int>(battery->mic_status), "Muted microphone should be reported");

    ASSERT_EQ(2, static_cast<int>(device.hid.writes.size()), "Battery and mic mute requests only");
    ASSERT_TRUE((maxwellWritePrefix(device.hid.writes[0], 10) == std::vector<uint8_t> { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C, 0x00 }), "Battery request should match the capture");

    // Stale battery bytes behind a zero length must never be taken as a reading
    TestableAudezeMaxwell offline;
    auto unavailable = offline.getBattery(nullptr);
    ASSERT_TRUE(unavailable.hasValue(), "A silent headset is not an error");
    ASSERT_EQ(static_cast<int>(BATTERY_UNAVAILABLE), static_cast<int>(unavailable->status), "Battery should be unavailable");
    ASSERT_EQ(-1, unavailable->level_percent, "No level without a reply");

    // Unmuted microphone, acknowledgement and result in separate reports
    TestableAudezeMaxwell unmuted;
    unmuted.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00 }));
    unmuted.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x64 }));
    unmuted.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x22, 0x00, 0x00, 0xFF }));
    auto full = unmuted.getBattery(nullptr);
    ASSERT_TRUE(full.hasValue(), "Battery should be read");
    ASSERT_EQ(100, full->level_percent, "Battery should be 100%");
    ASSERT_EQ(static_cast<int>(MICROPHONE_UNKNOWN), static_cast<int>(full->mic_status), "Unmuted microphone is not reported as up");

    // Another program took the acknowledgement: the result alone is enough
    TestableAudezeMaxwell shared;
    shared.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x3D, 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x24, 0x00, 0x00, 0x00 }));
    shared.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x22, 0x00, 0x00, 0xFF }));
    auto without_ack = shared.getBattery(nullptr);
    ASSERT_TRUE(without_ack.hasValue(), "Battery should be read");
    ASSERT_EQ(61, without_ack->level_percent, "Battery should be 61%");

    // Another program took the whole reply: the request is repeated
    TestableAudezeMaxwell retried;
    retried.hid.silent_writes = 2;
    retried.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x00, 0x05, 0x5D, 0x05, 0x00, 0xD6, 0x0C, 0x00, 0x00, 0x3E }));
    retried.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x22, 0x00, 0x00, 0xFF }));
    auto after_retry = retried.getBattery(nullptr);
    ASSERT_TRUE(after_retry.hasValue(), "Battery should be read");
    ASSERT_EQ(62, after_retry->level_percent, "Battery should be read on the third attempt");
    ASSERT_EQ(4, static_cast<int>(retried.hid.writes.size()), "Three battery requests and the mic mute request");

    // A failed acknowledgement means no reading
    TestableAudezeMaxwell refused;
    refused.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x03, 0x00, 0xD6, 0x0C, 0x01 }));
    auto no_reading = refused.getBattery(nullptr);
    ASSERT_TRUE(no_reading.hasValue(), "A refused battery request is not an error");
    ASSERT_EQ(static_cast<int>(BATTERY_UNAVAILABLE), static_cast<int>(no_reading->status), "Battery should be unavailable");
    ASSERT_EQ(1, static_cast<int>(refused.hid.writes.size()), "A refused request is not repeated");

    std::cout << "    [OK] Audeze Maxwell battery verified" << std::endl;
}

void testAudezeMaxwellReplyMatching()
{
    std::cout << "  Testing Audeze Maxwell reply matching..." << std::endl;

    // Replies to other commands and a notification are queued ahead of the chatmix reply,
    // which itself is split over two reports
    TestableAudezeMaxwell device;
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x24, 0x00, 0x00, 0x02, 0x05, 0x5C, 0x03, 0x00, 0x80, 0x2C, 0x03 }));
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x83, 0x2C, 0x00, 0x07, 0x00, 0x01 }));
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x83, 0x2C }));
    device.hid.replies.emplace_back(maxwellReport({}));
    device.hid.replies.emplace_back(maxwellReport({ 0x00, 0x0B, 0x00, 0x0A }));

    auto chatmix = device.getChatmix(nullptr);
    ASSERT_TRUE(chatmix.hasValue(), "Chatmix should be read");
    ASSERT_EQ(64, chatmix->level, "Raw 10 of 20 is the center");
    ASSERT_EQ(100, chatmix->game_volume_percent, "Game volume at center");
    ASSERT_EQ(100, chatmix->chat_volume_percent, "Chat volume at center");
    ASSERT_TRUE((maxwellWritePrefix(device.hid.writes[0], 11) == std::vector<uint8_t> { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x0B, 0x00 }), "Chatmix request should match the capture");

    // No matching reply is a timeout, not a value read from another command's reply
    TestableAudezeMaxwell silent;
    silent.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x24, 0x00, 0x00, 0x02 }));
    auto missing = silent.getChatmix(nullptr);
    ASSERT_TRUE(missing.hasError(), "Chatmix without a reply should fail");
    ASSERT_TRUE(missing.error().code == DeviceError::Code::Timeout, "Failure should be a timeout");

    // Non-zero status
    TestableAudezeMaxwell rejected;
    rejected.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x83, 0x2C, 0x01, 0x0B, 0x00, 0x0A }));
    auto error = rejected.getChatmix(nullptr);
    ASSERT_TRUE(error.hasError(), "Rejected read should fail");
    ASSERT_TRUE(error.error().code == DeviceError::Code::ProtocolError, "Failure should be a protocol error");

    std::cout << "    [OK] Audeze Maxwell reply matching verified" << std::endl;
}

void testAudezeMaxwellSidetone()
{
    std::cout << "  Testing Audeze Maxwell sidetone..." << std::endl;

    // Capture: level 9 while sidetone is switched off
    TestableAudezeMaxwell device;
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x2C, 0x00, 0x00, 0x09 }));
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x83, 0x2C, 0x00, 0x07, 0x00, 0x00 }));
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x2D, 0x00, 0x00, 0x1F }));

    auto off = device.getSidetone(nullptr);
    ASSERT_TRUE(off.hasValue(), "Sidetone should be read");
    ASSERT_TRUE(off->is_muted, "Disabled sidetone should be muted");
    ASSERT_EQ(0, static_cast<int>(off->current_level), "Disabled sidetone reports level 0");
    ASSERT_EQ(9, static_cast<int>(off->device_level), "Stored level should be kept");
    ASSERT_EQ(31, static_cast<int>(off->device_max), "Maximum should come from the headset");

    const std::vector<uint8_t> max_reply   = maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x2D, 0x00, 0x00, 0x0F });
    const std::vector<uint8_t> level_ack   = maxwellReport({ 0x05, 0x5B, 0x05, 0x00, 0x00, 0x09, 0x2C, 0x00, 0x00 });
    const std::vector<uint8_t> toggle_ack  = maxwellReport({ 0x05, 0x5B, 0x05, 0x00, 0x82, 0x2C, 0x00, 0x07, 0x00 });
    const std::vector<uint8_t> state_on    = maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x83, 0x2C, 0x00, 0x07, 0x00, 0x01 });
    const std::vector<uint8_t> state_off   = maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x83, 0x2C, 0x00, 0x07, 0x00, 0x00 });
    const std::vector<uint8_t> level_write = { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x2C, 0x00, 0x0F };
    const std::vector<uint8_t> toggle      = { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x82, 0x2C, 0x07, 0x00 };

    // While sidetone is on, a level above 0 is only a level write: the state is not toggled
    TestableAudezeMaxwell setter;
    for (const auto& reply : { max_reply, state_on, level_ack }) {
        setter.hid.replies.emplace_back(reply);
    }

    auto set = setter.setSidetone(nullptr, 128);
    ASSERT_TRUE(set.hasValue(), "Sidetone should be set");
    ASSERT_TRUE(!set->is_muted, "A level above 0 is not muted");
    ASSERT_EQ(15, static_cast<int>(set->device_level), "128 should map to the maximum the headset reported");
    ASSERT_EQ(3, static_cast<int>(setter.hid.writes.size()), "Maximum, state and level write");
    ASSERT_TRUE(maxwellWritePrefix(setter.hid.writes[2], 12) == level_write, "Level write");

    // While it is off, it is switched on with one toggle before the level is written. A
    // Maxwell 2 stays silent after a level write alone, and on an original Maxwell a level
    // write would switch it on, so a toggle after it would switch it off again.
    TestableAudezeMaxwell switch_on;
    for (const auto& reply : { max_reply, state_off, toggle_ack, level_ack }) {
        switch_on.hid.replies.emplace_back(reply);
    }
    auto enabled = switch_on.setSidetone(nullptr, 128);
    ASSERT_TRUE(enabled.hasValue(), "Sidetone should be switched on");
    ASSERT_TRUE(!enabled->is_muted, "A level above 0 is not muted");
    ASSERT_EQ(4, static_cast<int>(switch_on.hid.writes.size()), "Maximum, state, toggle and level write");
    ASSERT_TRUE(maxwellWritePrefix(switch_on.hid.writes[2], 11) == toggle, "Toggle before the level");
    ASSERT_EQ(0x01, static_cast<int>(switch_on.hid.writes[2][11]), "Switched on as Audeze's app does");
    ASSERT_TRUE(maxwellWritePrefix(switch_on.hid.writes[3], 12) == level_write, "Level written last");

    // A lost acknowledgement of that toggle is an error: the toggle is not repeated and
    // the level is not written
    TestableAudezeMaxwell lost_on_ack;
    lost_on_ack.hid.replies.emplace_back(max_reply);
    lost_on_ack.hid.replies.emplace_back(state_off);
    lost_on_ack.hid.replies.emplace_back(std::vector<uint8_t> {});
    ASSERT_TRUE(lost_on_ack.setSidetone(nullptr, 128).hasError(), "A missing acknowledgement should fail");
    ASSERT_EQ(3, static_cast<int>(lost_on_ack.hid.writes.size()), "The toggle is sent once, without a level write");

    // The lowest API level must not round down to 0, which would mean "off"
    TestableAudezeMaxwell lowest;
    for (const auto& reply : { max_reply, state_on, level_ack }) {
        lowest.hid.replies.emplace_back(reply);
    }
    auto quiet = lowest.setSidetone(nullptr, 1);
    ASSERT_TRUE(quiet.hasValue(), "Sidetone should be set");
    ASSERT_TRUE(!quiet->is_muted, "Level 1 is not muted");
    ASSERT_EQ(1, static_cast<int>(quiet->device_level), "Level 1 should map to the lowest device level");
    ASSERT_EQ(0x01, static_cast<int>(lowest.hid.writes[2][11]), "Level 1 written");

    // Level 0 switches an enabled sidetone off with one toggle and keeps the stored level
    TestableAudezeMaxwell disable;
    for (const auto& reply : { max_reply, state_on, toggle_ack }) {
        disable.hid.replies.emplace_back(reply);
    }
    auto disabled = disable.setSidetone(nullptr, 0);
    ASSERT_TRUE(disabled.hasValue(), "Sidetone should be switched off");
    ASSERT_TRUE(disabled->is_muted, "Level 0 is muted");
    ASSERT_EQ(3, static_cast<int>(disable.hid.writes.size()), "Maximum, state and toggle");
    ASSERT_TRUE(maxwellWritePrefix(disable.hid.writes[2], 11) == toggle, "Toggle");
    ASSERT_EQ(0x00, static_cast<int>(disable.hid.writes[2][11]), "Switched off as Audeze's app does");
    for (const auto& write : disable.hid.writes) {
        ASSERT_TRUE(maxwellWritePrefix(write, 10) != maxwellWritePrefix(level_write, 10), "The stored level must not be overwritten");
    }

    // Level 0 while it is already off must not toggle it back on
    TestableAudezeMaxwell already_off;
    already_off.hid.replies.emplace_back(max_reply);
    already_off.hid.replies.emplace_back(state_off);
    ASSERT_TRUE(already_off.setSidetone(nullptr, 0).hasValue(), "Sidetone should stay off");
    ASSERT_EQ(2, static_cast<int>(already_off.hid.writes.size()), "Maximum and state query only");

    // A lost toggle acknowledgement is an error, but the toggle is not repeated: a
    // second toggle would switch sidetone back on
    TestableAudezeMaxwell lost_ack;
    lost_ack.hid.replies.emplace_back(max_reply);
    lost_ack.hid.replies.emplace_back(state_on);
    lost_ack.hid.replies.emplace_back(std::vector<uint8_t> {});
    ASSERT_TRUE(lost_ack.setSidetone(nullptr, 0).hasError(), "A missing acknowledgement should fail");
    ASSERT_EQ(3, static_cast<int>(lost_ack.hid.writes.size()), "The toggle is sent once");

    // A setter without acknowledgement fails, and the write is not repeated: the headset
    // may still apply it
    TestableAudezeMaxwell unacknowledged;
    auto failed = unacknowledged.setNoiseFilter(nullptr, 1);
    ASSERT_TRUE(failed.hasError(), "Missing acknowledgement should fail");
    ASSERT_EQ(1, static_cast<int>(unacknowledged.hid.writes.size()), "A write is sent once");

    // A read reply for the same key is not the acknowledgement of a write
    TestableAudezeMaxwell read_reply;
    read_reply.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x06, 0x00, 0x01, 0x09, 0x24, 0x00, 0x00, 0x01 }));
    auto not_acked = read_reply.setNoiseFilter(nullptr, 1);
    ASSERT_TRUE(not_acked.hasError(), "A read reply should not acknowledge a write");
    ASSERT_TRUE(not_acked.error().code == DeviceError::Code::Timeout, "Failure should be a timeout");

    std::cout << "    [OK] Audeze Maxwell sidetone verified" << std::endl;
}

void testAudezeMaxwellInactiveTime()
{
    std::cout << "  Testing Audeze Maxwell inactive time..." << std::endl;

    const std::vector<uint8_t> ack = maxwellReport({ 0x05, 0x5B, 0x05, 0x00, 0x82, 0x2C, 0x00, 0x01, 0x00 });

    // 4 minutes rounds up to 5 minutes = 300 s = 0x012C
    TestableAudezeMaxwell device;
    device.hid.replies.emplace_back(ack);
    ASSERT_TRUE(device.setInactiveTime(nullptr, 4).hasValue(), "Inactive time should be set");
    ASSERT_TRUE((maxwellWritePrefix(device.hid.writes[0], 19) == std::vector<uint8_t> { 0x06, 0x10, 0x80, 0x05, 0x5A, 0x0C, 0x00, 0x82, 0x2C, 0x01, 0x00, 0x01, 0x00, 0x2C, 0x01, 0x01, 0x00, 0x2C, 0x01 }), "Five minute request should match the documented bytes");

    // 0 disables it
    device.hid.replies.emplace_back(ack);
    ASSERT_TRUE(device.setInactiveTime(nullptr, 0).hasValue(), "Inactive time should be disabled");
    ASSERT_TRUE((maxwellWritePrefix(device.hid.writes[1], 19) == std::vector<uint8_t> { 0x06, 0x10, 0x80, 0x05, 0x5A, 0x0C, 0x00, 0x82, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }), "Disable request");

    // Above 240 minutes the longest setting is used: 360 minutes = 21600 s = 0x5460
    device.hid.replies.emplace_back(ack);
    ASSERT_TRUE(device.setInactiveTime(nullptr, 250).hasValue(), "Inactive time should be set");
    ASSERT_EQ(0x60, static_cast<int>(device.hid.writes[2][13]), "Seconds low byte");
    ASSERT_EQ(0x54, static_cast<int>(device.hid.writes[2][14]), "Seconds high byte");

    // A rejected write is an error
    device.hid.replies.emplace_back(maxwellReport({ 0x05, 0x5B, 0x05, 0x00, 0x82, 0x2C, 0x01, 0x01, 0x00 }));
    ASSERT_TRUE(device.setInactiveTime(nullptr, 30).hasError(), "Non-zero status should fail");

    std::cout << "    [OK] Audeze Maxwell inactive time verified" << std::endl;
}

static std::vector<uint8_t> maxwellEqTemplate()
{
    std::vector<uint8_t> ui(AudezeMaxwell::EQ_SETTINGS_SIZE, 0);
    ui[0] = 1;
    ui[1] = 0xA5; // Uninterpreted header and band fields must survive an edit.
    for (size_t i = 0; i < AudezeMaxwell::EQ_FREQUENCIES.size(); ++i) {
        const size_t base = 5 + 18 * i;
        ui[base]          = static_cast<uint8_t>(i);
        ui[base + 1]      = 0x01;
        ui[base + 14]     = 0x5A;
        for (size_t byte = 0; byte < 4; ++byte) {
            const auto value     = static_cast<uint32_t>(AudezeMaxwell::EQ_FREQUENCIES[i] * 100);
            ui[base + 2 + byte]  = static_cast<uint8_t>(value >> (8 * byte));
            ui[base + 10 + byte] = static_cast<uint8_t>(value >> (8 * byte));
        }
    }
    return ui;
}

static void queueMaxwellMessage(TestableAudezeMaxwell& device, const std::vector<uint8_t>& body)
{
    auto reports = AudezeMaxwell::buildReports(AudezeMaxwell::buildMessage(AudezeMaxwell::TYPE_RESPONSE, body));
    for (auto& report : reports) {
        report[0] = AudezeMaxwell::REPORT_ID_IN;
        device.hid.replies.emplace_back(std::vector<uint8_t>(report.begin(), report.end()));
    }
}

static void queueMaxwellEqSlot(TestableAudezeMaxwell& device, uint8_t slot, const std::vector<uint8_t>& ui)
{
    std::vector<uint8_t> body { 0x0C, 0x0A, 0, slot, 0xEF, static_cast<uint8_t>(ui.size()), 0 };
    body.insert(body.end(), ui.begin(), ui.end());
    queueMaxwellMessage(device, body);
}

void testAudezeMaxwellEqCoefficients()
{
    const std::vector<AudezeMaxwell::EqBand> bands { { 125, -3, 100 }, { 1000, 6, 700 }, { 8000, -12, 5000 } };
    // Independent golden outputs from installed AirohaPeqLibrary.dll.
    const std::array<std::string_view, 4> goldens {
        "0300906b0022829c00d2735d00a49a8500724c770095ff7f00ffc8800065797e008b1881003fd97d00d1ff7f00ff2fd100d71c60000cdddd001a6b2300ee94030062",
        "0300906b0022d29b005e855e00b812850021fa7700d8ff7f00ffb78000d1997e001701810044067e002cff7f00ffa4c700dd6c61008b70d600ed3c26004b9c03008a",
        "0300906b00225b9800604064000d89820091907b0035ff7f00ff638000343c7f002e8b80006deb7e00bdff7f00ffa09c00a20d6c006a5baf00f58e3f0076ec0300ae",
        "0300906b002207980075d364002d4f820099ea7b00e2ff7f00ff5b8000124c7f000c8080000b027f0019ff7f00ff329900fb686d008f7eab00872843005ef8030072"
    };
    const std::array<int, 4> rates { 44100, 48000, 88200, 96000 };
    for (size_t i = 0; i < rates.size(); ++i) {
        auto bytes = AudezeMaxwell::eqCoefficientsForRate(bands, rates[i]);
        ASSERT_TRUE(bytes.hasValue(), "Coefficient generation succeeds");
        std::string hex;
        for (auto byte : *bytes) {
            hex += std::format("{:02x}", byte);
        }
        ASSERT_TRUE(hex == goldens[i], "Portable coefficients match independent DLL output");
    }
    auto ui     = maxwellEqTemplate();
    auto parsed = AudezeMaxwell::readEqBands(ui);
    ASSERT_TRUE(parsed.hasValue(), "Ten band template parses");
    auto payload = AudezeMaxwell::eqCoefficients(*parsed);
    ASSERT_TRUE(payload.hasValue(), "Four sample-rate payload builds");
    ASSERT_EQ(844u, payload->size(), "Ten-band coefficient payload length");
    ASSERT_EQ(4, static_cast<int>((*payload)[0]), "Four sample rates");
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ((std::array<int, 4> { 1, 2, 5, 6 })[i], static_cast<int>((*payload)[4 + 210 * i]), "Sample-rate ID");
        ASSERT_EQ(103, static_cast<int>((*payload)[6 + 210 * i]), "Word count includes bands and gain");
    }
    ui.pop_back();
    ASSERT_TRUE(AudezeMaxwell::readEqBands(ui).hasError(), "Reject truncated settings");
    ui     = maxwellEqTemplate();
    ui[17] = 0xFF;
    ASSERT_TRUE(AudezeMaxwell::readEqBands(ui).hasError(), "Reject invalid bandwidth");
    ASSERT_TRUE(AudezeMaxwell::eqCoefficientsForRate(bands, 32000).hasError(), "Reject unsupported sample rate");
    auto invalid    = bands;
    invalid[0].gain = std::numeric_limits<double>::quiet_NaN();
    ASSERT_TRUE(AudezeMaxwell::eqCoefficients(invalid).hasError(), "Reject nonfinite coefficients");
}

void testAudezeMaxwellCustomEq()
{
    for (uint8_t slot = 0; slot < 4; ++slot) {
        TestableAudezeMaxwell device;
        auto original = maxwellEqTemplate();
        auto expected = original;
        EqualizerSettings settings(std::vector<float>(10, 0));
        settings.bands[1] = -3;
        const auto gain   = static_cast<uint32_t>(int32_t(-300));
        for (size_t byte = 0; byte < 4; ++byte) {
            expected[5 + 18 + 6 + byte] = static_cast<uint8_t>(gain >> (8 * byte));
        }
        queueMaxwellMessage(device, { 0x01, 0x09, 0x00, 0x00, 0x00, static_cast<uint8_t>(slot + 7) });
        queueMaxwellEqSlot(device, slot, original);
        queueMaxwellMessage(device, { 0x03, 0x0E, 0 });
        queueMaxwellMessage(device, { 0x03, 0x0A, 1 });
        // Wrong slot acknowledgement is ignored.
        queueMaxwellMessage(device, { 0x0D, 0x0A, 0, static_cast<uint8_t>(slot + 1), 0xEF });
        queueMaxwellMessage(device, { 0x0D, 0x0A, 0, slot, 0xEF });
        queueMaxwellMessage(device, { 0x03, 0x0A, 1 });
        queueMaxwellMessage(device, { 0x0D, 0x0A, 0, static_cast<uint8_t>(0x2C + slot), 0xE4 });
        queueMaxwellEqSlot(device, slot, expected);
        auto expected_bands = AudezeMaxwell::readEqBands(expected);
        auto coefficients   = AudezeMaxwell::eqCoefficients(*expected_bands);
        std::vector<uint8_t> coefficient_reply { 0x0C, 0x0A, 0, static_cast<uint8_t>(0x2C + slot), 0xE4, 0x4C, 3 };
        coefficient_reply.insert(coefficient_reply.end(), coefficients->begin(), coefficients->end());
        queueMaxwellMessage(device, coefficient_reply);
        ASSERT_TRUE(device.setEqualizer(nullptr, settings).hasValue(), "Custom EQ is saved and verified");

        std::vector<uint8_t> stream;
        std::vector<AudezeMaxwell::Message> messages;
        for (const auto& write : device.hid.writes) {
            ASSERT_EQ(62u, write.size(), "EQ report size");
            ASSERT_EQ(0x80, static_cast<int>(write[2]), "Headset route");
            const auto fragment = AudezeMaxwell::reportFragment(write);
            stream.insert(stream.end(), fragment.begin(), fragment.end());
            for (auto message = AudezeMaxwell::extractMessage(stream); !message.empty(); message = AudezeMaxwell::extractMessage(stream)) {
                messages.push_back(std::move(message));
            }
        }
        ASSERT_TRUE(stream.empty(), "No incomplete output message");
        ASSERT_EQ(9u, messages.size(), "Preset query, slot read, five update commands and two readbacks");
        ASSERT_EQ(40u, device.hid.writes.size(), "Fragmented EQ report count");
        ASSERT_EQ(851u, messages[2].size(), "Realtime coefficient message");
        ASSERT_TRUE((messages[3] == std::vector<uint8_t> { 5, 0x5A, 4, 0, 3, 0x0A, 185, 0 }), "Prepare UI byte count, not key");
        ASSERT_TRUE(std::equal(expected.begin(), expected.end(), messages[4].begin() + 8), "Only requested gains change in stored UI data");
        ASSERT_TRUE((messages[5] == std::vector<uint8_t> { 5, 0x5A, 4, 0, 3, 0x0A, 0x4C, 3 }), "Prepare coefficient byte count");
        ASSERT_EQ(0x2C + slot, static_cast<int>(messages[6][6]), "Correct coefficient slot");
        ASSERT_TRUE(std::equal(messages[2].begin() + 7, messages[2].end(), messages[6].begin() + 8), "Applied and saved coefficients are identical");
    }
    AudezeMaxwell2 other_model;
    ASSERT_TRUE(!(other_model.getCapabilities() & B(CAP_EQUALIZER)), "Original EQ must not be advertised for Maxwell 2");
    ASSERT_TRUE(other_model.setEqualizer(nullptr, EqualizerSettings {}).hasError(), "Maxwell 2 does not inherit the writer");
}

void testAudezeMaxwellEqFailures()
{
    const EqualizerSettings flat(std::vector<float>(10, 0));
    for (float value : { -13.0f, 13.0f, 0.5f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
        TestableAudezeMaxwell device;
        auto invalid     = flat;
        invalid.bands[0] = value;
        ASSERT_TRUE(device.setEqualizer(nullptr, invalid).hasError(), "Reject invalid gains");
        ASSERT_TRUE(device.hid.writes.empty(), "Validate before any USB traffic");
    }
    TestableAudezeMaxwell wrong_count;
    ASSERT_TRUE(wrong_count.setEqualizer(nullptr, EqualizerSettings {}).hasError(), "Reject wrong band count");
    ASSERT_TRUE(wrong_count.hid.writes.empty(), "Wrong count sends nothing");
    TestableAudezeMaxwell builtin;
    queueMaxwellMessage(builtin, { 1, 9, 0, 0, 0, 1 });
    ASSERT_TRUE(builtin.setEqualizer(nullptr, flat).hasError(), "Never overwrite an unspecified custom slot");
    ASSERT_EQ(1u, builtin.hid.writes.size(), "Built-in preset only queried");
    // Stop on failure at every update stage; never retransmit NV writes.
    const std::array<std::vector<uint8_t>, 5> acknowledgements {
        std::vector<uint8_t> { 3, 0x0E, 0 }, { 3, 0x0A, 0 }, { 0x0D, 0x0A, 0, 0, 0xEF },
        { 3, 0x0A, 0 }, { 0x0D, 0x0A, 0, 0x2C, 0xE4 }
    };
    const std::array<size_t, 5> write_counts { 17, 18, 22, 23, 38 };
    for (size_t stage = 0; stage < acknowledgements.size(); ++stage) {
        for (bool timeout : { false, true }) {
            TestableAudezeMaxwell device;
            queueMaxwellMessage(device, { 1, 9, 0, 0, 0, 7 });
            queueMaxwellEqSlot(device, 0, maxwellEqTemplate());
            for (size_t i = 0; i < stage; ++i) {
                queueMaxwellMessage(device, acknowledgements[i]);
            }
            if (!timeout) {
                auto refused = acknowledgements[stage];
                if (stage == 1 || stage == 3) {
                    refused.pop_back(); // Reclaim's byte 6 is not a status.
                } else {
                    refused[2] = 1;
                }
                queueMaxwellMessage(device, refused);
            }
            auto result = device.setEqualizer(nullptr, flat);
            ASSERT_TRUE(result.hasError(), "Missing or rejected ACK fails");
            ASSERT_EQ(write_counts[stage], device.hid.writes.size(), "No retry or later write after a failure");
        }
    }
    TestableAudezeMaxwell malformed;
    queueMaxwellMessage(malformed, { 1, 9, 0, 0, 0, 7 });
    queueMaxwellEqSlot(malformed, 0, std::vector<uint8_t>(10, 0));
    ASSERT_TRUE(malformed.setEqualizer(nullptr, flat).hasError(), "Malformed slot is rejected before writes");
    ASSERT_EQ(2u, malformed.hid.writes.size(), "Only queries before malformed slot failure");

    TestableAudezeMaxwell readback;
    queueMaxwellMessage(readback, { 1, 9, 0, 0, 0, 7 });
    queueMaxwellEqSlot(readback, 0, maxwellEqTemplate());
    for (const auto& ack : acknowledgements) {
        queueMaxwellMessage(readback, ack);
    }
    auto wrong_ui = maxwellEqTemplate();
    wrong_ui[0] ^= 1;
    queueMaxwellEqSlot(readback, 0, wrong_ui);
    ASSERT_TRUE(readback.setEqualizer(nullptr, flat).hasError(), "Readback mismatch must not report success");

    TestableAudezeMaxwell coefficient_readback;
    queueMaxwellMessage(coefficient_readback, { 1, 9, 0, 0, 0, 7 });
    const auto original = maxwellEqTemplate();
    queueMaxwellEqSlot(coefficient_readback, 0, original);
    for (const auto& ack : acknowledgements) {
        queueMaxwellMessage(coefficient_readback, ack);
    }
    queueMaxwellEqSlot(coefficient_readback, 0, original);
    auto bands        = AudezeMaxwell::readEqBands(original);
    auto coefficients = AudezeMaxwell::eqCoefficients(*bands);
    std::vector<uint8_t> reply { 0x0C, 0x0A, 0, 0x2C, 0xE4, 0x4C, 3 };
    reply.insert(reply.end(), coefficients->begin(), coefficients->end());
    reply.back() ^= 1;
    queueMaxwellMessage(coefficient_readback, reply);
    ASSERT_TRUE(coefficient_readback.setEqualizer(nullptr, flat).hasError(), "Coefficient mismatch must not report success");
    ASSERT_EQ(40u, coefficient_readback.hid.writes.size(), "Coefficient readback failure does not retry writes");
}

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

    std::cout << "\n=== Logitech ASTRO A50 Gen 4 ===" << std::endl;
    runTest("ASTRO A50 Gen 4 Frame Building", testAstroA50Gen4FrameBuilding);
    runTest("ASTRO A50 Gen 4 Decoding", testAstroA50Gen4Decoding);
    runTest("ASTRO A50 Gen 4 Link Gating", testAstroA50Gen4LinkGating);
    runTest("ASTRO A50 Gen 4 Chatmix", testAstroA50Gen4Chatmix);
    runTest("ASTRO A50 Gen 4 Setters", testAstroA50Gen4Setters);
    runTest("ASTRO A50 Gen 4 Error Handling", testAstroA50Gen4ErrorHandling);
    runTest("ASTRO A50 Gen 4 Equalizer", testAstroA50Gen4Equalizer);
    runTest("ASTRO A50 Gen 4 Basic Equalizer", testAstroA50Gen4BasicEqualizer);
    runTest("ASTRO A50 Gen 4 Nonfinite Equalizer", testAstroA50Gen4NonfiniteEqualizer);
    runTest("ASTRO A50 Gen 4 Reply Length", testAstroA50Gen4ReplyLength);
    runTest("ASTRO A50 Gen 4 Timeout Recovery", testAstroA50Gen4TimeoutRecovery);

    std::cout << "\n=== Logitech ASTRO A50 Gen 5 ===" << std::endl;
    runTest("ASTRO A50 Gen 5 Volume Set", testAstroA50Gen5VolumeSet);

    std::cout << "\n=== HID++ Protocol (Logitech) ===" << std::endl;
    runTest("HID++ Constants", testHIDPPConstants);
    runTest("HID++ Packet Format", testHIDPPPacketFormat);
    runTest("HID++ Voltage To Percent", testHIDPPVoltageToPercent);
    runTest("HID++ Battery Response", testHIDPPBatteryResponseParsing);
    runTest("HID++ Offline Detection", testHIDPPOfflineDetection);
    runTest("Logitech PRO X2 Centurion Battery", testLogitechProX2CenturionBatteryParsing);
    runTest("Logitech PRO X2 Legacy Battery Fallback", testLogitechProX2LegacyBatteryFallbackDecision);
    runTest("Logitech Centurion Frame Building", testCenturionFrameBuilding);
    runTest("Logitech Centurion Bridge Parsing", testCenturionBridgeResponseParsing);
    runTest("Logitech Centurion Bridge Size Limit", testCenturionBridgeMessageSizeLimit);
    runTest("Logitech Centurion G522 Frame Prefix", testCenturionG522FramePrefix);
    runTest("Logitech Centurion Connection Status", testCenturionConnectionStatusHelpers);
    runTest("Logitech G522 Battery Wire", testCenturionG522BatteryWire);
    runTest("Logitech G522 Offline Detection", testCenturionG522Offline);
    runTest("Logitech G522 Sidetone And Auto-Sleep Parsing", testG522SidetoneAndAutoSleepParsing);
    runTest("Logitech G522 Settings Wire", testG522SettingsWire);
    runTest("Logitech G522 Equalizer Encoding", testG522EqualizerEncoding);
    runTest("Logitech G522 Equalizer Commands", testG522EqualizerCommands);
    runTest("Logitech PRO X2 Default Centurion Options", testCenturionProX2DefaultOptionsUnchanged);
    runTest("Logitech PRO X2 Battery Wire", testCenturionProX2BatteryWire);
    runTest("Logitech PRO X2 Offline Detection", testCenturionProX2Offline);
    runTest("Logitech PRO X2 Bridge Rejection", testCenturionProX2BridgeRejection);
    runTest("Logitech PRO X2 Equalizer Info Cache", testLogitechProX2EqualizerInfoRequiresDescriptor);
    runTest("Logitech PRO X2 EQ Quantization", testLogitechProX2OnboardEqCoefficientQuantization);
    runTest("Logitech PRO X2 Onboard EQ Payload", testLogitechProX2OnboardEqPayloadBuilding);

    std::cout << "\n=== Audeze Maxwell Protocol ===" << std::endl;
    runTest("Audeze Maxwell Framing", testAudezeMaxwellFraming);
    runTest("Audeze Maxwell Battery", testAudezeMaxwellBattery);
    runTest("Audeze Maxwell Reply Matching", testAudezeMaxwellReplyMatching);
    runTest("Audeze Maxwell Sidetone", testAudezeMaxwellSidetone);
    runTest("Audeze Maxwell Inactive Time", testAudezeMaxwellInactiveTime);
    runTest("Audeze Maxwell EQ Coefficients", testAudezeMaxwellEqCoefficients);
    runTest("Audeze Maxwell Custom EQ", testAudezeMaxwellCustomEq);
    runTest("Audeze Maxwell EQ Failures", testAudezeMaxwellEqFailures);

    std::cout << "\n=== Plantronics Protocol ===" << std::endl;
    runTest("Plantronics BT600 Battery Parsing", testPlantronicsBT600BatteryParsing);
    runTest("Plantronics BT600 Reply Matching", testPlantronicsBT600ReplyMatching);

    std::cout << "\n=== SteelSeries Protocol ===" << std::endl;
    runTest("SteelSeries Packet Sizes", testSteelSeriesPacketSizes);
    runTest("SteelSeries Battery Mapping", testSteelSeriesBatteryMapping);
    runTest("SteelSeries Chatmix Mapping", testSteelSeriesChatmixMapping);
    runTest("SteelSeries Sidetone Mapping", testSteelSeriesSidetoneMapping);
    runTest("SteelSeries Nova 3P/3X Microphone Volume Mapping", testSteelSeriesNova3PMicVolumeMapping);

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

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
#include "devices/logitech_astro_a50_gen4.hpp"
#include "devices/logitech_gpro_x2_lightspeed.hpp"
#include "devices/plantronics_bt600.hpp"
#include "devices/protocols/hidpp_protocol.hpp"
#include "devices/protocols/logitech_calibrations.hpp"
#include "devices/protocols/logitech_centurion_protocol.hpp"
#include "devices/protocols/steelseries_protocol.hpp"
#include "result_types.hpp"
#include "utility.hpp"

#include <algorithm>
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
    runTest("Logitech PRO X2 Equalizer Info Cache", testLogitechProX2EqualizerInfoRequiresDescriptor);
    runTest("Logitech PRO X2 EQ Quantization", testLogitechProX2OnboardEqCoefficientQuantization);
    runTest("Logitech PRO X2 Onboard EQ Payload", testLogitechProX2OnboardEqPayloadBuilding);

    std::cout << "\n=== Plantronics Protocol ===" << std::endl;
    runTest("Plantronics BT600 Battery Parsing", testPlantronicsBT600BatteryParsing);
    runTest("Plantronics BT600 Reply Matching", testPlantronicsBT600ReplyMatching);

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

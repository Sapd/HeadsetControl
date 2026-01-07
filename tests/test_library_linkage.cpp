/**
 * @file test_library_linkage.cpp
 * @brief Test that demonstrates linking against HeadsetControl library
 *
 * This test verifies that:
 * 1. The library can be linked by external applications
 * 2. The test device can be instantiated and used directly
 * 3. All device methods work correctly without HID hardware
 *
 * This serves as an example for how to use the HeadsetControl library
 * in your own applications.
 */

#include "device_registry.hpp"
#include "devices/headsetcontrol_test.hpp"
#include "headsetcontrol.hpp"
#include "result_types.hpp"

#include <iostream>
#include <memory>
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

// ============================================================================
// Direct Device Instantiation Tests
// ============================================================================

void testDirectDeviceInstantiation()
{
    std::cout << "  Testing direct device instantiation..." << std::endl;

    // Create test device directly - no HID hardware needed
    HeadsetControlTest device;

    // Verify device properties
    ASSERT_EQ(0xF00B, device.getVendorId(), "Vendor ID should be 0xF00B");
    ASSERT_EQ("HeadsetControl Test device", std::string(device.getDeviceName()),
        "Device name should match");

    auto productIds = device.getProductIds();
    ASSERT_TRUE(!productIds.empty(), "Should have at least one product ID");
    ASSERT_EQ(0xA00C, productIds[0], "Product ID should be 0xA00C");

    std::cout << "    ✓ Device instantiation works" << std::endl;
}

void testDeviceCapabilities()
{
    std::cout << "  Testing device capabilities..." << std::endl;

    HeadsetControlTest device;
    int caps = device.getCapabilities();

    // Test device should have all these capabilities
    ASSERT_TRUE((caps & B(CAP_SIDETONE)) != 0, "Should have sidetone");
    ASSERT_TRUE((caps & B(CAP_BATTERY_STATUS)) != 0, "Should have battery");
    ASSERT_TRUE((caps & B(CAP_LIGHTS)) != 0, "Should have lights");
    ASSERT_TRUE((caps & B(CAP_CHATMIX_STATUS)) != 0, "Should have chatmix");
    ASSERT_TRUE((caps & B(CAP_EQUALIZER)) != 0, "Should have equalizer");
    ASSERT_TRUE((caps & B(CAP_VOICE_PROMPTS)) != 0, "Should have voice prompts");

    std::cout << "    ✓ All expected capabilities present" << std::endl;
}

void testBatteryMethod()
{
    std::cout << "  Testing battery method..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(0); // Normal operation

    // Call battery method (nullptr for device_handle since test device doesn't use it)
    auto result = device.getBattery(nullptr);

    ASSERT_TRUE(result.hasValue(), "Battery should return success");

    auto battery = result.value();
    ASSERT_EQ(42, battery.level_percent, "Battery level should be 42%");
    ASSERT_EQ(BATTERY_AVAILABLE, battery.status, "Battery should be available");
    ASSERT_TRUE(battery.voltage_mv.has_value(), "Should have voltage");
    ASSERT_EQ(3650, battery.voltage_mv.value(), "Voltage should be 3650mV");

    std::cout << "    ✓ Battery method returns correct values" << std::endl;
}

void testSidetoneMethod()
{
    std::cout << "  Testing sidetone method..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(0);

    auto result = device.setSidetone(nullptr, 64);

    ASSERT_TRUE(result.hasValue(), "Sidetone should return success");

    auto sidetone = result.value();
    ASSERT_EQ(64, sidetone.current_level, "Sidetone level should be 64");
    ASSERT_EQ(0, sidetone.min_level, "Min should be 0");
    ASSERT_EQ(128, sidetone.max_level, "Max should be 128");

    std::cout << "    ✓ Sidetone method returns correct values" << std::endl;
}

void testChatmixMethod()
{
    std::cout << "  Testing chatmix method..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(0);

    auto result = device.getChatmix(nullptr);

    ASSERT_TRUE(result.hasValue(), "Chatmix should return success");

    auto chatmix = result.value();
    ASSERT_EQ(64, chatmix.level, "Chatmix level should be 64");
    ASSERT_EQ(50, chatmix.game_volume_percent, "Game volume should be 50%");
    ASSERT_EQ(50, chatmix.chat_volume_percent, "Chat volume should be 50%");

    std::cout << "    ✓ Chatmix method returns correct values" << std::endl;
}

void testLightsMethod()
{
    std::cout << "  Testing lights method..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(0);

    // Test turning lights on
    auto resultOn = device.setLights(nullptr, true);
    ASSERT_TRUE(resultOn.hasValue(), "Lights on should return success");
    ASSERT_TRUE(resultOn.value().enabled, "Lights should be enabled");

    // Test turning lights off
    auto resultOff = device.setLights(nullptr, false);
    ASSERT_TRUE(resultOff.hasValue(), "Lights off should return success");
    ASSERT_TRUE(!resultOff.value().enabled, "Lights should be disabled");

    std::cout << "    ✓ Lights method works correctly" << std::endl;
}

void testErrorConditions()
{
    std::cout << "  Testing error conditions (profile 1)..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(1); // Error condition profile

    // Battery should return error
    auto batteryResult = device.getBattery(nullptr);
    ASSERT_TRUE(!batteryResult.hasValue(), "Battery should return error in profile 1");

    // Sidetone should return error
    auto sidetoneResult = device.setSidetone(nullptr, 64);
    ASSERT_TRUE(!sidetoneResult.hasValue(), "Sidetone should return error in profile 1");

    // Chatmix should return error
    auto chatmixResult = device.getChatmix(nullptr);
    ASSERT_TRUE(!chatmixResult.hasValue(), "Chatmix should return error in profile 1");

    // Reset profile
    headsetcontrol::setTestProfile(0);

    std::cout << "    ✓ Error conditions handled correctly" << std::endl;
}

void testChargingState()
{
    std::cout << "  Testing charging state (profile 2)..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(2); // Charging profile

    auto result = device.getBattery(nullptr);
    ASSERT_TRUE(result.hasValue(), "Battery should return success");

    auto battery = result.value();
    ASSERT_EQ(BATTERY_CHARGING, battery.status, "Battery should be charging");
    ASSERT_EQ(50, battery.level_percent, "Battery level should be 50%");
    ASSERT_TRUE(battery.time_to_full_min.has_value(), "Should have time to full");

    // Reset profile
    headsetcontrol::setTestProfile(0);

    std::cout << "    ✓ Charging state works correctly" << std::endl;
}

void testTimeoutCondition()
{
    std::cout << "  Testing timeout condition (profile 5)..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(5); // Timeout profile

    auto result = device.getBattery(nullptr);
    ASSERT_TRUE(!result.hasValue(), "Battery should return error");
    ASSERT_TRUE(result.error().code == DeviceError::Code::Timeout, "Should be timeout error");

    // Reset profile
    headsetcontrol::setTestProfile(0);

    std::cout << "    ✓ Timeout condition handled correctly" << std::endl;
}

void testBatteryWithoutExtendedInfo()
{
    std::cout << "  Testing battery without extended info (profile 3)..." << std::endl;

    HeadsetControlTest device;
    headsetcontrol::setTestProfile(3); // Basic battery info only

    auto result = device.getBattery(nullptr);
    ASSERT_TRUE(result.hasValue(), "Battery should return success");

    auto battery = result.value();
    ASSERT_EQ(64, battery.level_percent, "Battery level should be 64%");
    ASSERT_EQ(BATTERY_AVAILABLE, battery.status, "Battery should be available");

    // Extended info should NOT be present
    ASSERT_TRUE(!battery.voltage_mv.has_value(), "Should not have voltage");
    ASSERT_TRUE(!battery.time_to_full_min.has_value(), "Should not have time to full");
    ASSERT_TRUE(!battery.time_to_empty_min.has_value(), "Should not have time to empty");

    // Reset profile
    headsetcontrol::setTestProfile(0);

    std::cout << "    ✓ Basic battery info works correctly" << std::endl;
}

// ============================================================================
// Device Registry Tests
// ============================================================================

void testDeviceRegistry()
{
    std::cout << "  Testing device registry lookup..." << std::endl;

    // Look up test device by vendor/product ID
    auto* device = DeviceRegistry::instance().getDevice(0xF00B, 0xA00C);

    ASSERT_TRUE(device != nullptr, "Should find test device in registry");
    ASSERT_EQ("HeadsetControl Test device", std::string(device->getDeviceName()),
        "Found device should be test device");

    std::cout << "    ✓ Device registry lookup works" << std::endl;
}

void testDeviceRegistryIteration()
{
    std::cout << "  Testing device registry iteration..." << std::endl;

    const auto& devices  = DeviceRegistry::instance().getAllDevices();
    int deviceCount      = 0;
    bool foundTestDevice = false;

    for (const auto& device : devices) {
        deviceCount++;
        if (device->getVendorId() == 0xF00B) {
            foundTestDevice = true;
        }
    }

    ASSERT_TRUE(deviceCount > 0, "Registry should have devices");
    ASSERT_TRUE(foundTestDevice, "Should find test device during iteration");

    std::cout << "    ✓ Device registry iteration works (" << deviceCount << " devices)" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllLibraryLinkageTests()
{
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║           Library Linkage Tests                      ║" << std::endl;
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

    std::cout << "=== Direct Device Tests ===" << std::endl;
    runTest("Device Instantiation", testDirectDeviceInstantiation);
    runTest("Device Capabilities", testDeviceCapabilities);
    runTest("Battery Method", testBatteryMethod);
    runTest("Sidetone Method", testSidetoneMethod);
    runTest("Chatmix Method", testChatmixMethod);
    runTest("Lights Method", testLightsMethod);
    runTest("Error Conditions", testErrorConditions);
    runTest("Charging State", testChargingState);
    runTest("Timeout Condition", testTimeoutCondition);
    runTest("Battery Without Extended Info", testBatteryWithoutExtendedInfo);

    std::cout << "\n=== Device Registry Tests ===" << std::endl;
    runTest("Registry Lookup", testDeviceRegistry);
    runTest("Registry Iteration", testDeviceRegistryIteration);

    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Library Linkage Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some library linkage tests failed");
    }
}

} // namespace headsetcontrol::testing

/**
 * @file test_library_api.cpp
 * @brief Tests for the high-level library API (headsetcontrol.hpp and headsetcontrol_c.h)
 *
 * These tests verify:
 * 1. C++ API functions that don't require hardware (version, supportedDevices, discover)
 * 2. C API functions that don't require hardware
 * 3. Both APIs are consistent with each other
 *
 * Note: discover() will return empty results without actual hardware,
 * but we test that it executes without crashing.
 */

#include "headsetcontrol.hpp"
#include "headsetcontrol_c.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
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

#define ASSERT_NOT_NULL(ptr, msg)                                               \
    do {                                                                        \
        if ((ptr) == nullptr) {                                                 \
            throw TestFailure(std::string("ASSERT_NOT_NULL failed: ") + (msg)); \
        }                                                                       \
    } while (0)

#define ASSERT_GT(a, b, msg)                             \
    do {                                                 \
        if (!((a) > (b))) {                              \
            std::ostringstream oss;                      \
            oss << "ASSERT_GT failed: " << (msg) << "\n" \
                << "  " << (a) << " is not > " << (b);   \
            throw TestFailure(oss.str());                \
        }                                                \
    } while (0)

// ============================================================================
// C++ API Tests
// ============================================================================

void testCppVersion()
{
    std::cout << "  Testing C++ version()..." << std::endl;

    auto ver = headsetcontrol::version();

    ASSERT_FALSE(ver.empty(), "Version should not be empty");
    ASSERT_TRUE(ver.length() > 0, "Version should have length > 0");

    std::cout << "    OK C++ version(): \"" << ver << "\"" << std::endl;
}

void testCppSupportedDevices()
{
    std::cout << "  Testing C++ supportedDevices()..." << std::endl;

    auto devices = headsetcontrol::supportedDevices();

    ASSERT_GT(devices.size(), 0u, "Should have at least one supported device");

    // Check that test device is in the list
    bool foundTestDevice = false;
    for (const auto& name : devices) {
        ASSERT_FALSE(name.empty(), "Device name should not be empty");
        if (name == "HeadsetControl Test device") {
            foundTestDevice = true;
        }
    }

    ASSERT_TRUE(foundTestDevice, "Should find HeadsetControl Test device");

    std::cout << "    OK supportedDevices(): " << devices.size() << " devices" << std::endl;
}

void testCppDiscover()
{
    std::cout << "  Testing C++ discover()..." << std::endl;

    // This will likely return empty without hardware, but shouldn't crash
    auto headsets = headsetcontrol::discover();

    // Just verify it returns a valid vector (may be empty)
    std::cout << "    OK discover(): " << headsets.size() << " headsets found" << std::endl;

    // If we found headsets, verify basic properties
    for (auto& headset : headsets) {
        auto name = headset.name();
        ASSERT_FALSE(name.empty(), "Headset name should not be empty");
        ASSERT_TRUE(headset.vendorId() != 0, "Headset should have vendor ID");
        ASSERT_TRUE(headset.productId() != 0, "Headset should have product ID");

        std::cout << "      Found: " << name
                  << " (0x" << std::hex << headset.vendorId()
                  << ":0x" << headset.productId() << std::dec << ")" << std::endl;
    }
}

void testCppDiscoverAll()
{
    std::cout << "  Testing C++ discoverAll()..." << std::endl;

    // This will likely return empty without hardware, but shouldn't crash
    auto headsets = headsetcontrol::discoverAll();

    std::cout << "    OK discoverAll(): " << headsets.size() << " headsets found" << std::endl;
}

// ============================================================================
// C API Tests
// ============================================================================

void testCVersion()
{
    std::cout << "  Testing C hsc_version()..." << std::endl;

    const char* ver = hsc_version();

    ASSERT_NOT_NULL(ver, "Version should not be null");
    ASSERT_GT(strlen(ver), 0u, "Version should have length > 0");

    std::cout << "    OK hsc_version(): \"" << ver << "\"" << std::endl;
}

void testCSupportedDeviceCount()
{
    std::cout << "  Testing C hsc_supported_device_count()..." << std::endl;

    int count = hsc_supported_device_count();

    ASSERT_GT(count, 0, "Should have at least one supported device");

    std::cout << "    OK hsc_supported_device_count(): " << count << " devices" << std::endl;
}

void testCSupportedDeviceName()
{
    std::cout << "  Testing C hsc_supported_device_name()..." << std::endl;

    int count = hsc_supported_device_count();

    // Test valid indices
    bool foundTestDevice = false;
    for (int i = 0; i < count; i++) {
        const char* name = hsc_supported_device_name(i);
        ASSERT_NOT_NULL(name, "Device name should not be null");
        ASSERT_GT(strlen(name), 0u, "Device name should not be empty");

        if (strcmp(name, "HeadsetControl Test device") == 0) {
            foundTestDevice = true;
        }
    }

    ASSERT_TRUE(foundTestDevice, "Should find HeadsetControl Test device");

    // Test invalid indices
    ASSERT_TRUE(hsc_supported_device_name(-1) == nullptr, "Negative index should return null");
    ASSERT_TRUE(hsc_supported_device_name(count) == nullptr, "Out of range index should return null");
    ASSERT_TRUE(hsc_supported_device_name(count + 100) == nullptr, "Far out of range should return null");

    std::cout << "    OK hsc_supported_device_name()" << std::endl;
}

void testCDiscover()
{
    std::cout << "  Testing C hsc_discover()..." << std::endl;

    hsc_headset_t* headsets = nullptr;
    int count               = hsc_discover(&headsets);

    // count >= 0 means success (0 = no devices found)
    ASSERT_TRUE(count >= 0, "hsc_discover should return >= 0");

    if (count > 0) {
        ASSERT_NOT_NULL(headsets, "Headsets should not be null when count > 0");

        // Test each headset
        for (int i = 0; i < count; i++) {
            const char* name = hsc_get_name(headsets[i]);
            ASSERT_NOT_NULL(name, "Headset name should not be null");

            uint16_t vid = hsc_get_vendor_id(headsets[i]);
            uint16_t pid = hsc_get_product_id(headsets[i]);

            std::cout << "      Found: " << name
                      << " (0x" << std::hex << vid << ":0x" << pid << std::dec << ")" << std::endl;

            // Test capabilities
            int caps = hsc_get_capabilities(headsets[i]);
            std::cout << "        Capabilities: 0x" << std::hex << caps << std::dec << std::endl;
        }

        // Free the headsets
        hsc_free_headsets(headsets, count);
    }

    std::cout << "    OK hsc_discover(): " << count << " headsets found" << std::endl;
}

void testCNullHandling()
{
    std::cout << "  Testing C API null handling..." << std::endl;

    // Test null headset handle
    ASSERT_TRUE(hsc_get_name(nullptr) == nullptr, "get_name(null) should return null");
    ASSERT_EQ(0u, hsc_get_vendor_id(nullptr), "get_vendor_id(null) should return 0");
    ASSERT_EQ(0u, hsc_get_product_id(nullptr), "get_product_id(null) should return 0");
    ASSERT_FALSE(hsc_supports(nullptr, HSC_CAP_BATTERY_STATUS), "supports(null) should return false");
    ASSERT_EQ(0, hsc_get_capabilities(nullptr), "get_capabilities(null) should return 0");

    // Test null output parameters
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_discover(nullptr), "discover(null) should return INVALID_PARAM");

    hsc_battery_t battery;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_battery(nullptr, &battery), "get_battery(null) should fail");

    hsc_chatmix_t chatmix;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_chatmix(nullptr, &chatmix), "get_chatmix(null) should fail");

    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_sidetone(nullptr, 64, nullptr), "set_sidetone(null) should fail");
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_lights(nullptr, true), "set_lights(null) should fail");

    std::cout << "    OK C API null handling" << std::endl;
}

// ============================================================================
// Test Device Mode Tests
// ============================================================================

void testCppTestDeviceMode()
{
    std::cout << "  Testing C++ test device mode..." << std::endl;

    // Initially should be disabled
    ASSERT_FALSE(headsetcontrol::isTestDeviceEnabled(), "Test device should be disabled initially");

    // Enable test device
    headsetcontrol::enableTestDevice(true);
    ASSERT_TRUE(headsetcontrol::isTestDeviceEnabled(), "Test device should be enabled after enableTestDevice(true)");

    // Discover should now find the test device
    auto headsets = headsetcontrol::discover();
    ASSERT_GT(headsets.size(), 0u, "Should find at least the test device");

    // Find the test device
    bool foundTestDevice = false;
    for (auto& headset : headsets) {
        if (headset.vendorId() == 0xF00B && headset.productId() == 0xA00C) {
            foundTestDevice = true;

            // Verify name
            ASSERT_EQ(std::string("HeadsetControl Test device"), std::string(headset.name()),
                "Test device name should match");

            // Verify capabilities
            ASSERT_TRUE(headset.supports(CAP_BATTERY_STATUS), "Test device should support battery");
            ASSERT_TRUE(headset.supports(CAP_SIDETONE), "Test device should support sidetone");
            ASSERT_TRUE(headset.supports(CAP_CHATMIX_STATUS), "Test device should support chatmix");

            // Test battery
            auto battery = headset.getBattery();
            ASSERT_TRUE(battery.hasValue(), "Battery should return success");
            ASSERT_EQ(42, battery->level_percent, "Battery level should be 42%");

            // Test sidetone
            auto sidetone = headset.setSidetone(64);
            ASSERT_TRUE(sidetone.hasValue(), "Sidetone should return success");
            ASSERT_EQ(64, sidetone->current_level, "Sidetone level should be 64");

            // Test chatmix
            auto chatmix = headset.getChatmix();
            ASSERT_TRUE(chatmix.hasValue(), "Chatmix should return success");
            ASSERT_EQ(64, chatmix->level, "Chatmix level should be 64");

            // Test equalizer info
            auto eq_info = headset.getEqualizerInfo();
            ASSERT_TRUE(eq_info.has_value(), "Equalizer info should be available");
            ASSERT_EQ(10, eq_info->bands_count, "Should have 10 bands");
            ASSERT_EQ(-12, eq_info->bands_min, "Min gain should be -12");
            ASSERT_EQ(12, eq_info->bands_max, "Max gain should be 12");

            // Test equalizer presets count
            ASSERT_EQ(4, headset.getEqualizerPresetsCount(), "Should have 4 presets");

            // Test equalizer preset
            ASSERT_TRUE(headset.supports(CAP_EQUALIZER_PRESET), "Test device should support equalizer preset");
            auto eq_preset = headset.setEqualizerPreset(2);
            ASSERT_TRUE(eq_preset.hasValue(), "Equalizer preset should return success");
            ASSERT_EQ(2, eq_preset->preset, "Preset should be 2");

            // Test custom equalizer
            ASSERT_TRUE(headset.supports(CAP_EQUALIZER), "Test device should support equalizer");
            EqualizerSettings eq_settings;
            eq_settings.bands = { 0, 2, 4, 2, 0, -2, -4, -2, 0, 2 };
            auto eq_result    = headset.setEqualizer(eq_settings);
            ASSERT_TRUE(eq_result.hasValue(), "Custom equalizer should return success");

            // Test parametric equalizer
            ASSERT_TRUE(headset.supports(CAP_PARAMETRIC_EQUALIZER), "Test device should support parametric EQ");
            ParametricEqualizerSettings peq_settings;
            peq_settings.bands.push_back({
                .frequency = 100,
                .gain      = 3.0f,
                .q_factor  = 1.0f,
                .type      = EqualizerFilterType::LowShelf
            });
            peq_settings.bands.push_back({
                .frequency = 1000,
                .gain      = 0.0f,
                .q_factor  = 1.414f,
                .type      = EqualizerFilterType::Peaking
            });
            auto peq_result = headset.setParametricEqualizer(peq_settings);
            ASSERT_TRUE(peq_result.hasValue(), "Parametric equalizer should return success");

            break;
        }
    }

    ASSERT_TRUE(foundTestDevice, "Should find test device in discover results");

    // Disable test device
    headsetcontrol::enableTestDevice(false);
    ASSERT_FALSE(headsetcontrol::isTestDeviceEnabled(), "Test device should be disabled");

    std::cout << "    OK C++ test device mode" << std::endl;
}

void testCTestDeviceMode()
{
    std::cout << "  Testing C test device mode..." << std::endl;

    // Initially should be disabled (we disabled it in previous test)
    ASSERT_FALSE(hsc_is_test_device_enabled(), "Test device should be disabled initially");

    // Enable test device
    hsc_enable_test_device(true);
    ASSERT_TRUE(hsc_is_test_device_enabled(), "Test device should be enabled");

    // Discover should now find the test device
    hsc_headset_t* headsets = nullptr;
    int count               = hsc_discover(&headsets);

    ASSERT_GT(count, 0, "Should find at least the test device");
    ASSERT_NOT_NULL(headsets, "Headsets should not be null");

    // Find and test the test device
    bool foundTestDevice = false;
    for (int i = 0; i < count; i++) {
        if (hsc_get_vendor_id(headsets[i]) == 0xF00B && hsc_get_product_id(headsets[i]) == 0xA00C) {
            foundTestDevice = true;

            // Verify name
            ASSERT_EQ(std::string("HeadsetControl Test device"),
                std::string(hsc_get_name(headsets[i])), "Test device name should match");

            // Verify capabilities
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_BATTERY_STATUS), "Should support battery");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_SIDETONE), "Should support sidetone");

            // Test battery
            hsc_battery_t battery;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_battery(headsets[i], &battery), "Battery should succeed");
            ASSERT_EQ(42, battery.level_percent, "Battery level should be 42%");

            // Test sidetone
            hsc_sidetone_t sidetone;
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_sidetone(headsets[i], 64, &sidetone), "Sidetone should succeed");
            ASSERT_EQ(64, sidetone.current_level, "Sidetone level should be 64");

            break;
        }
    }

    ASSERT_TRUE(foundTestDevice, "Should find test device");

    // Cleanup
    hsc_free_headsets(headsets, count);

    // Disable test device
    hsc_enable_test_device(false);
    ASSERT_FALSE(hsc_is_test_device_enabled(), "Test device should be disabled");

    std::cout << "    OK C test device mode" << std::endl;
}

// ============================================================================
// Cross-API Consistency Tests
// ============================================================================

void testVersionConsistency()
{
    std::cout << "  Testing version consistency between C++ and C APIs..." << std::endl;

    auto cpp_version      = headsetcontrol::version();
    const char* c_version = hsc_version();

    ASSERT_EQ(std::string(cpp_version), std::string(c_version),
        "C++ and C versions should match");

    std::cout << "    OK version consistency" << std::endl;
}

void testDeviceCountConsistency()
{
    std::cout << "  Testing device count consistency between C++ and C APIs..." << std::endl;

    auto cpp_devices = headsetcontrol::supportedDevices();
    int c_count      = hsc_supported_device_count();

    ASSERT_EQ(static_cast<int>(cpp_devices.size()), c_count,
        "C++ and C device counts should match");

    std::cout << "    OK device count consistency (" << c_count << " devices)" << std::endl;
}

void testDeviceNamesConsistency()
{
    std::cout << "  Testing device names consistency between C++ and C APIs..." << std::endl;

    auto cpp_devices = headsetcontrol::supportedDevices();
    int c_count      = hsc_supported_device_count();

    // Collect all C++ names
    std::set<std::string> cpp_names;
    for (const auto& name : cpp_devices) {
        cpp_names.insert(std::string(name));
    }

    // Collect all C names
    std::set<std::string> c_names;
    for (int i = 0; i < c_count; i++) {
        const char* name = hsc_supported_device_name(i);
        if (name) {
            c_names.insert(name);
        }
    }

    ASSERT_EQ(cpp_names.size(), c_names.size(), "Name sets should have same size");
    ASSERT_TRUE(cpp_names == c_names, "Name sets should be identical");

    std::cout << "    OK device names consistency" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllLibraryApiTests()
{
    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                    Library API Tests                              " << std::endl;
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

    std::cout << "=== C++ API Tests ===" << std::endl;
    runTest("C++ version()", testCppVersion);
    runTest("C++ supportedDevices()", testCppSupportedDevices);
    runTest("C++ discover()", testCppDiscover);
    runTest("C++ discoverAll()", testCppDiscoverAll);

    std::cout << "\n=== C API Tests ===" << std::endl;
    runTest("C hsc_version()", testCVersion);
    runTest("C hsc_supported_device_count()", testCSupportedDeviceCount);
    runTest("C hsc_supported_device_name()", testCSupportedDeviceName);
    runTest("C hsc_discover()", testCDiscover);
    runTest("C null handling", testCNullHandling);

    std::cout << "\n=== Cross-API Consistency Tests ===" << std::endl;
    runTest("Version consistency", testVersionConsistency);
    runTest("Device count consistency", testDeviceCountConsistency);
    runTest("Device names consistency", testDeviceNamesConsistency);

    std::cout << "\n=== Test Device Mode Tests ===" << std::endl;
    runTest("C++ test device mode", testCppTestDeviceMode);
    runTest("C test device mode", testCTestDeviceMode);

    std::cout << "\n-------------------------------------------------------------------" << std::endl;
    std::cout << "Library API Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some library API tests failed");
    }
}

} // namespace headsetcontrol::testing

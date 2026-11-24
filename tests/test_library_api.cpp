/**
 * @file test_library_api.cpp
 * @brief Tests for the high-level library API (headsetcontrol.hpp and headsetcontrol_c.h)
 *
 * These tests verify:
 * 1. C++ API functions that don't require hardware (version, supportedDevices, discover)
 * 2. C API functions that don't require hardware
 * 3. Both APIs are consistent with each other
 * 4. Product name refinement, against a mock device
 *
 * Note: discover() will return empty results without actual hardware,
 * but we test that it executes without crashing.
 */

#include "headsetcontrol.hpp"
#include "headsetcontrol_c.h"
#include "product_name.hpp"

#include <algorithm>
#include <array>
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
    ASSERT_TRUE(hsc_get_vendor_name(nullptr) == nullptr, "get_vendor_name(null) should return null");
    ASSERT_EQ(0u, hsc_get_product_id(nullptr), "get_product_id(null) should return 0");
    ASSERT_TRUE(hsc_get_product_name(nullptr) == nullptr, "get_product_name(null) should return null");
    ASSERT_FALSE(hsc_supports(nullptr, HSC_CAP_BATTERY_STATUS), "supports(null) should return false");
    ASSERT_EQ(0, hsc_get_capabilities(nullptr), "get_capabilities(null) should return 0");

    // Test null output parameters
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_discover(nullptr), "discover(null) should return INVALID_PARAM");

    hsc_battery_t battery;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_battery(nullptr, &battery), "get_battery(null) should fail");

    hsc_chatmix_t chatmix;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_chatmix(nullptr, &chatmix), "get_chatmix(null) should fail");

    hsc_sidetone_status_t sidetone_status;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_sidetone(nullptr, &sidetone_status), "get_sidetone(null) should fail");

    hsc_mic_mute_status_t mic_mute_status;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_mic_mute_status(nullptr, &mic_mute_status), "get_mic_mute_status(null) should fail");

    hsc_mic_attachment_status_t mic_attachment_status;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_mic_attachment_status(nullptr, &mic_attachment_status), "get_mic_attachment_status(null) should fail");

    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_anc(nullptr, 1), "set_anc(null) should fail");
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_anc_startup_mode(nullptr, 1), "set_anc_startup_mode(null) should fail");
    hsc_anc_button_modes_t anc_button_modes = { .off = true, .anc = true, .ambient = false };
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_anc_button_modes(nullptr, &anc_button_modes), "set_anc_button_modes(null) should fail");
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_sidetone(nullptr, 64, nullptr), "set_sidetone(null) should fail");
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_lights(nullptr, true), "set_lights(null) should fail");
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_noise_filter(nullptr, 1), "set_noise_filter(null) should fail");

    hsc_equalizer_info_t eq_info;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_equalizer_info(nullptr, &eq_info), "get_equalizer_info(null) should fail");
    hsc_parametric_equalizer_info_t peq_info;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_parametric_equalizer_info(nullptr, &peq_info), "get_parametric_equalizer_info(null) should fail");
    hsc_parametric_band_t peq_band = { .frequency = 1000.0f, .gain = 0.0f, .q_factor = 1.0f, .type = HSC_EQ_FILTER_PEAKING };
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_parametric_equalizer(nullptr, &peq_band, 1), "set_parametric_equalizer(null) should fail");

    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_headset_volume(nullptr, 64, nullptr), "set_headset_volume(null) should fail");
    hsc_headset_volume_t headset_volume;
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_headset_volume(nullptr, &headset_volume), "get_headset_volume(null) should fail");
    ASSERT_EQ(0, hsc_get_readable_capabilities(nullptr), "get_readable_capabilities(null) should be 0");

    ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_light_color(nullptr, 0, 0, 0), "set_light_color(null) should fail");

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
            ASSERT_EQ(std::string("HeadsetControl"), std::string(headset.vendorName()),
                "Test device vendor name should match");
            ASSERT_EQ(std::string("HeadsetControl Test device"), std::string(headset.productName()),
                "Test device product name should match");

            // Verify capabilities
            ASSERT_TRUE(headset.supports(CAP_BATTERY_STATUS), "Test device should support battery");
            ASSERT_TRUE(headset.supports(CAP_SIDETONE), "Test device should support sidetone");
            ASSERT_TRUE(headset.supports(CAP_CHATMIX_STATUS), "Test device should support chatmix");
            ASSERT_TRUE(headset.supports(CAP_SIDETONE_STATUS), "Test device should support sidetone status");
            ASSERT_TRUE(headset.supports(CAP_LIGHT_COLOR), "Test device should support light color");

            // Test light color
            auto color = headset.setLightColor(LightColorSettings { .r = 0x12, .g = 0x34, .b = 0x56 });
            ASSERT_TRUE(color.hasValue(), "Light color should return success");
            ASSERT_EQ(0x12, color->color.r, "Red should be echoed");
            ASSERT_EQ(0x34, color->color.g, "Green should be echoed");
            ASSERT_EQ(0x56, color->color.b, "Blue should be echoed");

            // Test battery
            auto battery = headset.getBattery();
            ASSERT_TRUE(battery.hasValue(), "Battery should return success");
            ASSERT_EQ(42, battery->level_percent, "Battery level should be 42%");

            // Test sidetone
            auto sidetone = headset.setSidetone(64);
            ASSERT_TRUE(sidetone.hasValue(), "Sidetone should return success");
            ASSERT_EQ(64, sidetone->current_level, "Sidetone level should be 64");

            auto sidetone_status = headset.getSidetone();
            ASSERT_TRUE(sidetone_status.hasValue(), "Sidetone status should return success");
            ASSERT_EQ(85, sidetone_status->current_level, "Sidetone status should be 85");
            ASSERT_EQ(2, sidetone_status->device_level, "Native sidetone level should be 2");

            // Test headset volume
            ASSERT_TRUE(headset.supports(CAP_HEADSET_VOLUME), "Test device should support headset volume");

            auto volume = headset.setHeadsetVolume(64);
            ASSERT_TRUE(volume.hasValue(), "Headset volume set should return success");
            ASSERT_EQ(64, volume->volume, "Headset volume should be 64");
            ASSERT_EQ(21, volume->device_max, "Native volume max should be 21");
            ASSERT_EQ(12, volume->device_volume, "Native volume level should be 12");

            ASSERT_TRUE(headset.canRead(CAP_HEADSET_VOLUME), "Test device should read back headset volume");
            ASSERT_FALSE(headset.canRead(CAP_LIGHTS), "Test device can't read back lights");
            ASSERT_EQ(B(CAP_SIDETONE) | B(CAP_HEADSET_VOLUME), headset.readableCapabilitiesMask(),
                "Readable mask should be sidetone and headset volume");

            auto volume_read = headset.getHeadsetVolume();
            ASSERT_TRUE(volume_read.hasValue(), "Headset volume read should return success");
            ASSERT_EQ(77, volume_read->volume, "Headset volume read should be 77");
            ASSERT_EQ(15, volume_read->device_volume, "Native volume read should be 15");

            // Profile 6: volume stays settable but can't be read back
            headsetcontrol::setTestProfile(6);
            ASSERT_TRUE(headset.supports(CAP_HEADSET_VOLUME), "Profile 6 should still support headset volume");
            ASSERT_FALSE(headset.canRead(CAP_HEADSET_VOLUME), "Profile 6 can't read back headset volume");
            ASSERT_TRUE(headset.setHeadsetVolume(64).hasValue(), "Profile 6 should still set headset volume");

            auto volume_write_only = headset.getHeadsetVolume();
            ASSERT_TRUE(volume_write_only.hasError(), "Profile 6 headset volume read should fail");
            ASSERT_TRUE(volume_write_only.error().code == DeviceError::Code::NotSupported,
                "Profile 6 headset volume read should be not supported");
            headsetcontrol::setTestProfile(0);

            // The normalised 0-128 setters are reachable without the CLI's parsing,
            // so they have to reject out-of-range levels instead of letting the
            // device scale them into maximum volume.
            auto volume_max = headset.setHeadsetVolume(128);
            ASSERT_TRUE(volume_max.hasValue(), "Headset volume 128 should be accepted");

            auto volume_high = headset.setHeadsetVolume(129);
            ASSERT_TRUE(volume_high.hasError(), "Headset volume 129 should be rejected");
            ASSERT_TRUE(volume_high.error().code == DeviceError::Code::InvalidParameter,
                "Headset volume 129 should be an invalid parameter");

            ASSERT_TRUE(headset.setSidetone(255).hasError(), "Sidetone 255 should be rejected");
            ASSERT_TRUE(headset.setMicVolume(200).hasError(), "Microphone volume 200 should be rejected");

            // Test noise filter
            ASSERT_TRUE(headset.supports(CAP_NOISE_FILTER), "Test device should support noise filter");
            auto noise_filter = headset.setNoiseFilter(2);
            ASSERT_TRUE(noise_filter.hasValue(), "Noise filter should return success");
            ASSERT_EQ(2, noise_filter->level, "Noise filter level should be 2");

            // The test device accepts any level, so the 0-2 bound has to come from the library
            auto noise_filter_high = headset.setNoiseFilter(3);
            ASSERT_TRUE(noise_filter_high.hasError(), "Noise filter 3 should be rejected");
            ASSERT_TRUE(noise_filter_high.error().code == DeviceError::Code::InvalidParameter,
                "Noise filter 3 should be an invalid parameter");

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

            auto presets = headset.getEqualizerPresets();
            ASSERT_TRUE(presets.has_value(), "Equalizer presets should be available");
            ASSERT_EQ(4, presets->count(), "Should return 4 equalizer presets");
            ASSERT_EQ(std::string("Flat"), presets->presets[0].name, "First preset name should match");
            ASSERT_EQ(10u, presets->presets[0].values.size(), "Flat preset should have 10 bands");
            ASSERT_EQ(0.0f, presets->presets[0].values[0], "Flat preset first band should be 0");
            ASSERT_EQ(std::string("Bass Boost"), presets->presets[1].name, "Second preset name should match");
            ASSERT_EQ(6.0f, presets->presets[1].values[0], "Bass Boost first band should match");

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
            peq_settings.bands.push_back({ .frequency = 100,
                .gain                                 = 3.0f,
                .q_factor                             = 1.0f,
                .type                                 = EqualizerFilterType::LowShelf });
            peq_settings.bands.push_back({ .frequency = 1000,
                .gain                                 = 0.0f,
                .q_factor                             = 1.414f,
                .type                                 = EqualizerFilterType::Peaking });
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
            ASSERT_EQ(std::string("HeadsetControl"),
                std::string(hsc_get_vendor_name(headsets[i])), "Test device vendor name should match");
            ASSERT_EQ(std::string("HeadsetControl Test device"),
                std::string(hsc_get_product_name(headsets[i])), "Test device product name should match");

            // Verify capabilities
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_BATTERY_STATUS), "Should support battery");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_SIDETONE), "Should support sidetone");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_SIDETONE_STATUS), "Should support sidetone status");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_ANC), "Should support ANC");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_ANC_STARTUP_MODE), "Should support ANC startup mode");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_ANC_BUTTON_MODES), "Should support ANC button modes");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_MICROPHONE_ATTACHMENT_STATUS), "Should support microphone attachment status");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_MICROPHONE_MUTE_STATUS), "Should support microphone mute status");
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_LIGHT_COLOR), "Should support light color");

            // Test light color, including black
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_light_color(headsets[i], 0xff, 0x80, 0x00), "Light color should succeed");
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_light_color(headsets[i], 0, 0, 0), "Black should succeed");

            // Test battery
            hsc_battery_t battery;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_battery(headsets[i], &battery), "Battery should succeed");
            ASSERT_EQ(42, battery.level_percent, "Battery level should be 42%");
            ASSERT_EQ(HSC_BATTERY_AVAILABLE, battery.status, "Battery status should be available");

            // Profile 2 reports a charging battery
            hsc_set_test_profile(2);
            hsc_battery_t charging;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_battery(headsets[i], &charging), "Charging battery should succeed");
            ASSERT_EQ(HSC_BATTERY_CHARGING, charging.status, "Battery status should be charging");
            hsc_set_test_profile(0);

            // Test sidetone
            hsc_sidetone_t sidetone;
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_sidetone(headsets[i], 64, &sidetone), "Sidetone should succeed");
            ASSERT_EQ(64, sidetone.current_level, "Sidetone level should be 64");

            hsc_sidetone_status_t sidetone_status;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_sidetone(headsets[i], &sidetone_status), "Sidetone status should succeed");
            ASSERT_EQ(85, sidetone_status.current_level, "Sidetone status should be 85");
            ASSERT_EQ(2, sidetone_status.device_level, "Native sidetone level should be 2");
            ASSERT_NOT_NULL(sidetone_status.level_name, "Sidetone name should be available");
            ASSERT_EQ(std::string("Medium"), std::string(sidetone_status.level_name), "Sidetone name should match");

            hsc_mic_mute_status_t mic_mute_status;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_mic_mute_status(headsets[i], &mic_mute_status), "Mic mute status should succeed");
            ASSERT_FALSE(mic_mute_status.muted, "Mic mute status should be unmuted");

            hsc_mic_attachment_status_t mic_attachment_status;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_mic_attachment_status(headsets[i], &mic_attachment_status), "Mic attachment status should succeed");
            ASSERT_TRUE(mic_attachment_status.attached, "Mic should be attached");

            ASSERT_EQ(HSC_RESULT_OK, hsc_set_anc(headsets[i], 2), "ANC should succeed");
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_anc_startup_mode(headsets[i], 3), "ANC startup mode should succeed");
            hsc_anc_button_modes_t anc_button_modes = { .off = true, .anc = true, .ambient = false };
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_anc_button_modes(headsets[i], &anc_button_modes), "ANC button modes should succeed");
            hsc_anc_button_modes_t no_modes = { .off = false, .anc = false, .ambient = false };
            ASSERT_TRUE(HSC_RESULT_OK != hsc_set_anc_button_modes(headsets[i], &no_modes), "ANC button modes with no mode should fail");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_anc_button_modes(headsets[i], nullptr), "ANC button modes with null modes should fail");

            // Test headset volume
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_HEADSET_VOLUME), "Should support headset volume");

            hsc_headset_volume_t volume;
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_headset_volume(headsets[i], 64, &volume), "Headset volume set should succeed");
            ASSERT_EQ(64, volume.volume, "Headset volume should be 64");
            ASSERT_EQ(12, volume.device_volume, "Native volume level should be 12");
            ASSERT_EQ(21, volume.device_max, "Native volume max should be 21");
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_headset_volume(headsets[i], 64, NULL), "Headset volume set with NULL result should succeed");
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_headset_volume(headsets[i], 128, NULL), "Headset volume 128 should be accepted");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_headset_volume(headsets[i], 129, NULL), "Headset volume 129 should be rejected");

            ASSERT_TRUE((hsc_get_readable_capabilities(headsets[i]) & (1 << HSC_CAP_HEADSET_VOLUME)) != 0,
                "Headset volume should be readable");
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_headset_volume(headsets[i], &volume), "Headset volume read should succeed");
            ASSERT_EQ(77, volume.volume, "Headset volume read should be 77");
            ASSERT_EQ(15, volume.device_volume, "Native volume read should be 15");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_headset_volume(headsets[i], NULL), "Headset volume read with NULL should fail");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_sidetone(headsets[i], 255, NULL), "Sidetone 255 should be rejected");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_mic_volume(headsets[i], 200), "Microphone volume 200 should be rejected");

            // Test noise filter
            ASSERT_TRUE(hsc_supports(headsets[i], HSC_CAP_NOISE_FILTER), "Should support noise filter");
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_noise_filter(headsets[i], 1), "Noise filter should succeed");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_noise_filter(headsets[i], 3), "Noise filter 3 should be rejected");

            ASSERT_EQ(4, hsc_get_equalizer_presets_count(headsets[i]), "Preset count should be 4");
            ASSERT_EQ(std::string("Flat"), std::string(hsc_get_equalizer_preset_name(headsets[i], 0)), "Flat preset name should match");
            ASSERT_EQ(10, hsc_get_equalizer_preset_band_count(headsets[i], 0), "Flat preset should have 10 bands");

            std::array<float, 10> preset_bands {};
            ASSERT_EQ(HSC_RESULT_OK,
                hsc_get_equalizer_preset_bands(headsets[i], 1, preset_bands.data(), static_cast<int>(preset_bands.size())),
                "Fetching preset bands should succeed");
            ASSERT_EQ(6.0f, preset_bands[0], "Bass Boost first band should match");
            ASSERT_EQ(4.0f, preset_bands[1], "Bass Boost second band should match");

            // Test equalizer layouts
            hsc_equalizer_info_t eq_info;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_equalizer_info(headsets[i], &eq_info), "Equalizer info should succeed");
            ASSERT_EQ(10, eq_info.bands_count, "Equalizer should have 10 bands");
            ASSERT_EQ(0, eq_info.bands_baseline, "Equalizer baseline should be 0");
            ASSERT_EQ(0.5f, eq_info.bands_step, "Equalizer step should be 0.5");
            ASSERT_EQ(-12, eq_info.bands_min, "Equalizer min should be -12");
            ASSERT_EQ(12, eq_info.bands_max, "Equalizer max should be 12");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_get_equalizer_info(headsets[i], nullptr), "Equalizer info with null output should fail");

            hsc_parametric_equalizer_info_t peq_info;
            ASSERT_EQ(HSC_RESULT_OK, hsc_get_parametric_equalizer_info(headsets[i], &peq_info), "Parametric equalizer info should succeed");
            ASSERT_EQ(10, peq_info.bands_count, "Parametric equalizer should have 10 bands");
            ASSERT_EQ(-12.0f, peq_info.gain_min, "Parametric gain min should be -12");
            ASSERT_EQ(12.0f, peq_info.gain_max, "Parametric gain max should be 12");
            ASSERT_EQ(20, peq_info.freq_min, "Parametric frequency min should be 20");
            ASSERT_EQ(20000, peq_info.freq_max, "Parametric frequency max should be 20000");
            ASSERT_TRUE((peq_info.filter_types & (1 << HSC_EQ_FILTER_PEAKING)) != 0, "Peaking filter should be supported");
            ASSERT_TRUE((peq_info.filter_types & (1 << HSC_EQ_FILTER_NOTCH)) == 0, "Notch filter should not be supported");

            // Test parametric equalizer
            hsc_parametric_band_t peq_bands[] = {
                { .frequency = 100.0f, .gain = 3.0f, .q_factor = 0.7f, .type = HSC_EQ_FILTER_LOW_SHELF },
                { .frequency = 1000.0f, .gain = -2.0f, .q_factor = 1.414f, .type = HSC_EQ_FILTER_PEAKING },
            };
            ASSERT_EQ(HSC_RESULT_OK, hsc_set_parametric_equalizer(headsets[i], peq_bands, 2), "Parametric equalizer should succeed");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_parametric_equalizer(headsets[i], peq_bands, 0), "Parametric equalizer with no bands should fail");
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_parametric_equalizer(headsets[i], nullptr, 2), "Parametric equalizer with null bands should fail");
            hsc_parametric_band_t bad_type = { .frequency = 1000.0f, .gain = 0.0f, .q_factor = 1.0f, .type = static_cast<hsc_eq_filter_type_t>(6) };
            ASSERT_EQ(HSC_RESULT_INVALID_PARAM, hsc_set_parametric_equalizer(headsets[i], &bad_type, 1), "Unknown filter type should be rejected");

            // Profile 10 drops both equalizers from the test device
            hsc_set_test_profile(10);
            ASSERT_EQ(HSC_RESULT_NOT_SUPPORTED, hsc_get_equalizer_info(headsets[i], &eq_info), "Equalizer info should be unsupported in profile 10");
            ASSERT_EQ(HSC_RESULT_NOT_SUPPORTED, hsc_get_parametric_equalizer_info(headsets[i], &peq_info), "Parametric equalizer info should be unsupported in profile 10");
            hsc_set_test_profile(0);

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

void testVendorProductNames()
{
    std::cout << "  Testing vendor/product name accessors..." << std::endl;

    // Enable test device for name verification
    headsetcontrol::enableTestDevice(true);

    // C++ API: verify test device names are non-empty and correct
    auto headsets        = headsetcontrol::discover();
    bool foundTestDevice = false;
    for (auto& headset : headsets) {
        if (headset.vendorId() == 0xF00B && headset.productId() == 0xA00C) {
            foundTestDevice = true;

            ASSERT_FALSE(headset.vendorName().empty(), "Test device vendorName() should not be empty");
            ASSERT_FALSE(headset.productName().empty(), "Test device productName() should not be empty");

            // Verify C and C++ APIs return consistent values
            break;
        }
    }
    ASSERT_TRUE(foundTestDevice, "Should find test device");

    // C API: verify test device names are non-null and match C++ values
    hsc_headset_t* c_headsets = nullptr;
    int count                 = hsc_discover(&c_headsets);
    for (int i = 0; i < count; i++) {
        if (hsc_get_vendor_id(c_headsets[i]) == 0xF00B && hsc_get_product_id(c_headsets[i]) == 0xA00C) {
            const char* vendor_name  = hsc_get_vendor_name(c_headsets[i]);
            const char* product_name = hsc_get_product_name(c_headsets[i]);

            ASSERT_NOT_NULL(vendor_name, "C API vendor_name should not be null for test device");
            ASSERT_NOT_NULL(product_name, "C API product_name should not be null for test device");

            // Verify C and C++ values match
            for (auto& headset : headsets) {
                if (headset.vendorId() == 0xF00B) {
                    ASSERT_EQ(std::string(headset.vendorName()), std::string(vendor_name),
                        "C and C++ vendor names should match");
                    ASSERT_EQ(std::string(headset.productName()), std::string(product_name),
                        "C and C++ product names should match");
                    break;
                }
            }
            break;
        }
    }
    hsc_free_headsets(c_headsets, count);

    headsetcontrol::enableTestDevice(false);

    std::cout << "    OK vendor/product name accessors" << std::endl;
}

void testEqualizerPresetConsistency()
{
    std::cout << "  Testing equalizer preset consistency between C++ and C APIs..." << std::endl;

    headsetcontrol::enableTestDevice(true);

    auto cpp_headsets         = headsetcontrol::discover();
    hsc_headset_t* c_headsets = nullptr;
    int c_count               = hsc_discover(&c_headsets);

    Headset* cpp_test_device = nullptr;
    for (auto& headset : cpp_headsets) {
        if (headset.vendorId() == 0xF00B && headset.productId() == 0xA00C) {
            cpp_test_device = &headset;
            break;
        }
    }

    hsc_headset_t c_test_device = nullptr;
    for (int i = 0; i < c_count; i++) {
        if (hsc_get_vendor_id(c_headsets[i]) == 0xF00B && hsc_get_product_id(c_headsets[i]) == 0xA00C) {
            c_test_device = c_headsets[i];
            break;
        }
    }

    ASSERT_TRUE(cpp_test_device != nullptr, "Should find C++ test device");
    ASSERT_TRUE(c_test_device != nullptr, "Should find C test device");

    auto cpp_presets = cpp_test_device->getEqualizerPresets();
    ASSERT_TRUE(cpp_presets.has_value(), "C++ equalizer presets should be available");
    ASSERT_EQ(cpp_test_device->getEqualizerPresetsCount(), hsc_get_equalizer_presets_count(c_test_device),
        "Preset counts should match");

    for (int preset_index = 0; preset_index < cpp_presets->count(); preset_index++) {
        const auto& cpp_preset = cpp_presets->presets[static_cast<size_t>(preset_index)];
        ASSERT_EQ(cpp_preset.name, std::string(hsc_get_equalizer_preset_name(c_test_device, preset_index)),
            "Preset names should match");
        ASSERT_EQ(static_cast<int>(cpp_preset.values.size()), hsc_get_equalizer_preset_band_count(c_test_device, preset_index),
            "Preset band counts should match");

        std::vector<float> c_values(cpp_preset.values.size());
        ASSERT_EQ(HSC_RESULT_OK,
            hsc_get_equalizer_preset_bands(c_test_device, preset_index, c_values.data(), static_cast<int>(c_values.size())),
            "Preset bands fetch should succeed");
        ASSERT_TRUE(c_values == cpp_preset.values, "Preset bands should match");
    }

    ASSERT_TRUE(hsc_get_equalizer_preset_name(c_test_device, -1) == nullptr, "Negative preset index should return null");
    ASSERT_EQ(0, hsc_get_equalizer_preset_band_count(c_test_device, 999), "Out-of-range preset band count should be 0");
    std::array<float, 2> short_buffer {};
    ASSERT_EQ(HSC_RESULT_INVALID_PARAM,
        hsc_get_equalizer_preset_bands(c_test_device, 0, short_buffer.data(), static_cast<int>(short_buffer.size())),
        "Too-small preset band buffer should fail");

    hsc_free_headsets(c_headsets, c_count);
    headsetcontrol::enableTestDevice(false);

    std::cout << "    OK equalizer preset consistency" << std::endl;
}

// ============================================================================
// Product Name Refinement Tests
// ============================================================================

// The test device opens no HID handle, so a device that reports its own name
// stands in for e.g. a Jabra Link dongle naming the headset paired to it
class MetadataDevice : public HIDDevice {
public:
    uint16_t getVendorId() const override { return 0x1234; }
    std::vector<uint16_t> getProductIds() const override { return { 0x5678 }; }
    std::string_view getDeviceName() const override { return "Metadata Device"; }
    int getCapabilities() const override { return 0; }

    Result<DeviceMetadata> getMetadata([[maybe_unused]] hid_device* device_handle) override
    {
        calls++;
        if (fail) {
            return DeviceError::timeout("No answer");
        }
        return DeviceMetadata {
            .name       = getDeviceName(),
            .vendor_id  = getVendorId(),
            .product_id = 0x5678,
            .product    = product,
        };
    }

    std::string product;
    bool fail = false;
    int calls = 0;
};

void testProductNameRefined()
{
    std::cout << "  Testing product name refinement..." << std::endl;

    MetadataDevice device;
    device.product = "Paired Headset";

    ProductName name("USB Dongle With A Name Too Long For SSO");
    std::string_view before = name.get();
    ASSERT_EQ(std::string("USB Dongle With A Name Too Long For SSO"), std::string(before),
        "Name should start as the enumeration name");

    name.refine(device, nullptr);
    ASSERT_EQ(std::string("Paired Headset"), std::string(name.get()), "Name should be the refined one");
    ASSERT_EQ(std::string("USB Dongle With A Name Too Long For SSO"), std::string(before),
        "A view taken before refinement should keep the enumeration name");

    std::cout << "    OK product name refinement" << std::endl;
}

void testProductNameRefinedOnce()
{
    std::cout << "  Testing product name is refined once..." << std::endl;

    MetadataDevice device;
    device.product = "First Headset";

    ProductName name("USB Dongle");
    name.refine(device, nullptr);
    std::string_view after = name.get();

    device.product = "Second Headset";
    name.refine(device, nullptr);

    ASSERT_EQ(1, device.calls, "Metadata should be queried once");
    ASSERT_EQ(std::string("First Headset"), std::string(name.get()), "A later refine should not change the name");
    ASSERT_EQ(std::string("First Headset"), std::string(after), "A view taken after refinement should stay valid");

    std::cout << "    OK product name is refined once" << std::endl;
}

void testProductNameEmptyRefinement()
{
    std::cout << "  Testing empty refined product name..." << std::endl;

    MetadataDevice device;

    ProductName name("USB Dongle");
    name.refine(device, nullptr);
    ASSERT_EQ(std::string("USB Dongle"), std::string(name.get()), "An empty name should keep the enumeration name");

    device.product = "Paired Headset";
    name.refine(device, nullptr);
    ASSERT_EQ(1, device.calls, "An empty answer should still count as the one query");
    ASSERT_EQ(std::string("USB Dongle"), std::string(name.get()), "The enumeration name should stay");

    std::cout << "    OK empty refined product name" << std::endl;
}

void testProductNameRefinementError()
{
    std::cout << "  Testing product name refinement error..." << std::endl;

    MetadataDevice device;
    device.product = "Paired Headset";
    device.fail    = true;

    ProductName name("USB Dongle");
    name.refine(device, nullptr);
    ASSERT_EQ(std::string("USB Dongle"), std::string(name.get()), "An error should keep the enumeration name");

    device.fail = false;
    name.refine(device, nullptr);
    ASSERT_EQ(1, device.calls, "A failed query should not be retried");
    ASSERT_EQ(std::string("USB Dongle"), std::string(name.get()), "The enumeration name should stay");

    std::cout << "    OK product name refinement error" << std::endl;
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
    runTest("Vendor/product names", testVendorProductNames);
    runTest("Equalizer preset consistency", testEqualizerPresetConsistency);

    std::cout << "\n=== Test Device Mode Tests ===" << std::endl;
    runTest("C++ test device mode", testCppTestDeviceMode);
    runTest("C test device mode", testCTestDeviceMode);

    std::cout << "\n=== Product Name Refinement Tests ===" << std::endl;
    runTest("Product name refined", testProductNameRefined);
    runTest("Product name refined once", testProductNameRefinedOnce);
    runTest("Product name empty refinement", testProductNameEmptyRefinement);
    runTest("Product name refinement error", testProductNameRefinementError);

    std::cout << "\n-------------------------------------------------------------------" << std::endl;
    std::cout << "Library API Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some library API tests failed");
    }
}

} // namespace headsetcontrol::testing

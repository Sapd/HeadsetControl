#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

// External variable for test profile (defined in main.cpp)
extern int test_profile;

namespace headsetcontrol {

/**
 * @brief HeadsetControl Test Device
 *
 * A synthetic device that implements ALL capabilities for testing purposes.
 * Uses vendor ID 0xF00B, product ID 0xA00C.
 *
 * Supports multiple test profiles (0-10) to simulate different scenarios:
 * - Profile 0: Normal operation
 * - Profile 1: Error conditions
 * - Profile 2: Charging state
 * - Profile 3: Different battery level
 * - Profile 4: Unavailable battery
 * - Profile 5: Timeout conditions
 * - Profile 6-9: Reserved
 * - Profile 10: Limited capabilities (sidetone, lights, battery only)
 */
class HeadsetControlTest : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0xA00C // Test device
    };

    static constexpr int TESTBYTES_SEND = 32;

    static constexpr int EQUALIZER_BANDS      = 10;
    static constexpr float EQUALIZER_BAND_MIN = -10.0f;
    static constexpr float EQUALIZER_BAND_MAX = 10.0f;

    constexpr uint16_t getVendorId() const override
    {
        return 0xF00B; // Test device
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HeadsetControl Test device"sv;
    }

    int getCapabilities() const override
    {
        // Profile 10 has limited capabilities
        if (test_profile == 10) {
            return B(CAP_SIDETONE) | B(CAP_LIGHTS) | B(CAP_BATTERY_STATUS);
        }

        // Full capabilities for all other profiles
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_NOTIFICATION_SOUND)
            | B(CAP_LIGHTS) | B(CAP_INACTIVE_TIME) | B(CAP_CHATMIX_STATUS)
            | B(CAP_VOICE_PROMPTS) | B(CAP_ROTATE_TO_MUTE) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME)
            | B(CAP_VOLUME_LIMITER) | B(CAP_BT_WHEN_POWERED_ON) | B(CAP_BT_CALL_VOLUME);
    }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        return EqualizerInfo {
            .bands_count    = 10,
            .bands_baseline = 0,
            .bands_step     = 0.5f,
            .bands_min      = -12,
            .bands_max      = 12
        };
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return 4;
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {

        BatteryResult result {};

        switch (test_profile) {
        case 0: // Normal battery with extended info
            result.level_percent     = 42;
            result.status            = BATTERY_AVAILABLE;
            result.voltage_mv        = 3650; // Typical lithium-ion voltage
            result.time_to_empty_min = (42 * 720) / 100; // ~5 hours remaining
            break;
        case 1:
            return DeviceError::hidError("Test error condition");
        case 2: // Charging with time to full
            result.level_percent    = 50;
            result.status           = BATTERY_CHARGING;
            result.voltage_mv       = 3800; // Charging voltage
            result.time_to_full_min = ((100 - 50) * 120) / 100; // ~60 min to full
            break;
        case 3: // Battery without extended info (legacy behavior)
            result.level_percent = 64;
            result.status        = BATTERY_AVAILABLE;
            // No optional fields populated
            break;
        case 4:
            return DeviceError::deviceOffline("Test unavailable");
        case 5:
            return DeviceError::timeout("Test timeout");
        case 6: // Full battery
            result.level_percent     = 100;
            result.status            = BATTERY_AVAILABLE;
            result.voltage_mv        = 4200; // Full charge voltage
            result.time_to_empty_min = 720; // ~12 hours at full
            break;
        case 7: // Low battery
            result.level_percent     = 10;
            result.status            = BATTERY_AVAILABLE;
            result.voltage_mv        = 3400; // Low voltage
            result.time_to_empty_min = 72; // ~1.2 hours remaining
            break;
        default:
            result.level_percent = 42;
            result.status        = BATTERY_AVAILABLE;
            break;
        }

        return result;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {

        if (test_profile == 1) {
            return DeviceError::hidError("Test error condition");
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        return LightsResult { .enabled = on };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        if (test_profile == 1) {
            return DeviceError::hidError("Test error condition");
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    Result<RotateToMuteResult> setRotateToMute(hid_device* device_handle, bool enabled) override
    {
        return RotateToMuteResult { .enabled = enabled };
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        return MicVolumeResult { .volume = volume, .min_volume = 0, .max_volume = 100 };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        return MicMuteLedBrightnessResult { .brightness = brightness, .min_brightness = 0, .max_brightness = 100 };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        return VolumeLimiterResult { .enabled = enabled };
    }

    Result<BluetoothWhenPoweredOnResult> setBluetoothWhenPoweredOn(hid_device* device_handle, bool enabled) override
    {
        return BluetoothWhenPoweredOnResult { .enabled = enabled };
    }

    Result<BluetoothCallVolumeResult> setBluetoothCallVolume(hid_device* device_handle, uint8_t volume) override
    {
        return BluetoothCallVolumeResult { .volume = volume, .min_volume = 0, .max_volume = 100 };
    }

    Result<NotificationSoundResult> notificationSound(hid_device* device_handle, uint8_t sound_id) override
    {
        return NotificationSoundResult { .sound_id = sound_id };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        if (test_profile == 1) {
            return DeviceError::hidError("Test error condition");
        }

        return ChatmixResult {
            .level               = 64, // 50% game, 50% chat
            .game_volume_percent = 50,
            .chat_volume_percent = 50
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        if (test_profile == 1) {
            return DeviceError::hidError("Test error condition");
        }

        return EqualizerPresetResult { .preset = preset };
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (test_profile == 1) {
            return DeviceError::hidError("Test error condition");
        }

        return EqualizerResult {}; // Empty struct - presence indicates success
    }

    Result<ParametricEqualizerResult> setParametricEqualizer(hid_device* device_handle, const ParametricEqualizerSettings& settings) override
    {
        if (test_profile == 1) {
            return DeviceError::hidError("Test error condition");
        }

        return ParametricEqualizerResult {}; // Empty struct - presence indicates success
    }
};
} // namespace headsetcontrol

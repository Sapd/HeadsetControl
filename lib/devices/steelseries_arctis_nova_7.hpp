#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova 7/7X Gaming Headset
 *
 * Features:
 * - Sidetone (4 levels)
 * - Battery status
 * - Chatmix
 * - Inactive time
 * - Equalizer (10 bands)
 * - Microphone mute LED (4 levels)
 * - Microphone volume (8 levels)
 * - Volume limiter
 * - Bluetooth settings
 *
 * Note: Nova 7P (0x220a) is handled by SteelSeriesArctisNova7P due to
 * different hardware behavior (no working sidetone/chatmix).
 */
class SteelSeriesArctisNova7 : public protocols::SteelSeriesNovaDevice<SteelSeriesArctisNova7> {
public:
    static constexpr std::array<uint16_t, 7> SUPPORTED_PRODUCT_IDS {
        0x2202, // Arctis Nova 7
        0x227e, // Arctis Nova 7 Wireless
        0x2206, // Arctis Nova 7x
        0x2258, // Arctis Nova 7x v2
        0x229e, // Arctis Nova 7x v2
        0x223a, // Arctis Nova 7 Diablo IV
        0x227a // Arctis Nova 7 WoW Edition
    };

    static constexpr int EQUALIZER_BANDS         = 10;
    static constexpr float EQUALIZER_BAND_MIN    = -10.0f;
    static constexpr float EQUALIZER_BAND_MAX    = 10.0f;
    static constexpr float EQUALIZER_BAND_STEP   = 0.5f;
    static constexpr int EQUALIZER_BASELINE      = 0x14; // 20 decimal
    static constexpr int EQUALIZER_PRESETS_COUNT = 4;

    // Preset arrays (flat, bass, focus, smiley)
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_FLAT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_BASS { 3.5f, 5.5f, 4, 1, -1.5f, -1.5f, -1, -1, -1, -1 };
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_FOCUS { -5, -3.5f, -1, -3.5f, -2.5f, 4, 6, -3.5f, 0, 0 };
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_SMILEY { 3, 3.5f, 1.5f, -1.5f, -4, -4, -2.5f, 1.5f, 3, 4 };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova 7"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME)
            | B(CAP_VOLUME_LIMITER) | B(CAP_BT_WHEN_POWERED_ON) | B(CAP_BT_CALL_VOLUME);
    }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        return EqualizerInfo {
            .bands_count    = EQUALIZER_BANDS,
            .bands_baseline = 0,
            .bands_step     = EQUALIZER_BAND_STEP,
            .bands_min      = static_cast<int>(EQUALIZER_BAND_MIN),
            .bands_max      = static_cast<int>(EQUALIZER_BAND_MAX)
        };
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQUALIZER_PRESETS_COUNT;
    }

    std::optional<EqualizerPresets> getEqualizerPresets() const override
    {
        EqualizerPresets presets;
        presets.presets = {
            { "Flat", std::vector<float>(PRESET_FLAT.begin(), PRESET_FLAT.end()) },
            { "Bass", std::vector<float>(PRESET_BASS.begin(), PRESET_BASS.end()) },
            { "Focus", std::vector<float>(PRESET_FOCUS.begin(), PRESET_FOCUS.end()) },
            { "Smiley", std::vector<float>(PRESET_SMILEY.begin(), PRESET_SMILEY.end()) }
        };
        return presets;
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 3 };
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto status_result = readDeviceStatus(device_handle);
        if (!status_result) {
            return status_result.error();
        }

        auto& data = *status_result;
        if (data.size() < 4) {
            return DeviceError::protocolError("Response too short");
        }

        constexpr uint8_t HEADSET_OFFLINE = 0x00;
        if (data[3] == HEADSET_OFFLINE) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        enum battery_status status = BATTERY_AVAILABLE;
        if (data[3] == 0x01) {
            status = BATTERY_CHARGING;
        }

        int level = map(data[2], 0, 4, 0, 100);
        if (level > 100)
            level = 100;

        return BatteryResult {
            .level_percent = level,
            .status        = status,
            .raw_data      = data
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Nova 7 supports 4 levels (0-3)
        uint8_t mapped = mapSidetoneToDiscrete<4>(level);

        std::array<uint8_t, 3> cmd { 0x00, 0x39, mapped };
        if (auto result = sendCommand(device_handle, cmd); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0x0,
            .device_max    = 0x3
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        std::array<uint8_t, 3> cmd { 0x00, 0xa3, minutes };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto status_result = readDeviceStatus(device_handle);
        if (!status_result) {
            return status_result.error();
        }

        auto& data = *status_result;
        if (data.size() < 6) {
            return DeviceError::protocolError("Response too short for chatmix");
        }

        // Note: Don't check for offline here - the dongle always reports valid
        // chatmix data even when the headset is disconnected.

        // Chatmix data: game volume at byte 4, chat volume at byte 5
        // Values range from 0 to 100
        int game_raw = data[4];
        int chat_raw = data[5];

        // Map to normalized chatmix value (0-128, 64 = center)
        int game  = map(game_raw, 0, 100, 0, 64);
        int chat  = map(chat_raw, 0, 100, 0, -64);
        int level = 64 - (chat + game);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_raw,
            .chat_volume_percent = chat_raw
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        const std::array<float, EQUALIZER_BANDS>* preset_values;
        switch (preset) {
        case 0:
            preset_values = &PRESET_FLAT;
            break;
        case 1:
            preset_values = &PRESET_BASS;
            break;
        case 2:
            preset_values = &PRESET_FOCUS;
            break;
        case 3:
            preset_values = &PRESET_SMILEY;
            break;
        default:
            return DeviceError::invalidParameter("Device only supports presets 0-3");
        }

        // Build equalizer settings from preset
        EqualizerSettings settings;
        settings.bands.assign(preset_values->begin(), preset_values->end());

        auto result = setEqualizer(device_handle, settings);
        if (!result) {
            return result.error();
        }

        return EqualizerPresetResult { .preset = preset, .total_presets = EQUALIZER_PRESETS_COUNT };
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (settings.size() != EQUALIZER_BANDS) {
            return DeviceError::invalidParameter("Device requires exactly 10 equalizer bands");
        }

        std::array<uint8_t, MSG_SIZE> data {};
        data[0] = 0x00;
        data[1] = 0x33;

        for (int i = 0; i < EQUALIZER_BANDS; i++) {
            float gain_value = settings.bands[i];
            if (gain_value < EQUALIZER_BAND_MIN || gain_value > EQUALIZER_BAND_MAX) {
                return DeviceError::invalidParameter("Gain values must be between -10 and +10");
            }

            // Nova 7 uses simple baseline + gain formula
            data[i + 2] = static_cast<uint8_t>(EQUALIZER_BASELINE + gain_value);
        }
        data[EQUALIZER_BANDS + 2] = 0x00; // Terminator

        auto write_result = sendCommand(device_handle, data);
        if (!write_result) {
            return write_result.error();
        }

        return EqualizerResult {};
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        // Nova 7 supports 4 levels: 0x00 (off), 0x01, 0x02, 0x03 (max)
        if (brightness > 3) {
            brightness = 3;
        }

        std::array<uint8_t, 3> cmd { 0x00, 0xae, brightness };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = brightness,
            .min_brightness = 0,
            .max_brightness = 3
        };
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        // Nova 7 supports 8 levels: 0x00 (off) to 0x07 (max)
        // Map 0-128 to 0-7
        uint8_t mapped = volume / 16;
        if (mapped == 8)
            mapped = 7;

        std::array<uint8_t, 3> cmd { 0x00, 0x37, mapped };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return MicVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 128
        };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, 3> cmd { 0x00, 0x3a, static_cast<uint8_t>(enabled ? 1 : 0) };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return VolumeLimiterResult { .enabled = enabled };
    }

    Result<BluetoothWhenPoweredOnResult> setBluetoothWhenPoweredOn(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, 3> cmd { 0x00, 0xb2, static_cast<uint8_t>(enabled ? 1 : 0) };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        // Save the setting
        std::array<uint8_t, 2> save_cmd { 0x06, 0x09 };
        auto save_result = sendCommand(device_handle, save_cmd);
        if (!save_result) {
            return save_result.error();
        }

        return BluetoothWhenPoweredOnResult { .enabled = enabled };
    }

    Result<BluetoothCallVolumeResult> setBluetoothCallVolume(hid_device* device_handle, uint8_t volume) override
    {
        // Nova 7 supports 3 modes:
        // 0x00 = do nothing
        // 0x01 = lower volume by 12dB
        // 0x02 = mute game during call
        if (volume > 2) {
            return DeviceError::invalidParameter("Device only supports values 0-2");
        }

        std::array<uint8_t, 3> cmd { 0x00, 0xb3, volume };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return BluetoothCallVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 2
        };
    }
};
} // namespace headsetcontrol

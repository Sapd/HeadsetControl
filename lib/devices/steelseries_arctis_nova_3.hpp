#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova 3 Gaming Headset
 *
 * Features:
 * - Sidetone (4 levels: 0-3)
 * - Equalizer (6 bands)
 * - Equalizer presets (4 presets)
 * - Microphone mute LED brightness
 * - Microphone volume (10 levels)
 */
class SteelSeriesArctisNova3 : public protocols::SteelSeriesNovaDevice<SteelSeriesArctisNova3> {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x12ec // Arctis Nova 3
    };

    static constexpr int EQUALIZER_BANDS         = 6;
    static constexpr float EQUALIZER_BAND_MIN    = -6.0f;
    static constexpr float EQUALIZER_BAND_MAX    = 6.0f;
    static constexpr float EQUALIZER_BAND_STEP   = 0.5f;
    static constexpr uint8_t EQUALIZER_BASELINE  = 0x14;
    static constexpr int EQUALIZER_PRESETS_COUNT = 4;

    // Preset arrays (flat, bass, smiley, focus)
    // Baseline is 0x14 (20), each step of 0.5 = 2 raw units
    static constexpr std::array<uint8_t, EQUALIZER_BANDS> PRESET_FLAT { 0x14, 0x14, 0x14, 0x14, 0x14, 0x14 };
    static constexpr std::array<uint8_t, EQUALIZER_BANDS> PRESET_BASS { 0x1c, 0x19, 0x11, 0x14, 0x14, 0x14 };
    static constexpr std::array<uint8_t, EQUALIZER_BANDS> PRESET_SMILEY { 0x1a, 0x17, 0x0f, 0x12, 0x17, 0x1a };
    static constexpr std::array<uint8_t, EQUALIZER_BANDS> PRESET_FOCUS { 0x0c, 0x0d, 0x11, 0x18, 0x1c, 0x14 };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova 3"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        // All capabilities use interface 4 with usage page 0xffc0
        switch (cap) {
        case CAP_SIDETONE:
        case CAP_EQUALIZER_PRESET:
        case CAP_EQUALIZER:
        case CAP_MICROPHONE_MUTE_LED_BRIGHTNESS:
        case CAP_MICROPHONE_VOLUME:
            return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 4 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
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

    // Rich Results V2 API
    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Nova 3 only supports 4 levels: 0, 1, 2, 3
        uint8_t mapped;
        if (level < 26) {
            mapped = 0x0;
        } else if (level < 51) {
            mapped = 0x1;
        } else if (level < 76) {
            mapped = 0x2;
        } else {
            mapped = 0x3;
        }

        std::array<uint8_t, 3> cmd { 0x06, 0x39, mapped };
        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        // Save state
        std::array<uint8_t, 2> save_cmd { 0x06, 0x09 };
        if (auto result = sendFeatureReport(device_handle, save_cmd, MSG_SIZE); !result) {
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

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        // Nova 3 supports 11 levels (0x00-0x0a)
        uint8_t mapped = mapSidetoneToDiscrete<11>(volume);

        std::array<uint8_t, 3> cmd { 0x06, 0x37, mapped };
        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        std::array<uint8_t, 2> save_cmd { 0x06, 0x09 };
        if (auto result = sendFeatureReport(device_handle, save_cmd, MSG_SIZE); !result) {
            return result.error();
        }

        return MicVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 128
        };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        // Nova 3 supports: 0 (off), 1 (low), 2 (medium), 3 (high)
        if (brightness > 3) {
            return DeviceError::invalidParameter("Brightness must be 0-3");
        }

        std::array<uint8_t, 3> cmd { 0x06, 0xae, brightness };
        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        std::array<uint8_t, 2> save_cmd { 0x06, 0x09 };
        if (auto result = sendFeatureReport(device_handle, save_cmd, MSG_SIZE); !result) {
            return result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = brightness,
            .min_brightness = 0,
            .max_brightness = 3
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        const std::array<uint8_t, EQUALIZER_BANDS>* preset_values;
        switch (preset) {
        case 0:
            preset_values = &PRESET_FLAT;
            break;
        case 1:
            preset_values = &PRESET_BASS;
            break;
        case 2:
            preset_values = &PRESET_SMILEY;
            break;
        case 3:
            preset_values = &PRESET_FOCUS;
            break;
        default:
            return DeviceError::invalidParameter("Device only supports presets 0-3");
        }

        std::array<uint8_t, MSG_SIZE> cmd {};
        cmd[0] = 0x06;
        cmd[1] = 0x33;
        for (int i = 0; i < EQUALIZER_BANDS; i++) {
            cmd[i + 2] = (*preset_values)[i];
        }

        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        std::array<uint8_t, 2> save_cmd { 0x06, 0x09 };
        if (auto result = sendFeatureReport(device_handle, save_cmd, MSG_SIZE); !result) {
            return result.error();
        }

        return EqualizerPresetResult { .preset = preset, .total_presets = EQUALIZER_PRESETS_COUNT };
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (settings.size() != EQUALIZER_BANDS) {
            return DeviceError::invalidParameter("Device requires exactly 6 equalizer bands");
        }

        std::array<uint8_t, MSG_SIZE> cmd {};
        cmd[0] = 0x06;
        cmd[1] = 0x33;

        for (int i = 0; i < settings.size(); i++) {
            float band_value = settings.bands[i];
            if (band_value < EQUALIZER_BAND_MIN || band_value > EQUALIZER_BAND_MAX) {
                return DeviceError::invalidParameter("Band values must be between -6 and +6");
            }
            cmd[i + 2] = static_cast<uint8_t>(EQUALIZER_BASELINE + 2 * band_value);
        }

        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        return EqualizerResult {};
    }
};
} // namespace headsetcontrol

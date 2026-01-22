#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis 7+ Gaming Headset
 *
 * Features:
 * - Sidetone (4 levels)
 * - Battery status (0-4 range)
 * - Chatmix
 * - Inactive time
 * - Equalizer (10 bands, -12 to +12 range)
 */
class SteelSeriesArctis7Plus : public protocols::SteelSeriesNovaDevice<SteelSeriesArctis7Plus> {
public:
    static constexpr std::array<uint16_t, 4> SUPPORTED_PRODUCT_IDS {
        0x220e, // Arctis 7+
        0x2212, // Arctis 7+ PS5
        0x2216, // Arctis 7+ Xbox
        0x2236 // Arctis 7+ Destiny
    };

    static constexpr int EQUALIZER_BANDS         = 10;
    static constexpr float EQUALIZER_BAND_MIN    = -12.0f;
    static constexpr float EQUALIZER_BAND_MAX    = 12.0f;
    static constexpr float EQUALIZER_BAND_STEP   = 0.5f;
    static constexpr uint8_t EQUALIZER_BASELINE  = 0x18;
    static constexpr int EQUALIZER_PRESETS_COUNT = 4;

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis 7+"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 3 };
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
        // Convert raw byte values to dB using: value = (raw - EQUALIZER_BASELINE) / 2.0f
        EqualizerPresets presets;
        presets.presets = {
            { "Flat", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
            { "Bass Boost", { 3.5f, 4.0f, 1.0f, -1.5f, -1.5f, -1.0f, -1.0f, -1.0f, -1.0f, 5.5f } },
            { "Smiley", { 3.0f, 1.5f, -1.5f, -4.0f, -4.0f, -2.5f, 1.5f, 3.0f, 4.0f, 3.5f } },
            { "Focus", { -5.0f, -1.0f, -3.5f, -2.5f, 4.0f, 6.0f, 3.5f, -3.5f, 0.0f, -3.5f } }
        };
        return presets;
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

        constexpr uint8_t HEADSET_OFFLINE = 0x01;
        if (data[1] == HEADSET_OFFLINE) {
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
        uint8_t mapped;
        if (level < 26)
            mapped = 0x0;
        else if (level < 51)
            mapped = 0x1;
        else if (level < 76)
            mapped = 0x2;
        else
            mapped = 0x3;

        std::array<uint8_t, 3> cmd { 0x00, 0x39, mapped };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
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
        if (minutes > 90) {
            minutes = 90;
        }

        std::array<uint8_t, 3> cmd { 0x00, 0xa3, minutes };
        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
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
        // Values range from 0 to 0x64 (100)
        int game_raw = data[4];
        int chat_raw = data[5];

        // Map to normalized chatmix value (0-128, 64 = center)
        int game = map(game_raw, 0, 0x64, 0, 64);
        int chat = map(chat_raw, 0, 0x64, 0, -64);
        int level = 64 - (chat + game);

        // Calculate percentages
        int game_pct = map(game_raw, 0, 0x64, 0, 100);
        int chat_pct = map(chat_raw, 0, 0x64, 0, 100);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        // 4 presets: flat (0), bass boost (1), smiley (2), focus (3)
        std::array<uint8_t, 13> cmd {};

        switch (preset) {
        case 0: // Flat
            cmd = { 0x0, 0x33, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0 };
            break;
        case 1: // Bass boost
            cmd = { 0x0, 0x33, 0x1f, 0x20, 0x1a, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x23, 0x0 };
            break;
        case 2: // Smiley
            cmd = { 0x0, 0x33, 0x1e, 0x1b, 0x15, 0x10, 0x10, 0x13, 0x1b, 0x1e, 0x20, 0x1f, 0x0 };
            break;
        case 3: // Focus
            cmd = { 0x0, 0x33, 0x0e, 0x16, 0x11, 0x13, 0x20, 0x24, 0x1f, 0x11, 0x18, 0x11, 0x0 };
            break;
        default:
            return DeviceError::invalidParameter("Device only supports presets 0-3");
        }

        auto result = sendCommand(device_handle, cmd);
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

        std::array<uint8_t, MSG_SIZE> cmd {};
        cmd[0] = 0x0;
        cmd[1] = 0x33;

        for (int i = 0; i < settings.size(); i++) {
            float band_value = settings.bands[i];
            if (band_value < EQUALIZER_BAND_MIN || band_value > EQUALIZER_BAND_MAX) {
                return DeviceError::invalidParameter("Band values must be between -12 and +12");
            }
            cmd[i + 2] = static_cast<uint8_t>(EQUALIZER_BASELINE + 2 * band_value);
        }
        cmd[settings.size() + 2] = 0x0;

        auto result = sendCommand(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return EqualizerResult {};
    }
};
} // namespace headsetcontrol

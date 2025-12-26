#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova Pro Wireless Gaming Headset
 *
 * Features:
 * - Sidetone (4 levels: 0-3)
 * - Battery status (0-8 range)
 * - Lights control
 * - Inactive time (discrete levels)
 * - Equalizer (10 bands)
 */
class SteelSeriesArctisNovaProWireless : public protocols::SteelSeriesLegacyDevice<SteelSeriesArctisNovaProWireless> {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x12e0, // Nova Pro Wireless Base Station
        0x12e5 // Nova Pro Wireless X Base Station
    };

    static constexpr int EQUALIZER_BANDS         = 10;
    static constexpr float EQUALIZER_BAND_MIN    = -10.0f;
    static constexpr float EQUALIZER_BAND_MAX    = 10.0f;
    static constexpr float EQUALIZER_BAND_STEP   = 0.5f;
    static constexpr uint8_t EQUALIZER_BASELINE  = 0x14;
    static constexpr int EQUALIZER_PRESETS_COUNT = 4; // Custom is preset 4

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova Pro Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS)
            | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .interface_id = 4 };
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

    // Override save state
    Result<void> saveState(hid_device* device_handle) const override
    {
        std::array<uint8_t, PACKET_SIZE_31> data { 0x06, 0x09 };
        return writeHID(device_handle, data);
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, PACKET_SIZE_31> request { 0x06, 0xb0 };

        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        std::array<uint8_t, 128> response {};
        auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }
        if (*read_result < 16) {
            return DeviceError::protocolError("Response too short");
        }

        // Check headset status
        constexpr uint8_t HEADSET_OFFLINE                          = 0x01;
        constexpr uint8_t HEADSET_CABLE_CHARGING                   = 0x02;
        [[maybe_unused]] constexpr uint8_t HEADSET_ONLINE          = 0x08;

        uint8_t status_byte = response[15];
        if (status_byte == HEADSET_OFFLINE) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        enum battery_status status = BATTERY_AVAILABLE;
        if (status_byte == HEADSET_CABLE_CHARGING) {
            status = BATTERY_CHARGING;
        }

        int level = map(response[6], 0, 8, 0, 100);

        return BatteryResult {
            .level_percent = level,
            .status        = status,
            .raw_data      = std::vector<uint8_t>(response.begin(), response.end())
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Map normalized level (0-128) to device levels (0-3)
        // This maintains API consistency with other devices
        uint8_t device_level = 0;
        if (level == 0) {
            device_level = 0;
        } else if (level <= 42) {
            device_level = 1;
        } else if (level <= 85) {
            device_level = 2;
        } else {
            device_level = 3;
        }

        std::array<uint8_t, 3> cmd { 0x06, 0x39, device_level };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 3
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // Map on/off to LED strength (1-10)
        uint8_t led_strength = map<uint8_t>(on ? 1 : 0, 0, 1, 1, 10);
        std::array<uint8_t, 3> cmd { 0x06, 0xbf, led_strength };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Round to nearest supported value
        uint8_t level = 0; // Disabled
        if (minutes >= 45)
            level = 6; // 60 min
        else if (minutes >= 23)
            level = 5; // 30 min
        else if (minutes >= 13)
            level = 4; // 15 min
        else if (minutes >= 8)
            level = 3; // 10 min
        else if (minutes >= 3)
            level = 2; // 5 min
        else if (minutes > 0)
            level = 1; // 1 min

        std::array<uint8_t, 3> cmd { 0x06, 0xc1, level };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        if (preset >= EQUALIZER_PRESETS_COUNT) {
            return DeviceError::invalidParameter("Device only supports presets 0-3");
        }

        std::array<uint8_t, 3> cmd { 0x06, 0x2e, preset };

        auto result = sendCommandWithSave(device_handle, cmd);
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

        // First, select custom preset (preset 4)
        constexpr uint8_t CUSTOM_PRESET = 4;
        std::array<uint8_t, 3> preset_cmd { 0x06, 0x2e, CUSTOM_PRESET };
        if (auto result = writeHID(device_handle, preset_cmd); !result) {
            return result.error();
        }

        // Build equalizer command
        std::array<uint8_t, PACKET_SIZE_31> cmd {};
        cmd[0] = 0x06;
        cmd[1] = 0x33;

        for (int i = 0; i < settings.size(); i++) {
            float band_value = settings.bands[i];
            if (band_value < EQUALIZER_BAND_MIN || band_value > EQUALIZER_BAND_MAX) {
                return DeviceError::invalidParameter("Band values must be between -10 and +10");
            }
            cmd[i + 2] = static_cast<uint8_t>(EQUALIZER_BASELINE + 2 * band_value);
        }

        auto result = writeHID(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return EqualizerResult {};
    }
};
} // namespace headsetcontrol

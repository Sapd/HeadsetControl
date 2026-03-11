#pragma once

#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Lenovo Wireless VoIP Headset
 *
 * Features:
 * - Sidetone (6 levels)
 * - Battery status (no charging status)
 * - Inactive time with discrete levels
 * - Voice prompts
 * - Rotate to mute
 * - Volume limiter
 * - Equalizer (4 presets) TODO: 5-band parametric
 */
class LenovoWirelessVoip : public HIDDevice {
private:
    static constexpr int MSG_SIZE = 61;

    static constexpr uint8_t ID_INDEX  = 0;
    static constexpr uint8_t CMD_INDEX = 1;
    static constexpr uint8_t ERR_INDEX = 2;

    static constexpr uint8_t CMD_ID = 0x24;
    static constexpr uint8_t RSP_ID = 0x27;

    static constexpr uint8_t STATUS_CMD        = 0x01;
    static constexpr uint8_t EQ_MODE_CMD       = 0x02;
    static constexpr uint8_t MIC_MUTE_CMD      = 0x04;
    static constexpr uint8_t VOICE_PROMPTS_CMD = 0x05;
    static constexpr uint8_t VOLUME_LIMIT_CMD  = 0x07;
    static constexpr uint8_t INACTIVE_TIME_CMD = 0x08;
    static constexpr uint8_t SIDETONE_CMD      = 0x0E;
    static constexpr uint8_t MIC_LIGHT_CMD     = 0x15;

    static constexpr int EQ_PRESET_COUNT = 4;

    /**
     * @brief Send feature report and get input report for a given command and payload
     */
    Result<void> sendGetReport(hid_device* device_handle, uint8_t cmd, std::span<const uint8_t> payload, std::array<uint8_t, MSG_SIZE>& response) const
    {
        std::array<uint8_t, MSG_SIZE> data {};

        if (payload.size() > MSG_SIZE - 2) {
            return DeviceError::hidError("Invalid payload size");
        }

        data[ID_INDEX]  = CMD_ID;
        data[CMD_INDEX] = cmd;

        std::copy(payload.begin(), payload.end(), data.begin() + 2);

        if (auto result = sendFeatureReport(device_handle, data); !result) {
            return result.error();
        }

        response[ID_INDEX] = RSP_ID;

        auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
        if (!read_result) {
            return read_result.error();
        }

        if (*read_result != MSG_SIZE) {
            return DeviceError::hidError("Failed to get input report");
        }

        if (response[CMD_INDEX] != cmd) {
            return DeviceError::hidError("Wrong command response");
        }

        // Check if device is connected
        if (response[ERR_INDEX] != 0) {
            return DeviceError::deviceOffline("Headset not connected to wireless receiver");
        }

        return {};
    }

public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS {
        0xA07D // Lenovo Wireless VoIP Headset-Receiver
    };

    constexpr uint16_t getVendorId() const override
    {
        return 0x17EF; // Lenovo vendor ID
    }

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Lenovo Wireless VoIP Headset"sv;
    }

    constexpr int getCapabilities() const override
    {
        // Return bitmask of supported capabilities
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME)
            | B(CAP_VOICE_PROMPTS) | B(CAP_ROTATE_TO_MUTE) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_VOLUME_LIMITER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xFF07, .usageid = 0x222, .interface_id = 0x03 };
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQ_PRESET_COUNT;
    }

    std::optional<EqualizerPresets> getEqualizerPresets() const override
    {
        EqualizerPresets presets;
        presets.presets = {
            { "Music", {} },
            { "Movie", {} },
            { "Game", {} },
            { "Voice", {} }
        };
        return presets;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, map<uint8_t>(level, 0, 128, 0, 5) };

        auto result = sendGetReport(device_handle, SIDETONE_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = response[4],
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 5
        };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, MSG_SIZE> response {};

        auto result = sendGetReport(device_handle, STATUS_CMD, {}, response);
        if (!result) {
            return result.error();
        }

        BatteryResult battery {};
        battery.level_percent = response[7];
        battery.status        = BATTERY_AVAILABLE;

        return battery;
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, 0x00 }; // Disabled by default

        // 0x00 -> disabled | 0x01 -> 1 hour | 0x02 -> 2 hours | 0x03 -> 4 hours | 0x04 -> 8 hours
        if (minutes > 0) {
            if (minutes <= 60)
                payload[1] = 0x01;
            else if (minutes <= 120)
                payload[1] = 0x02;
            else if (minutes <= 240)
                payload[1] = 0x03;
            else
                payload[1] = 0x04;
        }

        auto result = sendGetReport(device_handle, INACTIVE_TIME_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = response[4],
            .min_minutes = 0,
            .max_minutes = 255 // TODO: Headset supports up to 8 hours
        };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        // Right now switching between "voice" and "off", but we also have an option for "beep" (0x02)
        // that is the same as "voice", but the mute action uses a beep instead
        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, enabled ? uint8_t(0x01) : uint8_t(0x00) };

        auto result = sendGetReport(device_handle, VOICE_PROMPTS_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return VoicePromptsResult { .enabled = response[4] != 0 };
    }

    Result<RotateToMuteResult> setRotateToMute(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, enabled ? uint8_t(0x01) : uint8_t(0x00) };

        auto result = sendGetReport(device_handle, MIC_MUTE_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return RotateToMuteResult { .enabled = response[4] != 0 };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, preset };

        // Music, Movie, Game and Voice presets
        if (preset >= EQ_PRESET_COUNT) {
            return DeviceError::invalidParameter("Device only supports presets 0-3");
        }

        auto result = sendGetReport(device_handle, EQ_MODE_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return EqualizerPresetResult { .preset = response[4], .total_presets = EQ_PRESET_COUNT };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {

        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, (brightness > 0) ? uint8_t(0x00) : uint8_t(0x01) };

        auto result = sendGetReport(device_handle, MIC_LIGHT_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = brightness,
            .min_brightness = 0,
            .max_brightness = 1
        };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        // Right now switching between "85db" and "off", but we also have an option for "80db" (0x01)
        std::array<uint8_t, MSG_SIZE> response {};
        std::array<uint8_t, 2> payload { 0x01, enabled ? uint8_t(0x02) : uint8_t(0x00) };

        auto result = sendGetReport(device_handle, VOLUME_LIMIT_CMD, payload, response);
        if (!result) {
            return result.error();
        }

        return VolumeLimiterResult { .enabled = response[4] != 0 };
    }
};

} // namespace headsetcontrol

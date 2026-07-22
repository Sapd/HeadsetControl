#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <string_view>
#include <thread>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Audeze Maxwell 2 Gaming Headset
 *
 * Features:
 * - Sidetone (32 levels)
 * - Inactive time with discrete levels
 * - Equalizer presets (10 total: 6 default + 4 custom)
 * - Battery status
 * - Chatmix
 * - Voice prompts
 * - Mic noise filter
 *
 * Special Requirements:
 * - 15-packet initialization sequence followed by 6 status requests
 * - 60ms delay between packets (official software sends at 52ms but 60ms is more reliable in testing)
 * - Uses hid_get_input_report for responses
 */
class AudezeMaxwell2 : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x4b29, // Maxwell 2 (PlayStation/PC version)
        0x4b28  // Maxwell 2 (Xbox version)
    };

    static constexpr int MSG_SIZE  = 62;
    static constexpr int REPORT_ID = 0x06;
    static constexpr int DELAY_US  = 60000; // 60ms

    // 15-packet initialization sequence
    static constexpr std::array<std::array<uint8_t, MSG_SIZE>, 15> UNIQUE_REQUESTS { { { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x20 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x25 },
        { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0x07, 0x1C },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x28 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x01 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x07 },
        { 0x06, 0x07, 0x00, 0x05, 0x5A, 0x03, 0x00, 0x07, 0x1C },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2D },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2C },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x0B },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x24 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2F },
        { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x25, 0x00, 0x7A },
        { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C } } };

    // 6 packet status request sequence
    static constexpr std::array<std::array<uint8_t, MSG_SIZE>, 6> STATUS_REQUESTS { { { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x22 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x0B },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x24 },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2C },
        { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x07 } } };

    constexpr uint16_t getVendorId() const override
    {
        return 0x3329; // Audeze
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Audeze Maxwell 2"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_VOICE_PROMPTS)
            | B(CAP_NOISE_FILTER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff13, .usageid = 0x1, .interface_id = 5 };
    }

    static constexpr int EQUALIZER_PRESETS_COUNT = 10;

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQUALIZER_PRESETS_COUNT;
    }

private:
    /**
     * @brief Get device status (battery, chatmix, sidetone, mic mute)
     *
     * Must send all 6 status requests to get consistent information
     */
    struct MaxwellStatus {
        BatteryResult battery;
        int chatmix_level;
        int sidetone_level;
        int eq_setting;
        int ainf_level;
        bool sidetone_enabled;
        bool mic_muted;
    };

    /**
     * @brief Send request and get input report with 60ms delay to match Audeze HQ.
     */
    Result<void> sendGetInputReport(hid_device* device_handle, std::span<const uint8_t> data, std::span<uint8_t> buff = {}) const
    {
        std::this_thread::sleep_for(std::chrono::microseconds(DELAY_US));

        if (auto result = writeHID(device_handle, data, MSG_SIZE); !result) {
            return result.error();
        }

        if (buff.empty()) {
            std::array<uint8_t, MSG_SIZE> tempBuff { 0x7 };
            auto read_result = getInputReport(device_handle, tempBuff);
            if (!read_result || *read_result != MSG_SIZE) {
                return DeviceError::hidError("Failed to get input report");
            }
        } else {
            buff[0]          = 0x7;
            auto read_result = getInputReport(device_handle, buff);
            if (!read_result || *read_result != MSG_SIZE) {
                return DeviceError::hidError("Failed to get input report");
            }
        }

        return {};
    }

    /**
     * @brief Get comprehensive device status (battery, chatmix, sidetone, etc.)
     *
     * Must send all initialization and status requests packets to get consistent information
     */
    Result<MaxwellStatus> getDeviceStatus(hid_device* device_handle) const
    {
        std::array<std::array<uint8_t, MSG_SIZE>, 6> status_buffs {};

        // Send UNIQUE_REQUESTS packets
        for (int i = 0; i < 15; ++i) {
            auto result = sendGetInputReport(device_handle, UNIQUE_REQUESTS[i]);
            if (!result) {
                return result.error();
            }
        }

        // Send STATUS_REQUESTS packets
        for (int i = 0; i < 6; ++i) {
            auto result = sendGetInputReport(device_handle, STATUS_REQUESTS[i], status_buffs[i]);
            if (!result) {
                return result.error();
            }
        }

        // Status_buffs structure
        // status_buffs[0] = battery_status
        // status_buffs[1] = mic_status
        // status_buffs[2] = eq
        // status_buffs[3] = chatmix
        // status_buffs[4] = ainf_level
        // status_buffs[5] = sidetone_level

        MaxwellStatus status {};

        const auto& buff = status_buffs[0];

        // Initialize battery status as unavailable
        status.battery.status        = BATTERY_UNAVAILABLE;
        status.battery.level_percent = -1;

        // Parse battery level
        if (buff[0] == 0x00) {
            status.battery.status        = BATTERY_UNAVAILABLE;
            status.battery.level_percent = -1;
        } else {
            // Find battery level in response (level is first byte after d6 0c 00 00 in packet)
            for (int i = 0; i < MSG_SIZE - 4; ++i) {
                if (buff[i] == 0xD6 && buff[i + 1] == 0x0C && buff[i + 2] == 0x00 && buff[i + 3] == 0x00) {
                    status.battery.level_percent = buff[i + 4];
                    status.battery.status        = BATTERY_AVAILABLE;
                    break;
                }
            }
        }

        // Parse microphone mute status (FF = unmuted)
        status.mic_muted = status_buffs[1][12] != 0xFF;

        // Parse current eq setting
        // 1 = Audeze, 2 = Treble Boost, 3 = Bass Boost, 4 = Immersive, 5 = Competition,
        // 6 = Footsteps, 7 = EQ1, 8 = EQ2, 9 = EQ3, 10 = EQ4
        status.eq_setting = status_buffs[2][12];

        // Parse chatmix (0-20 range, center at 10)
        status.chatmix_level = map(status_buffs[3][12], 0, 20, 0, 128);

        // Parse current mic noise filter level
        // 0 = Off, 1 = Low, 2 = High
        status.ainf_level = status_buffs[4][12];

        // Parse sidetone (0-31 range)
        status.sidetone_level   = map(status_buffs[5][12], 0, 31, 0, 128);
        status.sidetone_enabled = status_buffs[5][12] != 0;

        return status;
    }

public:
    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto status_result = getDeviceStatus(device_handle);
        if (!status_result) {
            return status_result.error();
        }

        return status_result->battery;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Maxwell range: 0 to 31
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 31);

        std::array<uint8_t, MSG_SIZE> cmd { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x2C, 0x00, mapped };

        // Sidetone is enabled whenever level changes
        auto result = sendGetInputReport(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        if (mapped == 0) {
            // Toggle sidetone off
            std::array<uint8_t, MSG_SIZE> toggle_cmd { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x82, 0x2C, 0x07, 0x00, 0x00 };
            result = sendGetInputReport(device_handle, toggle_cmd);
            if (!result) {
                return result.error();
            }
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 31
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        std::array<uint8_t, MSG_SIZE> cmd { 0x06, 0x10, 0x80, 0x05, 0x5A, 0x0C, 0x00, 0x82, 0x2C, 0x01, 0x00 };

        /* The headset supports auto shutdown times up to 6 hours or 360 minutes but since this uses an 8-bit
         * integer it is limited to 255. Here it is truncated to 240 minutes (4 hours) which is the closest
         * option from the Audeze software.
         */

        if (minutes > 0) {
            // Round to supported values
            uint16_t final_minutes = 240;
            if (minutes <= 5)
                final_minutes = 5;
            else if (minutes <= 10)
                final_minutes = 10;
            else if (minutes <= 15)
                final_minutes = 15;
            else if (minutes <= 30)
                final_minutes = 30;
            else if (minutes <= 45)
                final_minutes = 45;
            else if (minutes <= 60)
                final_minutes = 60;
            else if (minutes <= 90)
                final_minutes = 90;
            else if (minutes <= 120)
                final_minutes = 120;
            else if (minutes <= 240)
                final_minutes = 240;

            uint16_t seconds = final_minutes * 60;

            cmd[11] = 0x01; // Inactive time flag
            cmd[13] = seconds & 0xFF; // LSB
            cmd[14] = (seconds >> 8) & 0xFF; // MSB
            cmd[15] = 0x01; // Constant flag
            cmd[17] = cmd[13]; // LSB again
            cmd[18] = cmd[14]; // MSB again
        }

        auto result = sendGetInputReport(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 240
        };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        // Voice prompt level: 0 (0%) to 15 (100%), default 7 (50%)
        // API doesn't support level, so use 7 when on, 0 when off
        std::array<uint8_t, MSG_SIZE> cmd { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x20, 0x00, enabled ? uint8_t(0x07) : uint8_t(0x00) };

        auto result = sendGetInputReport(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto status_result = getDeviceStatus(device_handle);
        if (!status_result) {
            return status_result.error();
        }

        int level = status_result->chatmix_level;

        // Calculate percentages (0-20 range, 10 = center)
        // level is already mapped to 0-128
        int game_pct = (level >= 64) ? 100 : map(level, 0, 64, 0, 100);
        int chat_pct = (level <= 64) ? 100 : map(level, 64, 128, 100, 0);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        // Maxwell supports presets 0-9 (mapped to device presets 1-10)
        // 0-5 are built-in presets, 6-9 are custom presets
        if (preset >= EQUALIZER_PRESETS_COUNT) {
            return DeviceError::invalidParameter("Device only supports presets 0-9");
        }

        // Device uses 1-10 range internally
        uint8_t device_preset = preset + 1;

        std::array<uint8_t, MSG_SIZE> cmd { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x00, 0x00, device_preset };

        auto result = sendGetInputReport(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return EqualizerPresetResult { .preset = preset, .total_presets = EQUALIZER_PRESETS_COUNT };
    }

    Result<NoiseFilterResult> setNoiseFilter(hid_device* device_handle, uint8_t level) override
    {
        // Maxwell 2 has three levels for the noise filter: high, low, and
        // off (2,1 and 0)
        if (level > 2) {
            return DeviceError::invalidParameter("Noise filter level must be 0, 1, or 2");
        }
        std::array<uint8_t, MSG_SIZE> cmd { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x24, 0x00, level };

        auto result = sendGetInputReport(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return NoiseFilterResult { .level = level };
    }
};
} // namespace headsetcontrol

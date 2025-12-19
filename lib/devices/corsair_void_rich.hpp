#pragma once

#include "../result_types.hpp"
#include "../utility.hpp"
#include "corsair_device.hpp"
#include <array>
#include <chrono>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Corsair Void family with RICH RESULT TYPES
 *
 * This implementation extracts ALL possible data from HID packets:
 * - Battery percentage (0-100%)
 * - Microphone status (up/down via bit 7)
 * - Charging status
 * - Device status (connected/disconnected)
 * - Query timing
 * - Raw packet data for debugging
 */
class CorsairVoidRich : public CorsairDevice {
public:
    static constexpr std::array<uint16_t, 18> SUPPORTED_PRODUCT_IDS {
        0x1b1c, 0x1b27, 0x0a14, 0x0a16, 0x0a17, 0x0a1d, 0x0a1a,
        0x1b2a, 0x1b23, 0x1b29, 0x0a55, 0x0a51, 0x0a52, 0x0a38,
        0x0a4f, 0x0a2b, 0x0a75, 0x0a56
    };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Corsair Headset Device"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_NOTIFICATION_SOUND) | B(CAP_LIGHTS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
            return { .usagepage = 0xff00, .usageid = 0x1, .interface_id = 0 };
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xffc5, .usageid = 0x1, .interface_id = 3 };
        case CAP_NOTIFICATION_SOUND:
            return { .usagepage = 0xffc5, .usageid = 0x1, .interface_id = 3 };
        case CAP_LIGHTS:
            return { .usagepage = 0xffc5, .usageid = 0x1, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    // ========================================================================
    // RICH RESULT API IMPLEMENTATIONS
    // ========================================================================

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        // Send battery status request
        std::array<uint8_t, 2> request { 0xC9, 0x64 };

        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        // Read response (5 bytes)
        std::array<uint8_t, 5> response {};
        auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        if (!read_result) {
            return read_result.error();
        }

        // Parse the Corsair battery packet
        // Packet format:
        // [0] = 100 (constant)
        // [1] = 0 (constant)
        // [2] = Battery level (0-100) with microphone flag in bit 7
        // [3] = 177 (constant)
        // [4] = Status: 0=disconnected, 1=normal, 2=low, 4/5=charging

        const uint8_t status_byte = response[4];

        // Check if device is connected
        if (status_byte == 0) {
            return DeviceError::deviceOffline("Headset not connected to wireless receiver");
        }

        // Determine status
        enum battery_status status;
        const bool is_charging = (status_byte == 4 || status_byte == 5);

        if (is_charging) {
            status = BATTERY_CHARGING;
        } else if (status_byte == 2) {
            status = BATTERY_AVAILABLE; // Low battery but still available
        } else if (status_byte == 1) {
            status = BATTERY_AVAILABLE; // Normal
        } else {
            return DeviceError::protocolError(
                std::format("Unknown status byte: {}", status_byte));
        }

        // Extract battery level (0-100%)
        uint8_t battery_byte              = response[2];
        enum microphone_status mic_status = MICROPHONE_UNKNOWN;

        // Check microphone flag (bit 7)
        static constexpr uint8_t MIC_UP_FLAG = 0x80;
        if (battery_byte & MIC_UP_FLAG) {
            mic_status = MICROPHONE_UP;
            battery_byte &= ~MIC_UP_FLAG; // Clear flag to get actual level
        }

        int level = static_cast<int>(battery_byte);

        // Build rich result
        BatteryResult result {
            .level_percent  = level,
            .status         = status,
            .mic_status     = mic_status,
            .raw_data       = std::vector<uint8_t>(response.begin(), response.end()),
            .query_duration = duration
        };

        // BONUS: Calculate additional info if charging
        if (is_charging && level < 100) {
            // Estimate time to full (rough calculation)
            // Assume ~2 hours to charge from 0 to 100%
            int remaining_percent   = 100 - level;
            result.time_to_full_min = (remaining_percent * 120) / 100;
        }

        // Estimate time to empty if discharging
        if (!is_charging && level > 0) {
            // Typical Corsair Void battery life: ~12 hours
            result.time_to_empty_min = (level * 720) / 100; // 12 hours * 60 min
        }

        return result;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Corsair uses range 200-255 internally
        constexpr uint8_t CORSAIR_MIN = 200;
        constexpr uint8_t CORSAIR_MAX = 255;

        // Map from 0-128 to 200-255
        uint8_t mapped_level = map(level, 0, 128, CORSAIR_MIN, CORSAIR_MAX);

        std::array<uint8_t, 64> data {
            0xFF, 0x0B, 0x00, 0xFF, 0x04, 0x0E, 0xFF, 0x05,
            0x01, 0x04, 0x00, mapped_level
        };

        if (auto result = sendFeatureReport(device_handle, data); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = CORSAIR_MIN,
            .device_max    = CORSAIR_MAX
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // Corsair uses inverted logic: 0x00 = ON, 0x01 = OFF
        std::array<uint8_t, 3> data { 0xC8, static_cast<uint8_t>(on ? 0x00 : 0x01), 0x00 };

        if (auto result = writeHID(device_handle, data); !result) {
            return result.error();
        }

        return LightsResult {
            .enabled = on
        };
    }

    Result<NotificationSoundResult> notificationSound(hid_device* device_handle, uint8_t sound_id) override
    {
        // Corsair Void supports sound_id 0 or 1
        std::array<uint8_t, 5> data { 0xCA, 0x02, sound_id };

        if (auto result = writeHID(device_handle, data, 3); !result) {
            return result.error();
        }

        return NotificationSoundResult { .sound_id = sound_id };
    }

    Result<CapabilityInfo> getCapabilityInfo(enum capabilities cap) override
    {
        auto info = HIDDevice::getCapabilityInfo(cap);
        if (!info)
            return info;

        // Add device-specific parameter info
        switch (cap) {
        case CAP_SIDETONE:
            info->parameter = CapabilityInfo::RangeParam {
                .min   = 0,
                .max   = 128,
                .step  = 1,
                .units = "level"
            };
            break;

        case CAP_NOTIFICATION_SOUND:
            info->parameter = CapabilityInfo::ChoiceParam {
                .choices = { "Sound 0", "Sound 1" }
            };
            break;

        case CAP_LIGHTS:
            info->parameter = CapabilityInfo::BoolParam {};
            break;

        default:
            break;
        }

        return info;
    }
};
} // namespace headsetcontrol

#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud Alpha Wireless Gaming Headset
 *
 * Features:
 * - Battery status (percentage-based)
 * - Inactive time/auto-shutoff
 * - Sidetone control
 * - Voice prompts toggle
 *
 * Vendor: HP, Inc (0x03f0) - Cloud Alpha sold with HP vendor ID
 */
class HyperXCloudAlphaWireless : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_HP = 0x03f0;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x098d // Cloud Alpha Wireless
    };

    static constexpr int TIMEOUT_MS = 2000;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_HP;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud Alpha Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_SIDETONE) | B(CAP_VOICE_PROMPTS);
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Check connection status
        std::array<uint8_t, 31> conn_request {};
        conn_request[0] = 0x21;
        conn_request[1] = 0xbb;
        conn_request[2] = 0x03;

        if (auto result = writeHID(device_handle, conn_request); !result) {
            return result.error();
        }

        std::array<uint8_t, 31> conn_response {};
        auto conn_read = readHIDTimeout(device_handle, conn_response, TIMEOUT_MS);
        if (!conn_read) {
            return conn_read.error();
        }

        if (conn_response[3] == 0x01) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        // Check charging status
        std::array<uint8_t, 31> charge_request {};
        charge_request[0] = 0x21;
        charge_request[1] = 0xbb;
        charge_request[2] = 0x0c;

        if (auto result = writeHID(device_handle, charge_request); !result) {
            return result.error();
        }

        std::array<uint8_t, 31> charge_response {};
        auto charge_read = readHIDTimeout(device_handle, charge_response, TIMEOUT_MS);
        if (!charge_read) {
            return charge_read.error();
        }

        enum battery_status status = BATTERY_AVAILABLE;
        if (charge_response[3] == 0x01) {
            status = BATTERY_CHARGING;
            return BatteryResult {
                .level_percent = -1,
                .status        = status
            };
        }

        // Request battery level
        std::array<uint8_t, 31> battery_request {};
        battery_request[0] = 0x21;
        battery_request[1] = 0xbb;
        battery_request[2] = 0x0b;

        if (auto result = writeHID(device_handle, battery_request); !result) {
            return result.error();
        }

        std::array<uint8_t, 31> battery_response {};
        auto battery_read = readHIDTimeout(device_handle, battery_response, TIMEOUT_MS);
        if (!battery_read) {
            return battery_read.error();
        }

        int level = battery_response[3];

        return BatteryResult {
            .level_percent = level,
            .status        = status,
            .raw_data      = std::vector<uint8_t> { battery_response.begin(), battery_response.end() }
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        if (level == 0) {
            // Turn off sidetone
            std::array<uint8_t, 31> cmd {};
            cmd[0] = 0x21;
            cmd[1] = 0xbb;
            cmd[2] = 0x10;
            cmd[3] = 0x00;

            if (auto result = writeHID(device_handle, cmd); !result) {
                return result.error();
            }
        } else {
            // Enable sidetone
            std::array<uint8_t, 31> cmd_enable {};
            cmd_enable[0] = 0x21;
            cmd_enable[1] = 0xbb;
            cmd_enable[2] = 0x10;
            cmd_enable[3] = 0x01;

            if (auto result = writeHID(device_handle, cmd_enable); !result) {
                return result.error();
            }

            // Set level
            std::array<uint8_t, 31> cmd_level {};
            cmd_level[0] = 0x21;
            cmd_level[1] = 0xbb;
            cmd_level[2] = 0x11;
            cmd_level[3] = level;

            if (auto result = writeHID(device_handle, cmd_level); !result) {
                return result.error();
            }
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        std::array<uint8_t, 31> cmd {};
        cmd[0] = 0x21;
        cmd[1] = 0xbb;
        cmd[2] = 0x12;
        cmd[3] = minutes;

        if (auto result = writeHID(device_handle, cmd); !result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, 31> cmd {};
        cmd[0] = 0x21;
        cmd[1] = 0xbb;
        cmd[2] = 0x13;
        cmd[3] = enabled ? 0x01 : 0x00;

        if (auto result = writeHID(device_handle, cmd); !result) {
            return result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }
};
} // namespace headsetcontrol

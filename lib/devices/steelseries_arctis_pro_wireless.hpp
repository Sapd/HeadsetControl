#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Pro Wireless Gaming Headset
 *
 * Features:
 * - Battery status (0-4 range)
 * - Sidetone control (0-9 levels)
 * - Inactive time (10-minute increments)
 *
 * Special protocol with base station communication
 */
class SteelSeriesArctisProWireless : public protocols::SteelSeriesLegacyDevice<SteelSeriesArctisProWireless> {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x1290 // Arctis Pro Wireless
    };

    static constexpr uint8_t BATTERY_MIN     = 0x00;
    static constexpr uint8_t BATTERY_MAX     = 0x04;
    static constexpr uint8_t HEADSET_ONLINE  = 0x04;
    static constexpr uint8_t HEADSET_OFFLINE = 0x02;

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Pro Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        return { .interface = 0 };
    }

    // Override save state for Arctis Pro Wireless
    Result<void> saveState(hid_device* device_handle) const override
    {
        std::array<uint8_t, PACKET_SIZE_31> data { 0x90, 0xAA };
        return writeHID(device_handle, data);
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // First check if headset is connected
        std::array<uint8_t, PACKET_SIZE_31> status_request { 0x41, 0xAA };
        if (auto result = writeHID(device_handle, status_request); !result) {
            return result.error();
        }

        std::array<uint8_t, 2> status_response {};
        auto status_read = readHIDTimeout(device_handle, status_response, hsc_device_timeout);
        if (!status_read) {
            return status_read.error();
        }

        if (status_response[0] == HEADSET_OFFLINE) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        // Now request battery level
        std::array<uint8_t, PACKET_SIZE_31> battery_request { 0x40, 0xAA };
        if (auto result = writeHID(device_handle, battery_request); !result) {
            return result.error();
        }

        std::array<uint8_t, 1> battery_response {};
        auto battery_read = readHIDTimeout(device_handle, battery_response, hsc_device_timeout);
        if (!battery_read) {
            return battery_read.error();
        }

        int bat_raw = battery_response[0];
        int level   = map(bat_raw, BATTERY_MIN, BATTERY_MAX, 0, 100);

        return BatteryResult {
            .level_percent = level,
            .status        = BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { battery_response.begin(), battery_response.end() }
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Range: 0x00 to 0x09
        uint8_t mapped = map(level, 0, 128, 0x00, 0x09);

        std::array<uint8_t, 3> cmd { 0x39, 0xAA, mapped };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0x00,
            .device_max    = 0x09
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Value sent in 10-minute increments
        uint32_t time = minutes / 10;
        std::array<uint8_t, 3> cmd { 0x3c, 0xAA, static_cast<uint8_t>(time) };

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
};
} // namespace headsetcontrol

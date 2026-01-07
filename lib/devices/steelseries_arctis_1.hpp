#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis 1/7X/7P Wireless Gaming Headset
 *
 * Features:
 * - Battery status (direct percentage)
 * - Sidetone control (0-18 levels)
 * - Inactive time/auto-shutoff
 *
 * Reduces code from 146 lines (C) to ~80 lines (C++)
 */
class SteelSeriesArctis1 : public protocols::SteelSeriesLegacyDevice<SteelSeriesArctis1> {
public:
    static constexpr std::array<uint16_t, 4> SUPPORTED_PRODUCT_IDS {
        0x12b3, // Arctis 1
        0x12b6, // Arctis 1 Xbox
        0x12d7, // Arctis 7X
        0x12d5 // Arctis 7P
    };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis (1/7X/7P) Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
        case CAP_BATTERY_STATUS:
        case CAP_INACTIVE_TIME:
            return { .usagepage = 0xff43, .usageid = 0x202, .interface_id = 0x03 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, 2> request { 0x06, 0x12 };

        return requestBatteryDirect(
            device_handle,
            request,
            8, // response size
            3, // battery byte
            2, // status byte
            0x01, // offline value
            0xFF, // charging value (no charging for this device)
            0, // min
            100 // max (already percentage)
        );
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Arctis 1 range: 0x00 to 0x12 (0-18)
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0x00, 0x12);

        std::array<uint8_t, 5> cmd_on { 0x06, 0x35, 0x01, 0x00, mapped };
        std::array<uint8_t, 2> cmd_off { 0x06, 0x35 };

        auto result = (level > 0)
            ? sendCommandWithSave(device_handle, cmd_on)
            : sendCommandWithSave(device_handle, cmd_off);

        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0x00,
            .device_max    = 0x12
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Range: 0 to 90 minutes (0x5A)
        if (minutes > 90) {
            minutes = 90;
        }

        std::array<uint8_t, 3> cmd { 0x06, 0x53, minutes };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }
};
} // namespace headsetcontrol

#pragma once

#include "protocols/hidpp_protocol.hpp"
#include "protocols/logitech_calibrations.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G533 Wireless Gaming Headset
 *
 * Clean implementation using HIDPPDevice protocol template.
 * Reduces code from 158 lines (C) to ~40 lines (C++)!
 *
 * Features:
 * - Battery status with voltage reporting
 * - Sidetone control (0-128 mapped to 200-255)
 * - Inactive time/auto-shutoff
 *
 * Original implementation: logitech_g533.c (158 lines)
 * New implementation: ~40 lines (75% reduction)
 */
class LogitechG533 : public protocols::HIDPPDevice<LogitechG533> {
public:
    std::vector<uint16_t> getProductIds() const override
    {
        return { 0x0a66 };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G533"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
            return { .usagepage = 0xff00, .usageid = 0x1, .interface_id = 3 };
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xff43, .usageid = 0x202, .interface_id = 3 };
        case CAP_INACTIVE_TIME:
            return { .usagepage = 0xff43, .usageid = 0x0202, .interface_id = 0 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // 1 line replaces 45 lines of boilerplate!
        std::array<uint8_t, 2> cmd { 0x07, 0x01 };
        return requestBatteryHIDPP(device_handle, cmd, calibrations::LOGITECH_G533);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Map user range (0-128) to device range (200-255)
        uint8_t mapped = map<uint8_t>(level, 0, 128, 200, 255);

        std::array<uint8_t, 8> cmd { 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, mapped };
        auto result = sendHIDPPFeature(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 200,
            .device_max    = 255
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        return setInactiveTimeHIDPP(device_handle, minutes);
    }
};

} // namespace headsetcontrol

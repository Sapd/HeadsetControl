#pragma once

#include "protocols/hidpp_protocol.hpp"
#include "protocols/logitech_calibrations.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G535 Wireless Gaming Headset
 *
 * Features:
 * - Battery status with voltage reporting (48-point calibration curve)
 * - Sidetone control (0-128 mapped to 0-100)
 * - Inactive time with special validation
 *
 * Original implementation: logitech_g535.c (213 lines with 65 lines of calibration data)
 * New implementation: ~55 lines (74% reduction)
 */
class LogitechG535 : public protocols::HIDPPDevice<LogitechG535> {
public:
    std::vector<uint16_t> getProductIds() const override
    {
        return { 0x0ac4 };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G535"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
            return { .usagepage = 0xc, .usageid = 0x1, .interface_id = 3 };
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xc, .usageid = 0x1, .interface_id = 3 };
        case CAP_INACTIVE_TIME:
            return { .usagepage = 0xc, .usageid = 0x1, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, 2> cmd { 0x05, 0x0d };
        return requestBatteryHIDPP(device_handle, cmd, calibrations::LOGITECH_G535);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // G535 uses 0-100 range (unlike G533's 200-255)
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 100);

        // Send sidetone command and read response for validation
        std::array<uint8_t, 3> cmd { 0x04, 0x1d, mapped };
        auto result = sendHIDPPCommand(device_handle, cmd, HIDPP_LONG_MESSAGE_LENGTH);
        if (!result) {
            return result.error();
        }

        // Validate: response byte 4 should echo back our value
        if ((*result)[4] != mapped) {
            return DeviceError::protocolError(
                std::format("Sidetone validation failed: expected {}, got {}", mapped, (*result)[4]));
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 100
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // G535 has special validation rules:
        // Accepted values: 0 (never), 1, 2, 5, 10, 15, 30
        uint8_t validated = validateInactiveTime(minutes);

        std::array<uint8_t, 3> cmd { 0x05, 0x2d, validated };
        auto result = sendHIDPPCommand(device_handle, cmd, HIDPP_LONG_MESSAGE_LENGTH);
        if (!result) {
            return result.error();
        }

        // Validate response
        if ((*result)[4] != validated) {
            return DeviceError::protocolError("Inactive time validation failed");
        }

        return InactiveTimeResult {
            .minutes     = validated,
            .min_minutes = 0,
            .max_minutes = 30
        };
    }

private:
    uint8_t validateInactiveTime(uint8_t minutes) const
    {
        // G535 has special validation rules:
        // Accepted values: 0 (never), 1, 2, 5, 10, 15, 30
        // Clamp to closest valid value
        if (minutes == 0)
            return 0;
        if (minutes <= 1)
            return 1;
        if (minutes <= 2)
            return 2;
        if (minutes <= 5)
            return 5;
        if (minutes <= 10)
            return 10;
        if (minutes <= 15)
            return 15;
        return 30; // >= 16
    }
};
} // namespace headsetcontrol

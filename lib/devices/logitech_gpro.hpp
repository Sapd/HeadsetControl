#pragma once

#include "protocols/hidpp_protocol.hpp"
#include "protocols/logitech_calibrations.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G PRO Gaming Headsets (Pro, Pro X, Pro X2)
 *
 * Features:
 * - Battery status with voltage reporting
 * - Sidetone control
 * - Inactive time
 *
 * Original implementations:
 * - logitech_gpro.c (130 lines)
 * - logitech_gpro_x2.c (142 lines)
 * Total: 272 lines
 * New implementation: ~50 lines (82% reduction!)
 */
class LogitechGPro : public protocols::HIDPPDevice<LogitechGPro> {
public:
    std::vector<uint16_t> getProductIds() const override
    {
        return {
            0x0aa7, // G PRO
            0x0aaa, // G PRO X variant 0
            0x0aba, // G PRO X variant 1
            0x0afb, // G PRO X2 variant 0
            0x0afc, // G PRO X2 variant 1
            0x0af7  // G PRO X2 variant 2
        };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G PRO Series"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_INACTIVE_TIME:
            return { .usagepage = 0xff43, .usageid = 0x0202 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // GPro uses feature 0x06 0x0d for battery
        std::array<uint8_t, 2> cmd { 0x06, 0x0d };
        return requestBatteryHIDPP(device_handle, cmd, calibrations::LOGITECH_GPRO);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 100);

        std::array<uint8_t, 3> cmd { 0x05, 0x1a, mapped };
        auto result = sendHIDPPFeature(device_handle, cmd);
        if (!result) {
            return result.error();
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
        return setInactiveTimeHIDPP(device_handle, minutes);
    }
};
} // namespace headsetcontrol

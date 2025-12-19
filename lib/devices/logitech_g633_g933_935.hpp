#pragma once

#include "protocols/hidpp_protocol.hpp"
#include "protocols/logitech_calibrations.hpp"
#include "utility.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G633/G635/G733/G933/G935 Gaming Headsets
 *
 * This family of headsets shares similar protocols with minor variations.
 * Credit to https://github.com/ashkitten/g933-utils/ for protocol details.
 *
 * Features:
 * - Battery status with voltage reporting
 * - Sidetone control (0-128 mapped to 0-100)
 * - RGB lights control (breathing effect)
 *
 * Original implementation: logitech_g633_g933_935.c (144 lines)
 * New implementation: ~70 lines (51% reduction)
 */
class LogitechG633Family : public protocols::HIDPPDevice<LogitechG633Family> {
public:
    std::vector<uint16_t> getProductIds() const override
    {
        return {
            0x0a5c, // G633
            0x0a89, // G635
            0x0a5b, // G933
            0x0a87, // G935
            0x0ab5, // G733
            0x0afe, // G733 variant 2
            0x0b1f // G733 variant 3
        };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G633/G635/G733/G933/G935"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
        case CAP_BATTERY_STATUS:
        case CAP_LIGHTS:
            return { .usagepage = 0xff43, .usageid = 0x0202 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Uses feature 0x08 0x0a for battery voltage
        std::array<uint8_t, 2> cmd { 0x08, 0x0a };
        return requestBatteryHIDPP(device_handle, cmd, calibrations::LOGITECH_G633);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Clamp to 0-100 range
        uint8_t clamped = std::min(level, static_cast<uint8_t>(100));

        std::array<uint8_t, 3> cmd { 0x07, 0x1a, clamped };
        auto result = sendHIDPPFeature(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = clamped,
            .min_level     = 0,
            .max_level     = 100,
            .device_min    = 0,
            .device_max    = 100
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // G933/G935 support breathing effect on side strips and logo
        // Packet format: 11 ff 04 3c <zone> <mode> ...
        // zone: 0=logo, 1=strips
        // mode: 0=off, 2=breathing

        // Turn on/off side strips
        std::array<uint8_t, 14> strips_data {
            0x04, 0x3c, 0x01, // Feature + zone(strips)
            static_cast<uint8_t>(on ? 0x02 : 0x00), // mode (breathing or off)
            0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00
        };

        auto result1 = sendHIDPPFeature(device_handle, strips_data);
        if (!result1) {
            return result1.error();
        }

        // Small delay between commands (Windows requirement)
        sleep_us(1000);

        // Turn on/off logo
        std::array<uint8_t, 14> logo_data {
            0x04, 0x3c, 0x00, // Feature + zone(logo)
            static_cast<uint8_t>(on ? 0x02 : 0x00), // mode (breathing or off)
            0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00
        };

        auto result2 = sendHIDPPFeature(device_handle, logo_data);
        if (!result2) {
            return result2.error();
        }

        return LightsResult {
            .enabled = on,
            .mode    = on ? "breathing" : "off"
        };
    }
};
} // namespace headsetcontrol

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
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS) | B(CAP_LIGHT_COLOR) | B(CAP_LIGHT_BRIGHTNESS) | B(CAP_LIGHT_MODE) | B(CAP_LIGHT_SPEED);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
        case CAP_BATTERY_STATUS:
        case CAP_LIGHTS:
        case CAP_LIGHT_COLOR:
        case CAP_LIGHT_BRIGHTNESS:
        case CAP_LIGHT_MODE:
        case CAP_LIGHT_SPEED:
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

        // Default to a bright cyan when turning lights on
        const uint8_t default_r         = 0x00;
        const uint8_t default_g         = 0xff;
        const uint8_t default_b         = 0xff;
        const uint8_t default_brightness = 100;

        auto result1 = sendZoneLighting(device_handle, 0x01, on ? 1 : 0, default_r, default_g, default_b, default_brightness, default_light_speed_);
        if (!result1) {
            return result1.error();
        }

        // Small delay between commands (Windows requirement)
        sleep_ms(1);

        // Turn on/off logo
        auto result2 = sendZoneLighting(device_handle, 0x00, on ? 1 : 0, default_r, default_g, default_b, default_brightness, default_light_speed_);
        if (!result2) {
            return result2.error();
        }

        return LightsResult {
            .enabled = on,
            .mode    = on ? "static" : "off"
        };
    }

    Result<LightsResult> setLightColor(hid_device* device_handle, uint8_t red, uint8_t green, uint8_t blue) override
    {
        constexpr uint8_t brightness = 100;

        auto result1 = sendZoneLighting(device_handle, 0x01, 1, red, green, blue, brightness, light_speed_);
        if (!result1) {
            return result1.error();
        }

        sleep_ms(1);

        auto result2 = sendZoneLighting(device_handle, 0x00, 1, red, green, blue, brightness, light_speed_);
        if (!result2) {
            return result2.error();
        }

        return LightsResult {
            .enabled = true,
            .mode    = "static"
        };
    }

    Result<LightsResult> setLightBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        uint8_t clamped = std::clamp<uint8_t>(brightness, 1, 100);

        // Keep current color roughly cyan if we don't know previous color
        const uint8_t r = 0x00;
        const uint8_t g = 0xff;
        const uint8_t b = 0xff;

        auto result1 = sendZoneLighting(device_handle, 0x01, 1, r, g, b, clamped, light_speed_);
        if (!result1) {
            return result1.error();
        }

        sleep_ms(1);

        auto result2 = sendZoneLighting(device_handle, 0x00, 1, r, g, b, clamped, light_speed_);
        if (!result2) {
            return result2.error();
        }

        return LightsResult {
            .enabled = clamped > 0,
            .mode    = "static"
        };
    }

    Result<LightsResult> setLightMode(hid_device* device_handle, uint8_t mode) override
    {
        if (mode < 1 || mode > 3) {
            return DeviceError::invalidParameter("Mode must be 1(static), 2(breathing), or 3(wave)");
        }
        light_mode_ = mode;

        // Keep a visible default color while changing animation mode.
        const uint8_t r          = 0x00;
        const uint8_t g          = 0xff;
        const uint8_t b          = 0xff;
        const uint8_t brightness = 100;

        auto result1 = sendZoneLighting(device_handle, 0x01, mode, r, g, b, brightness, light_speed_);
        if (!result1) {
            return result1.error();
        }

        sleep_ms(1);

        auto result2 = sendZoneLighting(device_handle, 0x00, mode, r, g, b, brightness, light_speed_);
        if (!result2) {
            return result2.error();
        }

        std::string mode_name = "static";
        if (mode == 2) {
            mode_name = "breathing";
        } else if (mode == 3) {
            mode_name = "wave";
        }

        return LightsResult {
            .enabled = true,
            .mode    = mode_name
        };
    }

    Result<LightsResult> setLightSpeed(hid_device* device_handle, uint8_t speed) override
    {
        uint8_t clamped = std::clamp<uint8_t>(speed, 1, 100);
        light_speed_    = clamped;

        // Static mode does not use animation speed.
        if (light_mode_ == 1) {
            return LightsResult {
                .enabled = true,
                .mode    = "static"
            };
        }

        const uint8_t r          = 0x00;
        const uint8_t g          = 0xff;
        const uint8_t b          = 0xff;
        const uint8_t brightness = 100;

        auto result1 = sendZoneLighting(device_handle, 0x01, light_mode_, r, g, b, brightness, light_speed_);
        if (!result1) {
            return result1.error();
        }

        sleep_ms(1);

        auto result2 = sendZoneLighting(device_handle, 0x00, light_mode_, r, g, b, brightness, light_speed_);
        if (!result2) {
            return result2.error();
        }

        return LightsResult {
            .enabled = true,
            .mode    = light_mode_ == 2 ? "breathing" : "wave"
        };
    }

private:
    static constexpr uint8_t default_light_speed_ = 50;
    mutable uint8_t light_mode_                   = 1;
    mutable uint8_t light_speed_                  = default_light_speed_;

    Result<void> sendZoneLighting(hid_device* device_handle,
        uint8_t zone,
        uint8_t mode,
        uint8_t red,
        uint8_t green,
        uint8_t blue,
        uint8_t brightness,
        uint8_t speed) const
    {
        // Logitech G733 uses a shifted channel layout in this packet:
        // byte4=R, byte5=G, byte6=B. byte7 is not a color channel.
        // Using byte7 for blue caused blue to switch the LEDs off.
        std::array<uint8_t, 14> data {
            0x04, 0x3c, zone,
            mode,
            red,
            green,
            blue,
            0x0f, // non-color control byte
            mapSpeedToDevice(speed),
            0x00,
            brightness,
            0x00,
            0x00,
            0x00
        };

        auto result = sendHIDPPFeature(device_handle, data);
        if (!result) {
            return result.error();
        }
        return {};
    }

    [[nodiscard]] static uint8_t mapSpeedToDevice(uint8_t speed)
    {
        uint8_t clamped = std::clamp<uint8_t>(speed, 1, 100);
        // Scale 1-100 to 1-255 for Logitech animation-speed byte.
        return static_cast<uint8_t>(1 + ((static_cast<uint16_t>(clamped - 1) * 254) / 99));
    }
};
} // namespace headsetcontrol

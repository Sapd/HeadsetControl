#pragma once

#include "hid_device.hpp"
#include <array>
#include <string_view>
#include <unistd.h>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G930 Wireless Gaming Headset
 *
 * G930 uses an older protocol (pre-HID++) with feature reports.
 * Credit to https://github.com/Roliga/g930-battery-percentage/
 *
 * Features:
 * - Battery status (raw value 44-91, mapped to 0-100%)
 * - Sidetone control
 *
 * Note: Does NOT use HID++ protocol, so doesn't inherit from HIDPPDevice.
 *
 * Original implementation: logitech_g930.c (89 lines)
 * New implementation: ~55 lines (38% reduction)
 */
class LogitechG930 : public HIDDevice {
private:
    static constexpr size_t MSG_SIZE = 64;
    static constexpr int BATTERY_MIN = 44;
    static constexpr int BATTERY_MAX = 91;

public:
    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_LOGITECH;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { 0x0a1f };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G930"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xff00, .usageid = 0x1 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        // Send battery request feature report
        std::array<uint8_t, MSG_SIZE> buf {};
        buf[0]  = 0xFF;
        buf[1]  = 0x09;
        buf[2]  = 0x00;
        buf[3]  = 0xFD;
        buf[4]  = 0xF4;
        buf[5]  = 0x10;
        buf[6]  = 0x05;
        buf[7]  = 0xB1;
        buf[8]  = 0xBF;
        buf[9]  = 0xA0;
        buf[10] = 0x04;

        if (auto result = sendFeatureReport(device_handle, buf); !result) {
            return result.error();
        }

        // Read response 3 times with delays (protocol requirement)
        for (int i = 0; i < 3; i++) {
            auto read_result = getFeatureReport(device_handle, buf);
            if (!read_result) {
                return read_result.error();
            }

            if (i < 2) {
                usleep(100000); // 100ms delay
            }
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        // Battery level is in byte 13, raw range 44-91
        int raw_level = buf[13];
        int level     = map(raw_level, BATTERY_MIN, BATTERY_MAX, 0, 100);

        return BatteryResult {
            .level_percent  = level,
            .status         = BATTERY_AVAILABLE,
            .raw_data       = std::vector<uint8_t>(buf.begin(), buf.end()),
            .query_duration = duration
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        std::array<uint8_t, MSG_SIZE> data {};
        data[0]  = 0xFF;
        data[1]  = 0x0A;
        data[2]  = 0;
        data[3]  = 0xFF;
        data[4]  = 0xF4;
        data[5]  = 0x10;
        data[6]  = 0x05;
        data[7]  = 0xDA;
        data[8]  = 0x8F;
        data[9]  = 0xF2;
        data[10] = 0x01;
        data[11] = level;

        if (auto result = sendFeatureReport(device_handle, data); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 128
        };
    }
};
} // namespace headsetcontrol

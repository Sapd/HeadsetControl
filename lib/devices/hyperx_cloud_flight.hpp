#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <cmath>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud Flight Wireless Gaming Headset
 *
 * Features:
 * - Battery status (voltage-based with polynomial estimation)
 *
 * Voltage range: 3300-4200mV with polynomial curve fitting
 */
class HyperXCloudFlight : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_HYPERX = 0x0951;
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x16C4, // Cloud Flight Old
        0x1723 // Cloud Flight New
    };

    static constexpr int TIMEOUT_MS                      = 2000;
    static constexpr uint16_t VOLTAGE_CHARGING_THRESHOLD = 0x100B;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_HYPERX;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud Flight Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    /**
     * @brief Estimate battery level from voltage using polynomial curve
     *
     * Derived from Logitech G633/G933/935 battery estimation
     * Uses different polynomial segments for accurate estimation
     */
    static float estimateBatteryLevel(uint16_t voltage)
    {
        if (voltage <= 3648) {
            return 0.00125f * voltage;
        }
        if (voltage > 3975) {
            return 100.0f;
        }

        // Polynomial curve for mid-range voltages
        float v = static_cast<float>(voltage);
        return 0.00000002547505f * std::pow(v, 4) - 0.0003900299f * std::pow(v, 3)
            + 2.238321f * std::pow(v, 2) - 5706.256f * v + 5452299.0f;
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Request battery voltage
        std::array<uint8_t, 20> request {
            0x21, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        std::array<uint8_t, 20> response {};
        auto read_result = readHIDTimeout(device_handle, response, TIMEOUT_MS);

        if (!read_result) {
            return read_result.error();
        }

        // Check if we got valid battery response (15 or 20 bytes)
        size_t bytes_read = *read_result;
        if (bytes_read != 0xf && bytes_read != 0x14) {
            return DeviceError::protocolError("Unexpected response length");
        }

        // Battery voltage in millivolts (big-endian)
        uint32_t voltage = (response[3] << 8) | response[4];

        // Check if charging
        if (voltage > VOLTAGE_CHARGING_THRESHOLD) {
            return BatteryResult {
                .level_percent = -1,
                .status        = BATTERY_CHARGING,
                .raw_data      = std::vector<uint8_t> { response.begin(), response.end() }
            };
        }

        // Estimate battery level from voltage
        int level = static_cast<int>(std::roundf(estimateBatteryLevel(static_cast<uint16_t>(voltage))));

        return BatteryResult {
            .level_percent = level,
            .status        = BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { response.begin(), response.end() }
        };
    }
};
} // namespace headsetcontrol

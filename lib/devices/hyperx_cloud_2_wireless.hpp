#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud II Wireless Gaming Headset
 *
 * Features:
 * - Battery status (percentage-based with charging detection)
 *
 * Uses HP vendor ID and special usage page/ID
 */
class HyperXCloud2Wireless : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_HYPERX = 0x03f0;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x0696 // Cloud II Wireless
    };

    static constexpr int REQUEST_SIZE  = 52;
    static constexpr int RESPONSE_SIZE = 20;
    static constexpr int TIMEOUT_MS    = 2000;

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
        return "HyperX Cloud II Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        if (cap == CAP_BATTERY_STATUS) {
            return { .usagepage = 0xFF90, .usageid = 0x0303, .interface_id = 0 };
        }
        return HIDDevice::getCapabilityDetail(cap);
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Request battery status
        std::array<uint8_t, REQUEST_SIZE> request {};
        request[0] = 0x06;
        request[1] = 0xff;
        request[2] = 0xbb;
        request[3] = 0x02;

        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        std::array<uint8_t, RESPONSE_SIZE> response {};
        auto read_result = readHIDTimeout(device_handle, response, TIMEOUT_MS);

        if (!read_result) {
            return read_result.error();
        }
        if (*read_result != RESPONSE_SIZE) {
            return DeviceError::protocolError("Invalid response size");
        }

        int level                  = response[7];
        enum battery_status status = BATTERY_AVAILABLE;

        if (response[5] == 0x10) {
            status = BATTERY_CHARGING;
        }

        return BatteryResult {
            .level_percent = level,
            .status        = status,
            .raw_data      = std::vector<uint8_t> { response.begin(), response.end() }
        };
    }
};
} // namespace headsetcontrol

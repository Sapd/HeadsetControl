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

    static constexpr int WRITE_PACKET_SIZE = 52;
    static constexpr int WRITE_TIMEOUT     = 100;
    static constexpr int READ_PACKET_SIZE  = 20;
    static constexpr int READ_TIMEOUT      = 1000;

    static constexpr int CMD_GET_BATTERY_LEVEL = 0x02;
    static constexpr int VOLTAGE_INDEX_1       = 0x05;
    static constexpr int VOLTAGE_INDEX_2       = 0x06;
    static constexpr int BATTERY_LEVEL_INDEX   = 0x07;

    static constexpr int CMD_GET_BATTERY_CHARGING = 0x03;
    static constexpr int BATTERY_CHARGING_INDEX   = 0x04;

    static constexpr int CMD_SET_INACTIVE_TIME = 0x22;
    static constexpr int CMD_GET_INACTIVE_TIME = 0x07;
    static constexpr int INACTIVE_TIME_INDEX   = 0x04;

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
        return B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        if (cap == CAP_BATTERY_STATUS || cap == CAP_INACTIVE_TIME) {
            return { .usagepage = 0xFF90, .usageid = 0x0303, .interface_id = 0 };
        }
        return HIDDevice::getCapabilityDetail(cap);
    }

    Result<std::array<uint8_t, READ_PACKET_SIZE>> sendCommand(hid_device* device_handle, uint8_t command, std::optional<uint8_t> value)
    {
        std::array<uint8_t, WRITE_PACKET_SIZE> request {};
        if (value) {
            request = { 0x06, 0xff, 0xbb, command, *value, 0x00 };
        } else {
            request = { 0x06, 0xff, 0xbb, command, 0x00 };
        }

        auto wr = writeHID(device_handle, request);
        std::this_thread::sleep_for(std::chrono::milliseconds(WRITE_TIMEOUT));
        if (!wr) {
            return wr.error();
        }

        std::array<uint8_t, READ_PACKET_SIZE> response {};
        auto rd = readHIDTimeout(device_handle, response, READ_TIMEOUT);
        if (!rd) {
            return rd.error();
        }
        if (*rd != READ_PACKET_SIZE) {
            return DeviceError::protocolError("Invalid response size");
        }
        if (response[0] != 0x06 || response[1] != 0xFF || response[2] != 0xBB || response[3] != command) {
            return DeviceError::protocolError("Invalid response header");
        }
        if (value && response[4] != *value) {
            return DeviceError::protocolError("Response value does not match request");
        }

        return response;
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto level_res = sendCommand(device_handle, CMD_GET_BATTERY_LEVEL, std::nullopt);
        if (!level_res) {
            return level_res.error();
        }

        auto charging_res = sendCommand(device_handle, CMD_GET_BATTERY_CHARGING, std::nullopt);
        if (!charging_res) {
            return charging_res.error();
        }

        return BatteryResult {
            .level_percent = (*level_res)[BATTERY_LEVEL_INDEX],
            .status        = ((*charging_res)[BATTERY_CHARGING_INDEX] == 1) ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .voltage_mv    = (static_cast<int>((*level_res)[VOLTAGE_INDEX_1]) << 8) | static_cast<int>((*level_res)[VOLTAGE_INDEX_2]),
            .raw_data      = std::vector<uint8_t>(level_res->begin(), level_res->end())
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t value) override
    {
        auto res = sendCommand(device_handle, CMD_SET_INACTIVE_TIME, value);
        if (!res) {
            return res.error();
        }

        return InactiveTimeResult {
            .minutes     = (*res)[INACTIVE_TIME_INDEX],
            .min_minutes = 0,
            .max_minutes = 255
        };
    }
};
} // namespace headsetcontrol

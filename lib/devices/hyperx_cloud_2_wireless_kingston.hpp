#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <string_view>
#include <thread>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud II Wireless Gaming Headset (Kingston-branded revision)
 *
 * Uses a different protocol than the HP-branded version (0x03f0:0x0696).
 * Protocol reverse-engineered from HyperHeadset project:
 * https://github.com/LennardKittner/HyperHeadset
 *
 * Key differences:
 * - 62-byte packets instead of 52
 * - Different base packet structure
 * - Response uses Report ID 11 (0x0B) instead of 6
 * - Battery level at response[7]
 */
class HyperXCloud2WirelessKingston : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_KINGSTON = 0x0951;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS_KINGSTON {
        0x1718 // Cloud II Wireless (Kingston)
    };

    static constexpr int WRITE_PACKET_SIZE = 62;
    static constexpr int WRITE_TIMEOUT     = 100;
    static constexpr int READ_PACKET_SIZE  = 64;
    static constexpr int READ_TIMEOUT      = 1000;

    static constexpr uint8_t CMD_GET_BATTERY_LEVEL    = 0x02;
    static constexpr uint8_t CMD_GET_BATTERY_CHARGING = 0x03;
    static constexpr uint8_t CMD_SET_AUTO_SHUTDOWN    = 0x18;
    static constexpr uint8_t CMD_SET_SIDETONE         = 0x19;

    static constexpr int BATTERY_LEVEL_INDEX   = 7;
    static constexpr int CHARGING_STATUS_INDEX = 4;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_KINGSTON;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS_KINGSTON.begin(), SUPPORTED_PRODUCT_IDS_KINGSTON.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud II Wireless (Kingston)"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME);
    }

private:
    std::array<uint8_t, WRITE_PACKET_SIZE> buildRequest(uint8_t command, uint8_t payload = 0) const
    {
        std::array<uint8_t, WRITE_PACKET_SIZE> request { };
        request[0]  = 0x06;
        request[1]  = 0x00;
        request[2]  = 0x02;
        request[3]  = 0x00;
        request[4]  = 0x9A;
        request[5]  = 0x00;
        request[6]  = 0x00;
        request[7]  = 0x68;
        request[8]  = 0x4A;
        request[9]  = 0x8E;
        request[10] = 0x0A;
        request[11] = 0x00;
        request[12] = 0x00;
        request[13] = 0x00;
        request[14] = 0xBB;
        request[15] = command;
        request[16] = payload;
        return request;
    }

    void prepareDevice(hid_device* device_handle) const
    {
        std::array<uint8_t, 64> input_report { };
        input_report[0] = 0x06;
        // Attempt to read input report before writing (may fail, ignore error)
        [[maybe_unused]] auto _ = getInputReport(device_handle, input_report);
    }

public:
    Result<std::array<uint8_t, READ_PACKET_SIZE>> sendCommand(hid_device* device_handle, uint8_t command, uint8_t payload = 0)
    {
        prepareDevice(device_handle);
        auto request = buildRequest(command, payload);

        auto wr = writeHID(device_handle, request);
        std::this_thread::sleep_for(std::chrono::milliseconds(WRITE_TIMEOUT));
        if (!wr) {
            return wr.error();
        }

        std::array<uint8_t, READ_PACKET_SIZE> response { };
        auto rd = readHIDTimeout(device_handle, response, READ_TIMEOUT);

        if (!rd) {
            return rd.error();
        }

        // Response format: [11, 0, 187, cmd_id, ...]
        if (response[0] != 0x0B || response[2] != 0xBB || response[3] != command) {
            return DeviceError::protocolError("Invalid response header");
        }

        return response;
    }

    Result<void> sendCommandFireAndForget(hid_device* device_handle, uint8_t command, uint8_t payload = 0)
    {
        prepareDevice(device_handle);
        auto request = buildRequest(command, payload);
        return writeHID(device_handle, request);
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto level_res = sendCommand(device_handle, CMD_GET_BATTERY_LEVEL);
        if (!level_res) {
            return level_res.error();
        }

        auto charging_res = sendCommand(device_handle, CMD_GET_BATTERY_CHARGING);
        if (!charging_res) {
            return charging_res.error();
        }

        return BatteryResult {
            .level_percent = (*level_res)[BATTERY_LEVEL_INDEX],
            .status = ((*charging_res)[CHARGING_STATUS_INDEX] == 1) ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .voltage_mv = std::nullopt,
            .raw_data = std::vector<uint8_t>(level_res->begin(), level_res->end())
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Protocol only supports binary on/off (1 or 0)
        uint8_t hardware_level = (level > 0) ? 1 : 0;
        auto res = sendCommandFireAndForget(device_handle, CMD_SET_SIDETONE, hardware_level);
        if (!res) {
            return res.error();
        }

        return SidetoneResult {
            .current_level = hardware_level,
            .min_level = 0,
            .max_level = 128,
            .device_min = 0,
            .device_max = 1
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Hardware limit: 30 minutes maximum
        uint8_t hardware_mins = (minutes > 30) ? 30 : minutes;
        auto res = sendCommand(device_handle, CMD_SET_AUTO_SHUTDOWN, hardware_mins);
        if (!res) {
            return res.error();
        }

        return InactiveTimeResult {
            .minutes = hardware_mins,
            .min_minutes = 0,
            .max_minutes = 30
        };
    }
};

} // namespace headsetcontrol

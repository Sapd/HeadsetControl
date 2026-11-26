#pragma once

#include "device.hpp"
#include "hid_device.hpp"
#include "hidapi.h"
#include "result_types.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {
/**
 * @brief Moondrop GM01 Pro
 *
 * Features:
 * - Battery level
 */

class MoondropGM01Pro : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0133 };

    static constexpr std::chrono::milliseconds MIN_GAP_TIME { 50 };

    static constexpr unsigned int MSG_SIZE = 64;
    using Message                          = std::vector<uint8_t>;

    static constexpr unsigned int OFFSET_REPORT_ID = 0;
    static constexpr unsigned int OFFSET_MSG_TYPE  = 1;
    static constexpr unsigned int OFFSET_CMD       = 2;
    static constexpr unsigned int OFFSET_DATA_LEN  = 3;

    static constexpr unsigned int OFFSET_LINK_STATUS   = 4;
    static constexpr unsigned int OFFSET_BATTERY_LEVEL = 9;

    static constexpr uint8_t REPORT_ID      = 0x4B;
    static constexpr uint8_t MSG_TYPE_READ  = 0x80;
    static constexpr uint8_t MSG_TYPE_WRITE = 0x01;

    static constexpr uint8_t CMD_LINK    = 0x31;
    static constexpr uint8_t CMD_BATTERY = 0x27;
    static constexpr uint8_t CMD_LED     = 0x28;

    uint16_t getVendorId() const override
    {
        return 0x35D8;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Moondrop GM01 Pro";
    }

    int getCapabilities() const override
    {
        // Return bitmask of supported capabilities
        return B(CAP_BATTERY_STATUS);
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Untested on Windows and Mac
        return PLATFORM_LINUX;
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        return { .usagepage = 0, .usageid = 0, .interface_id = 3 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto link_result = checkLink(device_handle);
        if (link_result.hasError()) {
            return link_result.error();
        }

        if (!link_result.value()) {
            return DeviceError::deviceOffline("Headset is not connected to the dongle");
        }

        Result<void> send_msg_result = sendMessage(device_handle, CMD_BATTERY);

        if (send_msg_result.hasError()) {
            return send_msg_result.error();
        }

        auto response = receiveMessage(device_handle, CMD_BATTERY);

        if (!response) {
            return response.error();
        }

        const auto& battery_response = response.value();
        return BatteryResult {
            .level_percent = battery_response.at(OFFSET_BATTERY_LEVEL),
            .status        = BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { battery_response.begin(), battery_response.end() }
        };
    };

private:
    using clock                 = std::chrono::steady_clock;
    using milliseconds          = std::chrono::milliseconds;
    clock::time_point last_send = clock::now() - MIN_GAP_TIME;

    Result<void> sendMessage(hid_device* device_handle, uint8_t cmd, std::span<const uint8_t> data = { }, uint8_t msg_type = MSG_TYPE_READ)
    {

        // Check if trying to send payload on MSG_TYPE_READ
        if (data.size() > 0 && msg_type == MSG_TYPE_READ) {
            std::string error_msg = std::format("Command {:02X} does not accept data on message type MSG_TYPE_READ", cmd);
            return DeviceError::invalidParameter(error_msg);
        }

        // Ensure messages are paced. Will be useful when doing multiple sends
        milliseconds wait = std::chrono::duration_cast<milliseconds>(MIN_GAP_TIME - (clock::now() - last_send));
        if (wait > std::chrono::milliseconds::zero())
            std::this_thread::sleep_for(wait);

        int expected_size = expectedWriteDataSize(cmd);

        if (msg_type == MSG_TYPE_WRITE) {

            if (expected_size == -1) {
                std::string error_msg = std::format("Command {:02X} is not writable or unknown", cmd);
                return DeviceError::invalidParameter(error_msg);
            }

            if (expected_size != data.size()) {
                std::string error_msg = std::format("Command {:02X} expects data size {}, got data size {}", cmd, expected_size, data.size());
                return DeviceError::invalidParameter(error_msg);
            }
        }

        auto data_size = static_cast<uint8_t>(data.size());

        Message request { REPORT_ID, msg_type, cmd, data_size };

        request.insert(request.end(), data.begin(), data.end());
        request.resize(MSG_SIZE, 0);

        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        last_send = clock::now();
        return { };
    }

    Result<Message> receiveMessage(hid_device* device_handle, uint8_t cmd)
    {
        using std::chrono::milliseconds;
        const auto end = clock::now() + milliseconds { hsc_device_timeout };
        Message response(MSG_SIZE);

        while (true) {
            const auto remaining = std::chrono::duration_cast<milliseconds>(end - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                std::string error_msg = std::format("Device did not respond with command: {:02X} in time", cmd);
                return DeviceError::timeout(error_msg);
            }

            auto read_result = readHIDTimeout(device_handle, response, static_cast<int>(remaining.count()));
            if (!read_result) {

                return read_result.error();
            }

            bool responded = *read_result == MSG_SIZE && response.at(OFFSET_REPORT_ID) == REPORT_ID
                && response.at(OFFSET_MSG_TYPE) == MSG_TYPE_READ && response.at(OFFSET_CMD) == cmd;
            if (!responded) {
                continue; // Ignore unsolicited packets
            }

            int expected_size      = expectedReadDataSize(cmd);
            int response_data_size = static_cast<int>(response.at(OFFSET_DATA_LEN));

            if (expected_size == -1) {
                std::string error_msg = std::format("Command {:02X} is unknown", cmd);
                return DeviceError::hidError(error_msg);
            }

            if (expected_size != response_data_size) {
                std::string error_msg = std::format("Command {:02X} expected data size is {}, got data size {}", cmd, expected_size, response_data_size);
                return DeviceError::hidError(error_msg);
            }

            return response;
        }
    }

    Result<bool> checkLink(hid_device* device_handle)
    {

        auto send_verified = sendMessage(device_handle, CMD_LINK);

        if (send_verified.hasError()) {
            return send_verified.error();
        }

        auto response = receiveMessage(device_handle, CMD_LINK);

        if (!response) {
            return response.error();
        }

        const auto& link_response = response.value();

        return static_cast<bool>(link_response.at(OFFSET_LINK_STATUS));
    }

    static constexpr int expectedWriteDataSize(uint8_t cmd) noexcept
    {
        switch (cmd) {
        case CMD_LED:
            return 1;
        default:
            // Command not writable or unknown command
            return -1;
        }
    }

    static constexpr int expectedReadDataSize(uint8_t cmd) noexcept
    {
        switch (cmd) {
        case CMD_LINK:
            return 1;
        case CMD_BATTERY:
            return 6;
        case CMD_LED:
            return 1;
        default:
            // Unknown command
            return -1;
        }
    }
};
} // namespace headsetcontrol

#pragma once

#include "device.hpp"
#include "hid_device.hpp"
#include "hidapi.h"
#include "result_types.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <sys/types.h>
#include <thread>
#include <unordered_map>
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

    static constexpr uint MSG_SIZE = 64;
    using Message                  = std::vector<uint8_t>;

    static constexpr uint OFFSET_REPORT_ID = 0;
    static constexpr uint OFFSET_MSG_TYPE  = 1;
    static constexpr uint OFFSET_CMD       = 2;
    static constexpr uint OFFSET_DATA_LEN  = 3;

    static constexpr uint8_t REPORT_ID      = 0x4B;
    static constexpr uint8_t MSG_TYPE_READ  = 0x80;
    static constexpr uint8_t MSG_TYPE_WRITE = 0x01;

    static constexpr uint8_t CMD_LINK    = 0x31;
    static constexpr uint8_t CMD_BATTERY = 0x27;

    const ::std::unordered_map<uint8_t, int> EXPECTED_READ_DATA_SIZES = {
        { CMD_LINK, 1 },
        { CMD_BATTERY, 6 },
    };

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

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        return { .usagepage = 0, .usageid = 0, .interface_id = 3 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        Message request { REPORT_ID, MSG_TYPE_READ, CMD_BATTERY };

        if (!checkLink(device_handle)) {
          return DeviceError::deviceOffline("Headset is not connected to the dongle");
        }

        bool send_msg_result = sendMessage(device_handle, CMD_BATTERY);

        if (!send_msg_result) {
            return DeviceError::invalidParameter("Battery request message is malformed");
        }

        auto response = receiveMessage(device_handle, CMD_BATTERY);

        if (!response) {
            return DeviceError::timeout("Battery request timed out");
        }

        const auto& battery_response = *response;
        return BatteryResult {
            .level_percent = battery_response.at(9),
            .status        = BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { battery_response.begin(), battery_response.end() }
        };
    };

private:
    using clock        = std::chrono::steady_clock;
    using milliseconds = std::chrono::milliseconds;
    clock::steady_clock::time_point last_send;

    bool sendMessage(hid_device* device_handle, uint8_t cmd, std::span<const uint8_t> data = { }, uint8_t msg_type = MSG_TYPE_READ)
    {
        // Ensure messages are paced. Will be useful when doing multiple sends
        milliseconds wait = std::chrono::duration_cast<milliseconds>(MIN_GAP_TIME - (clock::now() - last_send));
        if (wait > std::chrono::milliseconds::zero())
            std::this_thread::sleep_for(wait);

        int expected_size = EXPECTED_READ_DATA_SIZES.find(cmd)->second;
        bool verified     = msg_type == MSG_TYPE_READ ? data.empty() : data.size() == expected_size;

        auto data_size = static_cast<uint8_t>(data.size());
        Message request { REPORT_ID, msg_type, cmd, data_size };

        // Return early when data verification fails
        if (!verified) {
            return verified;
        }

        request.insert(request.end(), data.begin(), data.end());
        request.resize(MSG_SIZE, 0);

        if (auto result = writeHID(device_handle, request); !result) {
            return false;
        }


        return verified;
    }

    std::optional<Message> receiveMessage(hid_device* device_handle, uint8_t cmd)
    {
        using std::chrono::milliseconds;
        const auto end = clock::now() + milliseconds { hsc_device_timeout };
        Message response(MSG_SIZE);

        while (true) {
            const auto remaining = std::chrono::duration_cast<milliseconds>(end - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return std::nullopt;
            }

            auto read_result = readHIDTimeout(device_handle, response, static_cast<int>(remaining.count()));
            if (!read_result) {
                return std::nullopt;
            }

            bool responded = *read_result == MSG_SIZE && response.at(OFFSET_REPORT_ID) == REPORT_ID
                && response.at(OFFSET_MSG_TYPE) == MSG_TYPE_READ && response.at(OFFSET_CMD) == cmd;
            if (!responded) {
                continue; // Ignore unsolicited packets
            }

            return response;
        }
    }

    bool checkLink(hid_device* device_handle)
    {

        bool send_verified = sendMessage(device_handle, CMD_LINK);

        if (!send_verified) {
            return false;
        }

        auto response = receiveMessage(device_handle, CMD_LINK);

        if (!response) {
            return false;
        }

        const auto& link_response = *response;

        return bool(link_response.at(4));
    }
};
} // namespace headsetcontrol

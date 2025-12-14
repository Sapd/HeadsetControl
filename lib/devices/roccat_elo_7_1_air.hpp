#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <thread>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief ROCCAT Elo 7.1 Air Wireless Gaming Headset
 *
 * Features:
 * - Lights control (RGB with static color)
 * - Inactive time/auto-shutoff
 *
 * Special Requirements:
 * - Requires 75ms delay between HID commands (device hates quick packets)
 * - Uses 64-byte packets
 */
class RoccatElo71Air : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x3a37 // Elo 7.1 Air
    };

    static constexpr int PACKET_SIZE = 64;
    static constexpr int DELAY_MS    = 75;

    constexpr uint16_t getVendorId() const override
    {
        return 0x1e7d; // ROCCAT
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "ROCCAT Elo 7.1 Air"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_LIGHTS) | B(CAP_INACTIVE_TIME);
    }

private:
    /**
     * @brief Send and optionally receive data with required delay
     *
     * The Roccat Elo 7.1 Air dislikes rapid packets, so we wait 75ms after each operation
     */
    Result<void> sendReceive(hid_device* device_handle, std::span<const uint8_t> data, std::span<uint8_t> out_buffer = {}) const
    {
        if (auto result = writeHID(device_handle, data); !result) {
            return result.error();
        }

        if (!out_buffer.empty()) {
            auto read_result = readHIDTimeout(device_handle, out_buffer, hsc_device_timeout);
            if (!read_result) {
                return read_result.error();
            }
        }

        // Required delay: device hates quick packets
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_MS));

        return {};
    }

public:
    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // All commands are LED-related, exact meaning is unknown
        std::array<uint8_t, PACKET_SIZE> cmd92 {};
        cmd92[0] = 0xff;
        cmd92[1] = 0x02;

        auto result = sendReceive(device_handle, cmd92);
        if (!result) {
            return result.error();
        }

        std::array<uint8_t, PACKET_SIZE> cmd93 {};
        cmd93[0] = 0xff;
        cmd93[1] = 0x03;
        cmd93[2] = 0x00;
        cmd93[3] = 0x01;
        cmd93[4] = 0x00;
        cmd93[5] = 0x01;

        result = sendReceive(device_handle, cmd93);
        if (!result) {
            return result.error();
        }

        // Sets LED color as RGB value
        std::array<uint8_t, PACKET_SIZE> cmd94 {};
        cmd94[0] = 0xff;
        cmd94[1] = 0x04;
        cmd94[2] = 0x00;
        cmd94[3] = 0x00;
        cmd94[4] = on ? 0xff : 0; // Red
        cmd94[5] = on ? 0xff : 0; // Green
        cmd94[6] = on ? 0xff : 0; // Blue

        result = sendReceive(device_handle, cmd94);
        if (!result) {
            return result.error();
        }

        std::array<uint8_t, PACKET_SIZE> cmd95 {};
        cmd95[0] = 0xff;
        cmd95[1] = 0x01;

        result = sendReceive(device_handle, cmd95);
        if (!result) {
            return result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        if (minutes > 60) {
            return DeviceError::invalidParameter("Device only supports up to 60 minutes");
        }

        uint16_t seconds = minutes * 60;

        std::array<uint8_t, PACKET_SIZE> cmd {};
        cmd[0] = 0xa1;
        cmd[1] = 0x04;
        cmd[2] = 0x06;
        cmd[3] = 0x54;
        cmd[4] = (seconds >> 8) & 0xFF;
        cmd[5] = seconds & 0xFF;

        std::array<uint8_t, PACKET_SIZE> response {};
        auto result = sendReceive(device_handle, cmd, response);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 60
        };
    }
};

} // namespace headsetcontrol

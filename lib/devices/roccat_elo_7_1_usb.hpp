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
 * @brief ROCCAT Elo 7.1 USB Wired Gaming Headset
 *
 * Features:
 * - Lights control (RGB with static color)
 *
 * Special Requirements:
 * - Requires 40ms delay between HID commands
 * - Uses 16-byte packets (different from Air version!)
 */
class RoccatElo71USB : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x3a34 // Elo 7.1 USB
    };

    static constexpr int PACKET_SIZE = 16;
    static constexpr int DELAY_MS    = 40;

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
        return "ROCCAT Elo 7.1 USB"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_LIGHTS);
    }

private:
    /**
     * @brief Send and optionally receive data with required delay
     *
     * The Roccat Elo 7.1 USB needs delays between packets
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

        // Required delay
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_MS));

        return {};
    }

public:
    // Rich Results V2 API
    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
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

        result = sendReceive(device_handle, cmd93);
        if (!result) {
            return result.error();
        }

        if (on) {
            std::array<uint8_t, PACKET_SIZE> cmd94 {};
            cmd94[0] = 0xff;
            cmd94[1] = 0x04;
            cmd94[2] = 0x00;
            cmd94[3] = 0x00;
            cmd94[4] = 0xf4;

            result = sendReceive(device_handle, cmd94);
            if (!result) {
                return result.error();
            }
        } else {
            std::array<uint8_t, PACKET_SIZE> cmd97 {};
            cmd97[0] = 0xff;
            cmd97[1] = 0x04;

            result = sendReceive(device_handle, cmd97);
            if (!result) {
                return result.error();
            }
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
};
} // namespace headsetcontrol

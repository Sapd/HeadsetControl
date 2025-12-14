#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <cmath>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis 9 Gaming Headset
 *
 * Features:
 * - Battery status (0x64-0x9A range)
 * - Sidetone control (exponential scaling, unique!)
 * - Inactive time/auto-shutoff
 * - Chatmix dial
 *
 * Special Note: This device uses exponential sidetone scaling,
 * unlike other SteelSeries devices!
 */
class SteelSeriesArctis9 : public protocols::SteelSeriesLegacyDevice<SteelSeriesArctis9> {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x12c2 // Arctis 9
    };

    static constexpr uint8_t BATTERY_MIN = 0x64;
    static constexpr uint8_t BATTERY_MAX = 0x9A;

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis 9"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_CHATMIX_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        return { .interface = 0 };
    }

    // Override save state for Arctis 9
    Result<void> saveState(hid_device* device_handle) const override
    {
        std::array<uint8_t, PACKET_SIZE_31> data { 0x90, 0x0 };
        return writeHID(device_handle, data);
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, 2> request { 0x0, 0x20 };

        return requestBatteryDirect(
            device_handle,
            request,
            12, // response size
            3, // battery byte
            4, // status byte
            0xFF, // offline value (unused)
            0x01, // charging value
            BATTERY_MIN,
            BATTERY_MAX);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Arctis 9 uses exponential scaling: transform log scale into linear
        uint8_t num = level;
        if (num > 0) {
            // Map log2(num)*100 from 0-700 to 0-128, then exponentially to 0xc0-0xfd
            num = map(static_cast<int>(std::log2(num) * 100), 0, 700, 0, 128);
            num = map(num, 0, 128, 0xc0, 0xfd);
        } else {
            num = 0xc0; // Off value
        }

        std::array<uint8_t, 5> cmd { 0x06, 0x0, num };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0xc0,
            .device_max    = 0xfd
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Value needs to be in seconds for Arctis 9
        uint32_t time = minutes * 60;
        std::array<uint8_t, 4> cmd { 0x04, 0x0, static_cast<uint8_t>(time >> 8), static_cast<uint8_t>(time) };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        // Request device status (same as battery)
        std::array<uint8_t, 2> request { 0x0, 0x20 };
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        // Read response
        std::vector<uint8_t> response(12);
        auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
        if (!read_result) {
            return read_result.error();
        }

        // Chatmix values at bytes 9 and 10, range 0-19
        int game = map(response[9], 0, 19, 0, 64);
        int chat = map(response[10], 0, 19, 0, -64);
        int level = 64 - (chat + game);

        // Calculate percentages
        int game_pct = map(response[9], 0, 19, 0, 100);
        int chat_pct = map(response[10], 0, 19, 0, 100);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct
        };
    }
};
} // namespace headsetcontrol

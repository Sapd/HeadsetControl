#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis 7/Pro Gaming Headset
 *
 * Features:
 * - Battery status (direct percentage)
 * - Sidetone control (0-18 levels)
 * - Inactive time/auto-shutoff
 * - Chatmix dial
 * - LED control
 *
 * Reduces code from 204 lines (C) to ~110 lines (C++)
 */
class SteelSeriesArctis7 : public protocols::SteelSeriesLegacyDevice<SteelSeriesArctis7> {
public:
    static constexpr std::array<uint16_t, 4> SUPPORTED_PRODUCT_IDS {
        0x1260, // Arctis 7
        0x12ad, // Arctis 7 2019
        0x1252, // Arctis Pro 2019
        0x1280 // Arctis Pro GameDAC
    };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis (7/Pro)"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME)
            | B(CAP_CHATMIX_STATUS) | B(CAP_LIGHTS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_SIDETONE:
        case CAP_BATTERY_STATUS:
        case CAP_INACTIVE_TIME:
        case CAP_CHATMIX_STATUS:
        case CAP_LIGHTS:
            return { .interface = 0x05 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, 2> request { 0x06, 0x18 };

        return requestBatteryDirect(
            device_handle,
            request,
            8, // response size
            2, // battery byte
            -1, // no status byte
            0, // offline value (unused)
            0, // charging value (no charging indicator)
            0, // min
            100 // max (already percentage)
        );
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Arctis 7 range: 0x00 to 0x12 (0-18)
        uint8_t mapped = map(level, 0, 128, 0x00, 0x12);

        std::array<uint8_t, 5> cmd_on { 0x06, 0x35, 0x01, 0x00, mapped };
        std::array<uint8_t, 2> cmd_off { 0x06, 0x35 };

        auto result = (level > 0)
            ? sendCommandWithSave(device_handle, cmd_on)
            : sendCommandWithSave(device_handle, cmd_off);

        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0x00,
            .device_max    = 0x12
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        if (minutes > 90) {
            minutes = 90;
        }

        std::array<uint8_t, 3> cmd { 0x06, 0x51, minutes };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        std::array<uint8_t, 8> cmd { 0x06, 0x55, 0x01, static_cast<uint8_t>(on ? 0x02 : 0x00) };

        auto result = sendCommandWithSave(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        // Request chatmix level
        std::array<uint8_t, 2> request { 0x06, 0x24 };
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        // Read response
        std::vector<uint8_t> response(8);
        auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
        if (!read_result) {
            return read_result.error();
        }

        int game = response[2];
        int chat = response[3];

        // The two values are between 255 and 191
        // Convert to 0-128 range with 64 being center
        int level;
        if (game == 0 && chat == 0) {
            level = 64;
        } else if (game == 0) {
            level = 64 + 255 - chat;
        } else {
            level = 64 + (-1) * (255 - game);
        }

        // Calculate percentages (game/chat are 191-255, neutral at 255)
        int game_pct = (game == 0) ? 100 : map(game, 191, 255, 100, 0);
        int chat_pct = (chat == 0) ? 100 : map(chat, 191, 255, 100, 0);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct
        };
    }
};
} // namespace headsetcontrol

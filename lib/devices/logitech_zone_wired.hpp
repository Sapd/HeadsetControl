#pragma once

#include "protocols/hidpp_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech Zone Wired/Zone 750
 *
 * USB-powered business headsets with voice features.
 *
 * Features:
 * - Sidetone (0-10 range)
 * - Voice prompts toggle
 * - Rotate-to-mute toggle
 *
 * Original implementation: logitech_zone_wired.c (66 lines)
 * New implementation: ~50 lines (24% reduction)
 */
class LogitechZoneWired : public protocols::HIDPPDevice<LogitechZoneWired> {
private:
    static constexpr size_t MSG_SIZE = 20;

public:
    std::vector<uint16_t> getProductIds() const override
    {
        return {
            0x0aad, // Zone Wired
            0x0ade // Zone 750
        };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech Zone Wired/Zone 750"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_VOICE_PROMPTS) | B(CAP_ROTATE_TO_MUTE);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Zone Wired uses 0-10 range (0% to 100% in 10% steps)
        uint8_t raw_volume = map(level, 0, 128, 0, 10);

        std::array<uint8_t, MSG_SIZE> data {};
        data[0] = 0x22;
        data[1] = 0xF1;
        data[2] = 0x04;
        data[3] = 0x00;
        data[4] = 0x04;
        data[5] = 0x3d;
        data[6] = raw_volume;

        if (auto result = sendFeatureReport(device_handle, data, MSG_SIZE); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 10
        };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, MSG_SIZE> data {};
        data[0] = 0x22;
        data[1] = 0xF1;
        data[2] = 0x04;
        data[3] = 0x00;
        data[4] = 0x05;
        data[5] = 0x3d;
        data[6] = enabled ? 1 : 0;

        if (auto result = sendFeatureReport(device_handle, data, MSG_SIZE); !result) {
            return result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    Result<RotateToMuteResult> setRotateToMute(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, MSG_SIZE> data {};
        data[0] = 0x22;
        data[1] = 0xF1;
        data[2] = 0x04;
        data[3] = 0x00;
        data[4] = 0x05;
        data[5] = 0x6d;
        data[6] = enabled ? 1 : 0;

        if (auto result = sendFeatureReport(device_handle, data, MSG_SIZE); !result) {
            return result.error();
        }

        return RotateToMuteResult { .enabled = enabled };
    }
};
} // namespace headsetcontrol

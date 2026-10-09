#pragma once

#include "audeze_maxwell_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Audeze Maxwell 2 Gaming Headset
 *
 * Features:
 * - Sidetone (level read from the headset, 32 levels by default)
 * - Inactive time with discrete levels
 * - Equalizer presets (10 total: 6 default + 4 custom)
 * - Battery status
 * - Chatmix
 * - Voice prompts
 * - Mic noise filter
 *
 * The protocol is implemented in audeze_maxwell_device.hpp.
 */
class AudezeMaxwell2 : public AudezeMaxwellDevice {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x4b29, // Maxwell 2 (PlayStation/PC version)
        0x4b28 // Maxwell 2 (Xbox version)
    };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Audeze Maxwell 2"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_VOICE_PROMPTS)
            | B(CAP_NOISE_FILTER) | B(CAP_SIDETONE_STATUS);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff13, .usageid = 0x1, .interface_id = 5 };
    }
};
} // namespace headsetcontrol

#pragma once

#include "steelseries_arctis_nova_7.hpp"

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova 7P Gaming Headset
 *
 * The Nova 7P is a variant of the Nova 7 with different hardware behavior:
 * - The dial controls sidetone instead of chatmix, but sidetone setting doesn't work
 * - Chatmix always returns a fixed value (64)
 *
 * This class inherits from Nova 7 and disables the non-functional capabilities.
 * See: https://github.com/Sapd/HeadsetControl/issues/450
 */
class SteelSeriesArctisNova7P : public SteelSeriesArctisNova7 {
public:
    std::vector<uint16_t> getProductIds() const override
    {
        return { 0x220a }; // Arctis Nova 7P only
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova 7P"sv;
    }

    constexpr int getCapabilities() const override
    {
        // Nova 7P does not support sidetone or chatmix
        // (sidetone setting has no effect, chatmix always returns 64)
        return B(CAP_BATTERY_STATUS)
            | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME)
            | B(CAP_VOLUME_LIMITER) | B(CAP_BT_WHEN_POWERED_ON) | B(CAP_BT_CALL_VOLUME);
    }
};
} // namespace headsetcontrol

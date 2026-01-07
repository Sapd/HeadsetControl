#pragma once

#include "device.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace headsetcontrol {

/**
 * @brief Describes a capability's metadata
 *
 * Single source of truth for capability information including:
 * - CLI flags and help text
 * - Parameter value ranges
 * - Feature type (action vs info)
 */
struct CapabilityDescriptor {
    capabilities cap;
    capabilitytype type;
    std::string_view name; // CLI long flag name (e.g., "sidetone")
    std::string_view short_flag; // CLI short flag (e.g., "-s")
    std::string_view description; // Help text description
    std::optional<int> min_value; // Minimum parameter value (for actions)
    std::optional<int> max_value; // Maximum parameter value (for actions)
    std::string_view value_hint; // Parameter hint for help (e.g., "<0-128>")

    [[nodiscard]] constexpr bool hasValueRange() const noexcept
    {
        return min_value.has_value() && max_value.has_value();
    }

    [[nodiscard]] constexpr bool isInfoFeature() const noexcept
    {
        return type == CAPABILITYTYPE_INFO;
    }

    [[nodiscard]] constexpr bool isActionFeature() const noexcept
    {
        return type == CAPABILITYTYPE_ACTION;
    }
};

// All capability metadata - add new capabilities here
inline constexpr std::array<CapabilityDescriptor, NUM_CAPABILITIES> CAPABILITY_DESCRIPTORS = { {
    // CAP_SIDETONE
    {
        .cap         = CAP_SIDETONE,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "sidetone",
        .short_flag  = "-s",
        .description = "Set sidetone (microphone feedback) level",
        .min_value   = 0,
        .max_value   = 128,
        .value_hint  = "<0-128>" },

    // CAP_BATTERY_STATUS
    {
        .cap         = CAP_BATTERY_STATUS,
        .type        = CAPABILITYTYPE_INFO,
        .name        = "battery",
        .short_flag  = "-b",
        .description = "Show battery charge level and status",
        .min_value   = std::nullopt,
        .max_value   = std::nullopt,
        .value_hint  = "" },

    // CAP_NOTIFICATION_SOUND
    {
        .cap         = CAP_NOTIFICATION_SOUND,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "notificate",
        .short_flag  = "-n",
        .description = "Play notification sound",
        .min_value   = 0,
        .max_value   = 1,
        .value_hint  = "<0|1>" },

    // CAP_LIGHTS
    {
        .cap         = CAP_LIGHTS,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "lights",
        .short_flag  = "-l",
        .description = "Turn lights on or off",
        .min_value   = 0,
        .max_value   = 1,
        .value_hint  = "<0|1>" },

    // CAP_INACTIVE_TIME
    {
        .cap         = CAP_INACTIVE_TIME,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "inactive-time",
        .short_flag  = "-i",
        .description = "Set auto-shutoff time in minutes (0 = never)",
        .min_value   = 0,
        .max_value   = 90,
        .value_hint  = "<0-90>" },

    // CAP_CHATMIX_STATUS
    {
        .cap         = CAP_CHATMIX_STATUS,
        .type        = CAPABILITYTYPE_INFO,
        .name        = "chatmix",
        .short_flag  = "-m",
        .description = "Show chat-mix level (game/chat balance)",
        .min_value   = std::nullopt,
        .max_value   = std::nullopt,
        .value_hint  = "" },

    // CAP_VOICE_PROMPTS
    {
        .cap         = CAP_VOICE_PROMPTS,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "voice-prompt",
        .short_flag  = "-v",
        .description = "Enable or disable voice prompts",
        .min_value   = 0,
        .max_value   = 1,
        .value_hint  = "<0|1>" },

    // CAP_ROTATE_TO_MUTE
    {
        .cap         = CAP_ROTATE_TO_MUTE,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "rotate-to-mute",
        .short_flag  = "-r",
        .description = "Enable or disable rotate-to-mute",
        .min_value   = 0,
        .max_value   = 1,
        .value_hint  = "<0|1>" },

    // CAP_EQUALIZER_PRESET
    {
        .cap         = CAP_EQUALIZER_PRESET,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "equalizer-preset",
        .short_flag  = "-p",
        .description = "Set equalizer preset",
        .min_value   = 0,
        .max_value   = 255, // Device-specific max
        .value_hint  = "<preset>" },

    // CAP_EQUALIZER
    {
        .cap         = CAP_EQUALIZER,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "equalizer",
        .short_flag  = "-e",
        .description = "Set custom equalizer curve",
        .min_value   = std::nullopt,
        .max_value   = std::nullopt,
        .value_hint  = "<curve>" },

    // CAP_PARAMETRIC_EQUALIZER
    {
        .cap         = CAP_PARAMETRIC_EQUALIZER,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "parametric-equalizer",
        .short_flag  = "", // No short flag
        .description = "Set parametric equalizer bands",
        .min_value   = std::nullopt,
        .max_value   = std::nullopt,
        .value_hint  = "<bands>" },

    // CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
    {
        .cap         = CAP_MICROPHONE_MUTE_LED_BRIGHTNESS,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "microphone-mute-led-brightness",
        .short_flag  = "",
        .description = "Set microphone mute LED brightness",
        .min_value   = 0,
        .max_value   = 3,
        .value_hint  = "<0-3>" },

    // CAP_MICROPHONE_VOLUME
    {
        .cap         = CAP_MICROPHONE_VOLUME,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "microphone-volume",
        .short_flag  = "",
        .description = "Set microphone volume level",
        .min_value   = 0,
        .max_value   = 128,
        .value_hint  = "<0-128>" },

    // CAP_VOLUME_LIMITER
    {
        .cap         = CAP_VOLUME_LIMITER,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "volume-limiter",
        .short_flag  = "",
        .description = "Enable or disable volume limiter",
        .min_value   = 0,
        .max_value   = 1,
        .value_hint  = "<0|1>" },

    // CAP_BT_WHEN_POWERED_ON
    {
        .cap         = CAP_BT_WHEN_POWERED_ON,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "bt-when-powered-on",
        .short_flag  = "",
        .description = "Enable Bluetooth when headset powers on",
        .min_value   = 0,
        .max_value   = 1,
        .value_hint  = "<0|1>" },

    // CAP_BT_CALL_VOLUME
    {
        .cap         = CAP_BT_CALL_VOLUME,
        .type        = CAPABILITYTYPE_ACTION,
        .name        = "bt-call-volume",
        .short_flag  = "",
        .description = "Set Bluetooth call volume",
        .min_value   = 0,
        .max_value   = 100,
        .value_hint  = "<0-100>" },
} };

/**
 * @brief Get descriptor for a capability
 * @param cap Capability to look up
 * @return Reference to the capability's descriptor
 */
[[nodiscard]] constexpr const CapabilityDescriptor& getCapabilityDescriptor(capabilities cap) noexcept
{
    return CAPABILITY_DESCRIPTORS[static_cast<size_t>(cap)];
}

/**
 * @brief Find descriptor by CLI name
 * @param name CLI flag name (e.g., "sidetone")
 * @return Pointer to descriptor or nullptr if not found
 */
[[nodiscard]] inline const CapabilityDescriptor* findDescriptorByName(std::string_view name) noexcept
{
    for (const auto& desc : CAPABILITY_DESCRIPTORS) {
        if (desc.name == name) {
            return &desc;
        }
    }
    return nullptr;
}

/**
 * @brief Find descriptor by short flag
 * @param flag Short flag (e.g., "-s")
 * @return Pointer to descriptor or nullptr if not found
 */
[[nodiscard]] inline const CapabilityDescriptor* findDescriptorByShortFlag(std::string_view flag) noexcept
{
    for (const auto& desc : CAPABILITY_DESCRIPTORS) {
        if (!desc.short_flag.empty() && desc.short_flag == flag) {
            return &desc;
        }
    }
    return nullptr;
}

} // namespace headsetcontrol

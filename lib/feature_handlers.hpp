#pragma once

#include "capability_descriptors.hpp"
#include "device.hpp"
#include "devices/hid_device.hpp"
#include "result_types.hpp"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace headsetcontrol {

/**
 * @brief Unified feature output that can represent any feature result
 */
struct FeatureOutput {
    int value = 0; // Primary result value
    std::string message; // Human-readable message
    std::optional<BatteryResult> battery; // Extended battery info
    std::optional<ChatmixResult> chatmix; // Extended chatmix info
    std::optional<SidetoneResult> sidetone; // Extended sidetone info
    std::optional<HeadsetVolumeResult> headset_volume; // Extended headset volume info
    std::optional<LightColorSettings> light_color; // Color that was set

    static FeatureOutput success(int val, std::string msg = "")
    {
        return { .value = val, .message = std::move(msg) };
    }

    static FeatureOutput fromBattery(const BatteryResult& b)
    {
        return {
            .value   = b.level_percent,
            .message = "",
            .battery = b
        };
    }

    static FeatureOutput fromChatmix(const ChatmixResult& c)
    {
        return {
            .value   = c.level,
            .message = std::format("Chat-Mix: {}", c.level),
            .chatmix = c
        };
    }

    static FeatureOutput fromSidetone(const SidetoneResult& s)
    {
        return {
            .value    = s.current_level,
            .message  = "",
            .sidetone = s
        };
    }

    static FeatureOutput fromHeadsetVolume(const HeadsetVolumeResult& v)
    {
        return {
            .value          = v.volume,
            .message        = "",
            .headset_volume = v
        };
    }

    static FeatureOutput fromLightColor(const LightColorResult& c)
    {
        return {
            .value       = (c.color.r << 16) | (c.color.g << 8) | c.color.b,
            .message     = "",
            .light_color = c.color
        };
    }
};

/**
 * @brief Handler function signature for feature execution
 */
using FeatureHandler = std::function<Result<FeatureOutput>(
    HIDDevice* device,
    hid_device* handle,
    const FeatureParam& param)>;

// Dispatch table mapping (capability, direction) to handlers. Setting and reading
// back are two directions of one capability, so each capability has an ACTION and
// an INFO slot. Info-only capabilities (battery, chatmix, ...) use the INFO slot.
class FeatureHandlerRegistry {
public:
    static FeatureHandlerRegistry& instance()
    {
        static FeatureHandlerRegistry registry;
        return registry;
    }

    /**
     * @brief Register a handler for one direction of a capability
     */
    void registerHandler(capabilities cap, capabilitytype type, FeatureHandler handler)
    {
        handlers_[static_cast<size_t>(cap)][static_cast<size_t>(type)] = std::move(handler);
    }

    /**
     * @brief Check if a handler is registered for one direction of a capability
     */
    [[nodiscard]] bool hasHandler(capabilities cap, capabilitytype type) const
    {
        return handlers_[static_cast<size_t>(cap)][static_cast<size_t>(type)] != nullptr;
    }

    /**
     * @brief Execute a feature
     * @param cap Capability to execute
     * @param type Direction: CAPABILITYTYPE_ACTION sets, CAPABILITYTYPE_INFO reads
     * @param device Device implementation
     * @param handle HID device handle (may be nullptr for test devices)
     * @param param Feature parameter
     * @return Result with feature output or error
     */
    [[nodiscard]] Result<FeatureOutput> execute(
        capabilities cap,
        capabilitytype type,
        HIDDevice* device,
        hid_device* handle,
        const FeatureParam& param) const
    {
        const auto& handler = handlers_[static_cast<size_t>(cap)][static_cast<size_t>(type)];
        if (!handler) {
            return DeviceError::notSupported(
                std::format("No {} handler registered for {}",
                    type == CAPABILITYTYPE_INFO ? "read" : "set", capability_to_string(cap)));
        }
        return handler(device, handle, param);
    }

private:
    FeatureHandlerRegistry()
    {
        registerAllHandlers();
    }

    void registerAllHandlers();

    std::array<std::array<FeatureHandler, 2>, NUM_CAPABILITIES> handlers_ {};
};

/**
 * @brief Validate a feature parameter against its descriptor
 * @param cap Capability to validate for
 * @param param Parameter to validate
 * @return Error message if invalid, nullopt if valid
 */
[[nodiscard]] inline std::optional<std::string> validateFeatureParam(
    capabilities cap,
    const FeatureParam& param)
{
    const auto& desc = getCapabilityDescriptor(cap);

    // Info features should have no parameter (monostate)
    if (desc.isInfoFeature()) {
        if (!std::holds_alternative<std::monostate>(param) && !std::holds_alternative<int>(param)) {
            return std::format("{} doesn't take a parameter", desc.name);
        }
        return std::nullopt; // Valid
    }

    // Action features need a parameter
    if (std::holds_alternative<std::monostate>(param)) {
        // Allow monostate for features that might have default behavior
        return std::nullopt;
    }

    // Validate integer range if applicable
    if (auto* val = std::get_if<int>(&param)) {
        if (desc.min_value && *val < *desc.min_value) {
            return std::format("{} must be >= {} (got {})",
                desc.name, *desc.min_value, *val);
        }
        if (desc.max_value && *val > *desc.max_value) {
            return std::format("{} must be <= {} (got {})",
                desc.name, *desc.max_value, *val);
        }
    }

    return std::nullopt; // Valid
}

// ============================================================================
// Handler Registration Helpers
// ============================================================================

namespace detail {

    // Helper to get int from FeatureParam
    inline int getInt(const FeatureParam& p)
    {
        return std::get<int>(p);
    }

    inline uint8_t getUint8(const FeatureParam& p)
    {
        return static_cast<uint8_t>(std::get<int>(p));
    }

    inline bool getBool(const FeatureParam& p)
    {
        return std::get<int>(p) != 0;
    }

    inline const EqualizerSettings& getEqualizer(const FeatureParam& p)
    {
        return std::get<EqualizerSettings>(p);
    }

    inline const ParametricEqualizerSettings& getParametricEq(const FeatureParam& p)
    {
        return std::get<ParametricEqualizerSettings>(p);
    }

    inline const AncButtonModes& getAncButtonModes(const FeatureParam& p)
    {
        return std::get<AncButtonModes>(p);
    }

    inline const LightColorSettings& getLightColor(const FeatureParam& p)
    {
        return std::get<LightColorSettings>(p);
    }

} // namespace detail

// ============================================================================
// Handler Registration
// ============================================================================

inline void FeatureHandlerRegistry::registerAllHandlers()
{
    using namespace detail;

    // CAP_SIDETONE
    registerHandler(CAP_SIDETONE, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setSidetone(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromSidetone(r.value());
    });

    registerHandler(CAP_SIDETONE, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getSidetone(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromSidetone(r.value());
    });

    // CAP_BATTERY_STATUS
    registerHandler(CAP_BATTERY_STATUS, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getBattery(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromBattery(r.value());
    });

    // CAP_NOTIFICATION_SOUND
    registerHandler(CAP_NOTIFICATION_SOUND, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->notificationSound(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->sound_id);
    });

    // CAP_LIGHTS
    registerHandler(CAP_LIGHTS, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setLights(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_INACTIVE_TIME
    registerHandler(CAP_INACTIVE_TIME, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setInactiveTime(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->minutes);
    });

    // CAP_CHATMIX_STATUS
    registerHandler(CAP_CHATMIX_STATUS, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getChatmix(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromChatmix(r.value());
    });

    // CAP_VOICE_PROMPTS
    registerHandler(CAP_VOICE_PROMPTS, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setVoicePrompts(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_ROTATE_TO_MUTE
    registerHandler(CAP_ROTATE_TO_MUTE, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setRotateToMute(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_EQUALIZER_PRESET
    registerHandler(CAP_EQUALIZER_PRESET, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setEqualizerPreset(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->preset);
    });

    // CAP_EQUALIZER
    registerHandler(CAP_EQUALIZER, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setEqualizer(h, getEqualizer(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(0);
    });

    // CAP_PARAMETRIC_EQUALIZER
    registerHandler(CAP_PARAMETRIC_EQUALIZER, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setParametricEqualizer(h, getParametricEq(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(0);
    });

    // CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
    registerHandler(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setMicMuteLedBrightness(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->brightness);
    });

    // CAP_MICROPHONE_VOLUME
    registerHandler(CAP_MICROPHONE_VOLUME, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setMicVolume(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->volume);
    });

    // CAP_VOLUME_LIMITER
    registerHandler(CAP_VOLUME_LIMITER, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setVolumeLimiter(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_BT_WHEN_POWERED_ON
    registerHandler(CAP_BT_WHEN_POWERED_ON, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setBluetoothWhenPoweredOn(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_BT_CALL_VOLUME
    registerHandler(CAP_BT_CALL_VOLUME, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setBluetoothCallVolume(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->volume);
    });

    // CAP_NOISE_FILTER
    registerHandler(CAP_NOISE_FILTER, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setNoiseFilter(h, detail::getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->level);
    });

    // CAP_SIDETONE_STATUS
    registerHandler(CAP_SIDETONE_STATUS, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getSidetone(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromSidetone(r.value());
    });

    // CAP_ANC
    registerHandler(CAP_ANC, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setAnc(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->mode);
    });

    // CAP_ANC_STARTUP_MODE
    registerHandler(CAP_ANC_STARTUP_MODE, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setAncStartupMode(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->mode);
    });

    // CAP_ANC_BUTTON_MODES
    registerHandler(CAP_ANC_BUTTON_MODES, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setAncButtonModes(h, getAncButtonModes(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(0);
    });

    // CAP_MICROPHONE_ATTACHMENT_STATUS
    registerHandler(CAP_MICROPHONE_ATTACHMENT_STATUS, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getMicAttachmentStatus(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->attached ? 1 : 0, r->attached ? "attached" : "detached");
    });

    // CAP_MICROPHONE_MUTE_STATUS
    registerHandler(CAP_MICROPHONE_MUTE_STATUS, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getMicMuteStatus(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->muted ? 1 : 0, r->muted ? "muted" : "unmuted");
    });

    // CAP_HEADSET_VOLUME
    registerHandler(CAP_HEADSET_VOLUME, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setHeadsetVolume(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromHeadsetVolume(r.value());
    });

    registerHandler(CAP_HEADSET_VOLUME, CAPABILITYTYPE_INFO, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getHeadsetVolume(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromHeadsetVolume(r.value());
    });

    // CAP_LIGHT_COLOR
    registerHandler(CAP_LIGHT_COLOR, CAPABILITYTYPE_ACTION, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setLightColor(h, getLightColor(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromLightColor(r.value());
    });
}

} // namespace headsetcontrol

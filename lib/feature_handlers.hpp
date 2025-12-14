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
};

/**
 * @brief Handler function signature for feature execution
 */
using FeatureHandler = std::function<Result<FeatureOutput>(
    HIDDevice* device,
    hid_device* handle,
    const FeatureParam& param)>;

// Dispatch table mapping capabilities to handlers
class FeatureHandlerRegistry {
public:
    static FeatureHandlerRegistry& instance()
    {
        static FeatureHandlerRegistry registry;
        return registry;
    }

    /**
     * @brief Register a handler for a capability
     */
    void registerHandler(capabilities cap, FeatureHandler handler)
    {
        handlers_[static_cast<size_t>(cap)] = std::move(handler);
    }

    /**
     * @brief Check if a handler is registered for a capability
     */
    [[nodiscard]] bool hasHandler(capabilities cap) const
    {
        return handlers_[static_cast<size_t>(cap)] != nullptr;
    }

    /**
     * @brief Execute a feature
     * @param cap Capability to execute
     * @param device Device implementation
     * @param handle HID device handle (may be nullptr for test devices)
     * @param param Feature parameter
     * @return Result with feature output or error
     */
    [[nodiscard]] Result<FeatureOutput> execute(
        capabilities cap,
        HIDDevice* device,
        hid_device* handle,
        const FeatureParam& param) const
    {
        const auto& handler = handlers_[static_cast<size_t>(cap)];
        if (!handler) {
            return DeviceError::notSupported(
                std::format("No handler registered for {}", capabilities_str[cap]));
        }
        return handler(device, handle, param);
    }

private:
    FeatureHandlerRegistry()
    {
        registerAllHandlers();
    }

    void registerAllHandlers();

    std::array<FeatureHandler, NUM_CAPABILITIES> handlers_ {};
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

} // namespace detail

// ============================================================================
// Handler Registration
// ============================================================================

inline void FeatureHandlerRegistry::registerAllHandlers()
{
    using namespace detail;

    // CAP_SIDETONE
    registerHandler(CAP_SIDETONE, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setSidetone(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromSidetone(r.value());
    });

    // CAP_BATTERY_STATUS
    registerHandler(CAP_BATTERY_STATUS, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getBattery(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromBattery(r.value());
    });

    // CAP_NOTIFICATION_SOUND
    registerHandler(CAP_NOTIFICATION_SOUND, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->notificationSound(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->sound_id);
    });

    // CAP_LIGHTS
    registerHandler(CAP_LIGHTS, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setLights(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_INACTIVE_TIME
    registerHandler(CAP_INACTIVE_TIME, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setInactiveTime(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->minutes);
    });

    // CAP_CHATMIX_STATUS
    registerHandler(CAP_CHATMIX_STATUS, [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getChatmix(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::fromChatmix(r.value());
    });

    // CAP_VOICE_PROMPTS
    registerHandler(CAP_VOICE_PROMPTS, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setVoicePrompts(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_ROTATE_TO_MUTE
    registerHandler(CAP_ROTATE_TO_MUTE, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setRotateToMute(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_EQUALIZER_PRESET
    registerHandler(CAP_EQUALIZER_PRESET, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setEqualizerPreset(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->preset);
    });

    // CAP_EQUALIZER
    registerHandler(CAP_EQUALIZER, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setEqualizer(h, getEqualizer(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(0);
    });

    // CAP_PARAMETRIC_EQUALIZER
    registerHandler(CAP_PARAMETRIC_EQUALIZER, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setParametricEqualizer(h, getParametricEq(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(0);
    });

    // CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
    registerHandler(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setMicMuteLedBrightness(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->brightness);
    });

    // CAP_MICROPHONE_VOLUME
    registerHandler(CAP_MICROPHONE_VOLUME, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setMicVolume(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->volume);
    });

    // CAP_VOLUME_LIMITER
    registerHandler(CAP_VOLUME_LIMITER, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setVolumeLimiter(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_BT_WHEN_POWERED_ON
    registerHandler(CAP_BT_WHEN_POWERED_ON, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setBluetoothWhenPoweredOn(h, getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });

    // CAP_BT_CALL_VOLUME
    registerHandler(CAP_BT_CALL_VOLUME, [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setBluetoothCallVolume(h, getUint8(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->volume);
    });
}

} // namespace headsetcontrol

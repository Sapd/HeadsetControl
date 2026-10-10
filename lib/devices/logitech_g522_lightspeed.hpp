#pragma once

#include "../utility.hpp"
#include "device.hpp"
#include "protocols/logitech_centurion_protocol.hpp"
#include "result_types.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G522 LIGHTSPEED (PID 0x0b18)
 *
 * The G522 speaks the Logitech Centurion protocol on usage page 0xffa0, like the
 * G PRO X 2 LIGHTSPEED, but its frames start with 0x50 0x23 instead of 0x51.
 * Feature indexes are discovered at runtime through the dongle's bridge.
 */
class LogitechG522Lightspeed : public protocols::LogitechCenturionProtocol {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0b18 };
    static constexpr std::array<uint8_t, 2> FRAME_PREFIX { 0x50, 0x23 };
    static constexpr uint8_t SIDETONE_DEVICE_MAX = 9;
    static constexpr uint8_t SIDETONE_MIC_ID     = 0x01;

    // Lighting effect parameters (feature 0x0621, function 3): zone 00 00, effect, effect data.
    // "On" is G HUB's default look: two-zone effect 04, zone 1 Logitech blue 00b8fc,
    // zone 2 magenta ff00ab, brightness 100. "Off" is the fixed effect 00 with color 000000.
    static constexpr std::array<uint8_t, 10> LIGHTS_ON_PARAMS { 0x00, 0x00, 0x04, 0x00, 0xb8, 0xfc, 0xff, 0x00, 0xab, 0x64 };
    static constexpr std::array<uint8_t, 10> LIGHTS_OFF_PARAMS {};

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_LOGITECH;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G522 LIGHTSPEED"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS) | B(CAP_INACTIVE_TIME) | B(CAP_VOICE_PROMPTS)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_SIDETONE_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_SIDETONE:
        case CAP_SIDETONE_STATUS:
        case CAP_LIGHTS:
        case CAP_INACTIVE_TIME:
        case CAP_VOICE_PROMPTS:
        case CAP_MICROPHONE_MUTE_LED_BRIGHTNESS:
            return { .usagepage = 0xffa0, .usageid = 0x0001, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        auto battery = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::CenturionBatterySoc),
            0x00);
        if (!battery) {
            return battery.error();
        }

        auto battery_result = parseCenturionBatteryResponse(*battery);
        if (!battery_result) {
            return battery_result.error();
        }

        battery_result->raw_data       = *battery;
        auto end_time                  = std::chrono::steady_clock::now();
        battery_result->query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        return *battery_result;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // INFO: The original G HUB app does some strange mapping:
        //   0 -   5 -> 0x00 (off)
        //   6 -  16 -> 0x01
        //  17 -  27 -> 0x02
        //  28 -  38 -> 0x03
        //  39 -  49 -> 0x04
        //  50 -  61 -> 0x05
        //  62 -  72 -> 0x06
        //  73 -  83 -> 0x07
        //  84 -  94 -> 0x08
        //  95 - 100 -> 0x09
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, SIDETONE_DEVICE_MAX);

        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAudioSidetone),
                0x10,
                std::array<uint8_t, 3> { SIDETONE_MIC_ID, 0xFF, mapped });
            !write_result) {
            return write_result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX
        };
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        auto reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAudioSidetone),
            0x00);
        if (!reply) {
            return reply.error();
        }
        return parseSidetoneResponse(*reply);
    }

    /**
     * @brief Parse the sidetone read reply: <mic id> <?> <gain?> <level 0-9>.
     *
     * The headset only stores 10 steps, so the read-back level is approximate:
     * setting 64 stores step 4, which reads back as 56.
     */
    static Result<SidetoneResult> parseSidetoneResponse(std::span<const uint8_t> params)
    {
        if (params.size() < 4 || params[3] > SIDETONE_DEVICE_MAX) {
            return DeviceError::protocolError("Unexpected G522 sidetone reply");
        }

        const uint8_t device_level = params[3];
        return SidetoneResult {
            .current_level = map<uint8_t>(device_level, 0, SIDETONE_DEVICE_MAX, 0, 128),
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX,
            .is_muted      = device_level == 0,
            .device_level  = device_level,
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // The headset has no separate on/off switch for its lighting: "off" writes a black
        // fixed color, and "on" restores G HUB's default look rather than the previous one.
        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetLighting),
                0x30,
                on ? LIGHTS_ON_PARAMS : LIGHTS_OFF_PARAMS);
            !write_result) {
            return write_result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        // 1 = spoken prompts, 0 = tones (not silence). The difference is heard on the
        // Bluetooth/LIGHTSPEED switch ("lightspeed" spoken vs a beep); mic mute always beeps.
        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetVoicePrompts),
                0x50,
                std::array<uint8_t, 2> { 0x00, static_cast<uint8_t>(enabled) });
            !write_result) {
            return write_result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    /**
     * @brief Build the auto-sleep write parameters: <sleep minutes> <lights dim> <lights off>.
     *
     * The same command also carries the two lighting inactivity timers (minutes, 0 = never),
     * and the headset rejects a write without them, so keep the values that were read back.
     */
    static Result<std::array<uint8_t, 3>> buildAutoSleepParams(uint8_t minutes, std::span<const uint8_t> current)
    {
        if (current.size() < 3) {
            return DeviceError::protocolError("Unexpected G522 auto-sleep reply");
        }
        return std::array<uint8_t, 3> { minutes, current[1], current[2] };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        auto current = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::CenturionAutoSleep),
            0x00);
        if (!current) {
            return current.error();
        }

        auto params = buildAutoSleepParams(minutes, *current);
        if (!params) {
            return params.error();
        }

        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::CenturionAutoSleep),
                0x10,
                *params);
            !write_result) {
            return write_result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        uint8_t mute_led = static_cast<uint8_t>(static_cast<bool>(brightness)); // 0 or 1

        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetMicMuteLed),
                0x20,
                std::array<uint8_t, 1> { mute_led });
            !write_result) {
            return write_result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = mute_led,
            .min_brightness = 0,
            .max_brightness = 1
        };
    }

protected:
    std::span<const uint8_t> centurionFramePrefix() const override
    {
        return FRAME_PREFIX;
    }

    protocols::CenturionOptions centurionOptions() const override
    {
        return {
            .exact_direct_length      = true,
            .detect_connection_events = true,
            .probe_offline_on_timeout = true,
            .lookup_features_by_id    = true,
        };
    }
};

} // namespace headsetcontrol

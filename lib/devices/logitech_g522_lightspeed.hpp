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
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_SIDETONE:
        case CAP_INACTIVE_TIME:
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

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // WARN: This has a side effect since there are multiple timers being set with the same command.
        // The second parameter sets the time until "lighting goes into inactive mode" e.g. dimmer lights, etc. (can be set in G HUB).
        // The third parameter sets the time until "lighting off because of inactivity".
        // For both timers, a value of 0 is labeled "never" in G HUB.
        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::CenturionAutoSleep),
                0x10,
                std::array<uint8_t, 3> { minutes, 0x00, 0x00 });
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

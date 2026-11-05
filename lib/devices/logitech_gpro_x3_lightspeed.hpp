#pragma once

#include "protocols/logitech_centurion_protocol.hpp"
#include <array>
#include <chrono>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G PRO X 3 LIGHTSPEED (receiver PID 0x0b45).
 *
 * Verified on USB device revision 1.02: Centurion frames use prefix 50 23 on
 * interface 3. Feature indexes are discovered through the receiver bridge.
 * EQ is intentionally omitted until its version-2 payload is verified.
 */
class LogitechGProX3Lightspeed : public protocols::LogitechCenturionProtocol {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0b45 };
    static constexpr std::array<uint8_t, 2> FRAME_PREFIX { 0x50, 0x23 };

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
        return "Logitech G PRO X 3 LIGHTSPEED"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_INACTIVE_TIME:
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

    // Preserve the two additional timer fields returned by the headset.
    static Result<std::array<uint8_t, 3>> buildAutoSleepParams(uint8_t minutes, std::span<const uint8_t> current)
    {
        if (current.size() < 3) {
            return DeviceError::protocolError("Unexpected PRO X 3 auto-sleep reply");
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

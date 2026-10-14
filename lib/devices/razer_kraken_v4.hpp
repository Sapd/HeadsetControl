#pragma once

#include "hid_device.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/** Battery queries captured from Synapse 4 on the Kraken V4 2.4 GHz receiver.
 * Uses 64-byte Output reports on interface 5 and interrupt Input responses.
 * These packets are specific to PID 056c; no initialization or Feature reports
 * are required. Notifications (02 0a ...) can arrive between query responses.
 */
class RazerKrakenV4 : public HIDDevice {
public:
    uint16_t getVendorId() const override { return 0x1532; }
    std::vector<uint16_t> getProductIds() const override { return { 0x056c }; }
    std::string_view getDeviceName() const override { return "Razer Kraken V4"sv; }
    constexpr uint8_t getSupportedPlatforms() const override { return PLATFORM_LINUX; }
    int getCapabilities() const override { return B(CAP_BATTERY_STATUS); }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] capabilities cap) const override
    {
        return { .usagepage = 0xff14, .usageid = 0x01, .interface_id = 5 };
    }

    Result<BatteryResult> getBattery(hid_device* handle) override
    {
        auto charging = query(handle, CHARGING_QUERY);
        if (!charging)
            return charging.error();
        if (*charging > 1)
            return DeviceError::protocolError("Invalid Kraken V4 charging state");

        auto level = query(handle, BATTERY_QUERY);
        if (!level)
            return level.error();
        if (*level > 100)
            return DeviceError::protocolError("Invalid Kraken V4 battery percentage");

        return BatteryResult {
            .level_percent = *level,
            .status        = *charging ? BATTERY_CHARGING : BATTERY_AVAILABLE
        };
    }

private:
    // Exact Output reports from the capture: battery frame 7677, charging 7673.
    // The trailing bytes are kept verbatim, rather than inferring a checksum.
    static constexpr auto BATTERY_QUERY = [] {
        std::array<uint8_t, 64> report { 0x02, 0x00, 0x60, 0, 0, 0, 0x04, 0, 0, 0x80, 0x21 };
        report[62] = 0xc7;
        return report;
    }();
    static constexpr auto CHARGING_QUERY = [] {
        std::array<uint8_t, 64> report { 0x02, 0x00, 0x60, 0, 0, 0, 0x04, 0, 0, 0x80, 0x2a };
        report[62] = 0xcc;
        return report;
    }();

    Result<uint8_t> query(hid_device* handle, const std::array<uint8_t, 64>& request) const
    {
        if (auto result = writeHID(handle, request); !result)
            return result.error();

        std::array<uint8_t, 13> header { 0x02, 0x02, 0x60, 0, 0, 0, 0x05, 0, 0x80, 0x80, request[10], 0x01, 0x01 };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(hsc_device_timeout);
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0)
                return DeviceError::timeout("Kraken V4 query timed out");

            std::array<uint8_t, 64> response {};
            auto result = readHIDTimeout(handle, response, static_cast<int>(remaining));
            if (!result)
                return result.error();

            if (*result >= header.size() && std::equal(header.begin(), header.end(), response.begin())) {
                if (*result != response.size())
                    return DeviceError::protocolError("Truncated Kraken V4 response");
                return response[13];
            }
            // Ignore asynchronous notifications and unrelated HID reports,
            // while keeping one deadline for the entire query.
        }
    }
};

} // namespace headsetcontrol

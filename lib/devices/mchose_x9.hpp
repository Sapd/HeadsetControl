#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief MCHOSE X9 Wireless Gaming Headset
 *
 * Features:
 * - Battery status
 *
 * Protocol reverse-engineered from a USB capture (Wireshark + USBPcap) of
 * the official "M HUB" Windows app, and verified against the percentage
 * that app displayed.
 *
 * Battery is queried on the vendor-defined collection with usage page
 * 0xff90, which carries a 64-byte report 0x55 in both directions:
 *
 *   request:  55 65 01 00 ...   (message type 0x65, query battery)
 *   response: 55 65 VV FF ...   (VV = value, FF = which field it belongs to)
 */
class MchoseX9 : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_MCHOSE = 0x3837;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x6045 };

    static constexpr uint16_t USAGE_PAGE_STATUS = 0xff90;
    static constexpr uint16_t USAGE_ID_STATUS   = 0x01;
    static constexpr uint8_t REPORT_ID_STATUS   = 0x55;
    static constexpr uint8_t MSG_TYPE_VALUE     = 0x65;
    static constexpr uint8_t QUERY_BATTERY      = 0x01;
    static constexpr uint8_t FIELD_BATTERY      = 0x02;
    static constexpr size_t REPORT_LENGTH       = 64;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_MCHOSE;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "MCHOSE X9 Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Untested on macOS: the dongle exposes six top-level HID collections
        // and macOS opens only the first enumerated one, which is not the one
        // carrying report 0x55 (see get_hid_path).
        return PLATFORM_LINUX | PLATFORM_WINDOWS;
    }

    // On Windows every collection of this dongle shares interface 0, so the
    // usage page is what selects the right one; on Linux the whole HID
    // interface is opened as a single node and the report id disambiguates.
    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = USAGE_PAGE_STATUS, .usageid = USAGE_ID_STATUS, .interface_id = 0 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, REPORT_LENGTH> request {};
        request[0] = REPORT_ID_STATUS;
        request[1] = MSG_TYPE_VALUE;
        request[2] = QUERY_BATTERY;

        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        using clock         = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds { hsc_device_timeout };
        std::array<uint8_t, REPORT_LENGTH> response {};

        while (true) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return DeviceError::timeout("No battery reply from the dongle");
            }

            auto read_result = readHIDTimeout(device_handle, response, static_cast<int>(remaining.count()));
            if (!read_result) {
                return read_result.error();
            }

            bool is_battery_reply = *read_result >= 4 && response[0] == REPORT_ID_STATUS
                && response[1] == MSG_TYPE_VALUE && response[3] == FIELD_BATTERY;
            if (!is_battery_reply) {
                continue; // Heartbeat or other unrelated telemetry; keep waiting
            }

            return BatteryResult {
                .level_percent = response[2],
                .status        = BATTERY_AVAILABLE,
                .raw_data      = std::vector<uint8_t> { response.begin(), response.end() }
            };
        }
    }
};

} // namespace headsetcontrol

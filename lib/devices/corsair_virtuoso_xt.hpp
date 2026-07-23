#pragma once
#include "../result_types.hpp"
#include "../utility.hpp"
#include "corsair_device.hpp"
#include <array>
#include <chrono>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Corsair Virtuoso XT (Wireless + Wired)
 *
 * Protocol reverse-engineered via hidraw probing on Linux.
 *
 * Battery request: send 0x02 0x00 on interface 3 (Usage-Page 0xff42)
 * Response format (64 bytes):
 *   [0] = 0x01 (report ID)
 *   [1] = status flags (0xf0 = normal, TBD for charging)
 *   [2] = battery percentage (0-100)
 *   [3+] = zeros (unused)
 *
 * Volume events are broadcast unsolicited:
 *   [0] = 0x0E
 *   [1] = 0x00 (down), 0x01 (up), 0x02 (fast up)
 *
 * Wireless Product ID: 0x0a64 (receiver)
 * Wired Product ID:    0x0a62
 */
class CorsairVirtuosoXT : public CorsairDevice {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x0a64, // Wireless receiver
        0x0a62  // Wired USB
    };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Corsair Virtuoso XT"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
            // Interface 3, Usage-Page 0xff42, Usage-ID 0x0001
            return { .usagepage = 0xff42, .usageid = 0x1, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        // Send battery status request: 0x02 0x00
        std::array<uint8_t, 2> request { 0x02, 0x00 };
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        // Read the battery report (ID 0x01), skipping any unsolicited volume
        // events (ID 0x0e) the device broadcasts and that may be queued ahead
        // of the reply.
        std::array<uint8_t, 64> response {};
        int attempt = 0;
        while (true) {
            auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                return read_result.error();
            }
            if (response[0] == 0x01) {
                break;
            }
            if (++attempt >= MAX_READ_ATTEMPTS) {
                return DeviceError::protocolError(
                    std::format("Unexpected report ID: 0x{:02x}", response[0]));
            }
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        // Byte 1: status flags
        //   0xf0 = normal / discharging (confirmed via observation)
        //   0x00 = headset offline / not connected to receiver
        //   Other values TBD (charging state not yet reverse-engineered)
        const uint8_t status_byte   = response[1];
        const uint8_t battery_level = response[2];

        if (status_byte == 0x00) {
            return DeviceError::deviceOffline("Headset not connected to receiver");
        }

        if (battery_level > 100) {
            return DeviceError::protocolError(
                std::format("Battery percentage out of range: {}", battery_level));
        }

        return BatteryResult {
            .level_percent  = static_cast<int>(battery_level),
            .status         = BATTERY_AVAILABLE,
            .mic_status     = MICROPHONE_UNKNOWN,
            .raw_data       = std::vector<uint8_t>(response.begin(), response.end()),
            .query_duration = duration,
        };
    }

    Result<CapabilityInfo> getCapabilityInfo(enum capabilities cap) override
    {
        return HIDDevice::getCapabilityInfo(cap);
    }

private:
    // Reports to skip (e.g. unsolicited volume events) before giving up on the
    // battery reply
    static constexpr int MAX_READ_ATTEMPTS = 8;
};

} // namespace headsetcontrol

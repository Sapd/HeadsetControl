#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova Pro Omni GameHub
 *
 * The Nova Pro Omni's GameHub is a close cousin of the Nova Pro Wireless base
 * station, but its vendor HID interface differs in two ways that matter:
 *   - Commands use report ID 0x01 (not 0x06), and 64-byte reports (not 31).
 *   - Interface 3 exposes TWO top-level collections; the command/response
 *     channel is the one on usage page 0xFFC0, report ID 1. It must be selected
 *     explicitly (see getCapabilityDetail), or writes hit the wrong collection.
 *
 * Captured on firmware rev 0x0132 (Sep 2026):
 *   - Battery: `01 B0` -> reply[6] = headset battery % (0-100),
 *     reply[15] = status (0x01 offline, 0x02 cable charging, 0x08 online),
 *     the same layout as the Nova Pro Wireless. Only 0x08 has been seen on
 *     this hub so far; reply[14] may be the link state instead (see #584).
 *     While the headset is connected the hub also sends unsolicited reports
 *     with report ID 0x07 on the same interface (seen on Linux, where hidraw
 *     has one node per interface), so the reply is the first report that
 *     starts with 01 B0. Where those reports do not arrive, this changes
 *     nothing.
 *   - Sidetone: `01 38 <enable> <level>`, enable 1=on/0=muted, level 0-10.
 *     Not read back: a Linux tester found the level in feature report 1
 *     after `01 20` (see #584), not yet confirmed on a second unit.
 *
 * Features:
 * - Battery status
 * - Sidetone (levels 0-10)
 */
class SteelSeriesArctisNovaProOmni : public protocols::SteelSeriesNovaDevice<SteelSeriesArctisNovaProOmni> {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x2290 // Arctis Nova Pro Omni GameHub
    };

    static constexpr int SIDETONE_MAX = 10; // GG slider is 1-10; 0 = off

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova Pro Omni"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        // Command/response collection: usage page 0xFFC0, report ID 1, iface 3.
        return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 3 };
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Status poll uses report ID 0x01 on this hub (not 0x00).
        std::array<uint8_t, 2> request { 0x01, 0xb0 };
        if (auto result = sendCommand(device_handle, request); !result) {
            return result.error();
        }

        // Skip the hub's unsolicited reports (report ID 0x07) until the reply
        // arrives. A positive timeout is one deadline for the whole exchange,
        // checked after the first read, so a reply already queued is always
        // read. 0 (do not wait) and a negative one (wait forever) go to every
        // read as they are, as hid_read_timeout() takes them.
        using clock          = std::chrono::steady_clock;
        const int configured = hsc_device_timeout;
        const auto deadline  = clock::now() + std::chrono::milliseconds { std::max(configured, 0) };
        std::vector<uint8_t> response(STATUS_BUF_SIZE);
        size_t received = 0;
        for (bool first_read = true;; first_read = false) {
            int wait = configured;
            if (configured > 0) {
                const auto remaining = deadline - clock::now();
                if (remaining <= clock::duration::zero() && !first_read) {
                    return DeviceError::timeout("No status reply from the hub");
                }
                // At least 1 ms: a positive timeout must not turn into a 0 (do not wait) read
                wait = std::max(1, static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(remaining).count()));
            }
            auto read_result = this->readHIDTimeout(device_handle, response, wait);
            if (!read_result) {
                return read_result.error();
            }
            if (*read_result >= 2 && response[0] == 0x01 && response[1] == 0xb0) {
                if (*read_result < 16) {
                    return DeviceError::protocolError("Response too short");
                }
                received = *read_result;
                break;
            }
        }
        response.resize(received);

        // reply[0]=0x01 (report id), reply[1]=0xb0, reply[6]=battery%,
        // reply[15]=status.
        constexpr uint8_t HEADSET_OFFLINE        = 0x01;
        constexpr uint8_t HEADSET_CABLE_CHARGING = 0x02;

        uint8_t status_byte = response[15];
        if (status_byte == HEADSET_OFFLINE) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        enum battery_status status = BATTERY_AVAILABLE;
        if (status_byte == HEADSET_CABLE_CHARGING) {
            status = BATTERY_CHARGING;
        }

        int level = response[6];
        if (level > 100) {
            return DeviceError::protocolError("Invalid battery level");
        }

        return BatteryResult {
            .level_percent = level,
            .status        = status,
            .raw_data      = response
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Map normalized level (0-128) to the device's 0-10 range.
        uint8_t mapped = static_cast<uint8_t>(map(level, 0, 128, 0, SIDETONE_MAX));
        if (mapped > SIDETONE_MAX)
            mapped = SIDETONE_MAX;
        uint8_t enable = mapped > 0 ? 0x01 : 0x00;

        std::array<uint8_t, 4> cmd { 0x01, 0x38, enable, mapped };
        if (auto result = sendCommand(device_handle, cmd); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_MAX,
            .is_muted      = mapped == 0,
            .device_level  = mapped
        };
    }
};
} // namespace headsetcontrol

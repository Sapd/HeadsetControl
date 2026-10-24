#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Sony PULSE 3D wireless headset (CFI-ZWH1)
 *
 * Communicates via a 2.4 GHz USB dongle (VID 0x054C, PID 0x0D5E). HID is on
 * interface 3, which exposes four top-level collections; the headset status is
 * on the vendor collection with usage page 0xFF01, usage 0x0020.
 *
 * The dongle does not answer queries. It sends an 8-byte status report
 * (report ID 0xB0) only when something changes: power on/off, a button press,
 * plugging in the charge cable, or a battery step (roughly once an hour).
 *   byte[3] = battery 0-100 in steps of 10, or 0x80 while charging
 *   byte[4] = flags; bit 0x08 is set while the headset is connected and
 *             cleared when it is powered off
 * The remaining bytes (a 0-100 level and button flags) are not decoded.
 *
 * getBattery() therefore drains the reports queued on the cached connection
 * since the last call and remembers the newest status per handle. A
 * long-running caller sees every change; a one-shot query waits up to
 * hsc_device_timeout for the next report.
 *
 * The collection also declares a 1-bit output report (ID 0xB1), but the dongle
 * rejects writes to it, so no way to request a status report is known.
 */
class SonyPulse3D : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_SONY = 0x054c;
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0d5e };

    static constexpr int REPORT_SIZE          = 8;
    static constexpr uint8_t STATUS_REPORT_ID = 0xb0;
    static constexpr int BYTE_LEVEL           = 3;
    static constexpr int BYTE_FLAGS           = 4;
    static constexpr uint8_t LEVEL_CHARGING   = 0x80;
    static constexpr uint8_t FLAG_CONNECTED   = 0x08;

    // Queued reports are already in the OS buffer; the short timeout only covers
    // a read completing asynchronously. The cap guards against a report flood.
    static constexpr int DRAIN_TIMEOUT_MS = 50;
    static constexpr int MAX_DRAIN_READS  = 64;

    constexpr uint16_t getVendorId() const override { return VENDOR_SONY; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Sony PULSE 3D"sv; }

    constexpr int getCapabilities() const override { return B(CAP_BATTERY_STATUS); }

    // usagepage/usageid select the status collection on Windows; elsewhere the
    // whole interface 3 is opened and other collections' reports are skipped.
    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff01, .usageid = 0x0020, .interface_id = 3 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        const std::lock_guard lock(status_mutex_);
        std::array<uint8_t, 64> report {};

        for (int i = 0; i < MAX_DRAIN_READS; ++i) {
            auto rd = readHIDTimeout(device_handle, report, DRAIN_TIMEOUT_MS);
            if (!rd) {
                if (rd.error().code == DeviceError::Code::Timeout)
                    break;
                return rd.error();
            }
            remember(device_handle, std::span(report).first(*rd));
        }

        // Nothing seen on this connection yet: wait for the next status report,
        // skipping reports from the other collections
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(hsc_device_timeout);
        while (!last_status_.contains(device_handle)) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                                       .count();
            auto rd = remaining > 0 ? readHIDTimeout(device_handle, report, static_cast<int>(remaining))
                                    : Result<size_t>(DeviceError::timeout("deadline reached"));
            if (!rd) {
                if (rd.error().code == DeviceError::Code::Timeout) {
                    return DeviceError::timeout(
                        "No status report from the PULSE 3D yet. It only reports changes; "
                        "press a button on the headset (e.g. volume) and try again");
                }
                return rd.error();
            }
            remember(device_handle, std::span(report).first(*rd));
        }

        return toBatteryResult(last_status_.at(device_handle));
    }

    void onConnectionClosed(hid_device* device_handle) const override
    {
        const std::lock_guard lock(status_mutex_);
        last_status_.erase(device_handle);
    }

private:
    struct Status {
        int level_percent = -1; // last reported level; not sent while charging
        bool charging     = false;
        bool connected    = false;
        std::vector<uint8_t> raw;
    };

    mutable std::mutex status_mutex_;
    mutable std::unordered_map<hid_device*, Status> last_status_;

    void remember(hid_device* device_handle, std::span<const uint8_t> report) const
    {
        // Other collections on the same interface (media keys) use other report IDs
        if (report.size() < REPORT_SIZE || report[0] != STATUS_REPORT_ID)
            return;

        auto& status       = last_status_[device_handle];
        const uint8_t level = report[BYTE_LEVEL];
        status.connected   = (report[BYTE_FLAGS] & FLAG_CONNECTED) != 0;
        if (level == LEVEL_CHARGING) {
            status.charging = true;
        } else if (level <= 100) {
            status.charging      = false;
            status.level_percent = level;
        }
        status.raw.assign(report.begin(), report.end());
    }

    static BatteryResult toBatteryResult(const Status& status)
    {
        if (!status.connected) {
            return BatteryResult { .level_percent = -1, .status = BATTERY_UNAVAILABLE, .raw_data = status.raw };
        }
        return BatteryResult {
            .level_percent = status.level_percent,
            .status        = status.charging ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .raw_data      = status.raw,
        };
    }
};

} // namespace headsetcontrol

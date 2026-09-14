#pragma once
#include "../result_types.hpp"
#include "../utility.hpp"
#include "corsair_device.hpp"
#include "device.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Corsair Virtuoso XT / SE (Wireless + Wired)
 *
 * These headsets speak Corsair's "Bragi" property protocol, the same one used by
 * the newer devices in corsair_void_v2w.hpp, but framed on HID report 0x02 of the
 * vendor collection (Usage-Page 0xff42) rather than on an unnumbered report.
 *
 * Request layout (64 bytes, zero padded):
 *   [0] = 0x02        report ID of the vendor OUT report
 *   [1] = target      0x09 = headset behind a receiver, 0x08 = this device
 *   [2] = command     0x01 = SET, 0x02 = GET
 *   [3] = property ID
 *   [4] = 0x00
 *   [5..] = little-endian value (SET only)
 *
 * Reply layout (64 bytes, report ID 0x01):
 *   [0] = 0x01        report ID of the vendor IN report
 *   [1] = 0x01        reply relayed from the headset (0x00 = from this device)
 *   [2] = command echo
 *   [3] = status      0x00 = ok, 0x05 = no such property, 0x09 = write refused
 *   [4..] = little-endian value
 *
 * Writes are refused (status 0x09) unless the headset has been switched into
 * software mode (property 0x03 = 2). Settings written that way persist after
 * switching back, so they are bracketed by a scope guard that hands the headset
 * straight back to hardware mode instead of parking it in software mode.
 *
 * Lights are switched with the brightness property (0x02) rather than by painting
 * a frame. Brightness is the headset's own persisted setting and gates whatever
 * effect it is running, so 0 turns the LEDs off and full brightness brings back
 * the user's own effect. On the XT this was tested on, brightness 0 survived a
 * power cycle.
 *
 * The device also broadcasts unsolicited volume events on report 0x0e, which have
 * to be skipped when looking for a reply.
 *
 * Note that plugging the USB-C cable into a Virtuoso XT does not just charge it:
 * the headset re-enumerates as the wired product ID, and the receiver then reports
 * that no headset is attached.
 *
 * Virtuoso XT - Wireless Product ID: 0x0a64 (receiver), Wired Product ID: 0x0a62
 * Virtuoso SE - Wireless Product ID: 0x0a3e (receiver), Wired Product ID: 0x0a3d
 */
class CorsairVirtuosoXT : public CorsairDevice {
public:
    static constexpr uint16_t PID_XT_WIRELESS = 0x0a64; // Wireless receiver (Virtuoso XT)
    static constexpr uint16_t PID_XT_WIRED    = 0x0a62; // Wired USB (Virtuoso XT)
    static constexpr uint16_t PID_SE_WIRELESS = 0x0a3e; // Wireless receiver (Virtuoso SE)
    static constexpr uint16_t PID_SE_WIRED    = 0x0a3d; // Wired USB (Virtuoso SE)

    static constexpr std::array<uint16_t, 4> SUPPORTED_PRODUCT_IDS {
        PID_XT_WIRELESS, PID_XT_WIRED, PID_SE_WIRELESS, PID_SE_WIRED
    };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Corsair Virtuoso XT/SE"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_LIGHTS);
    }

    constexpr capability_detail
    getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        // Interface 3, Usage-Page 0xff42, Usage-ID 0x0001
        return { .usagepage = 0xff42, .usageid = 0x1, .interface_id = 3 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        // Resolving the target already reads the battery level. Neither that nor
        // the charge state needs software mode, which keeps the headset from
        // producing an audible pop just to report its battery.
        auto resolved = resolveTarget(device_handle);
        if (!resolved) {
            return resolved.error();
        }
        const uint32_t level = resolved->battery_level;

        // The level is reported in tenths of a percent.
        if (level > BATTERY_LEVEL_MAX) {
            return DeviceError::protocolError(std::format("Battery level out of range: {}", level));
        }

        // Charge state is a separate property; treat it as advisory so that a
        // firmware which does not implement it still yields a usable level.
        auto status = BATTERY_AVAILABLE;
        if (auto charge_state = readProperty(device_handle, resolved->target, PROP_BATTERY_STATUS);
            charge_state && *charge_state == CHARGE_STATE_CHARGING) {
            status = BATTERY_CHARGING;
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);

        return BatteryResult {
            .level_percent  = static_cast<int>(level / 10),
            .status         = status,
            .mic_status     = MICROPHONE_UNKNOWN,
            .query_duration = duration,
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // The headset stores the sidetone volume as 0-1000 in steps of 10.
        const uint16_t mapped_level
            = map<uint16_t>(level, 0, 128, SIDETONE_DEVICE_MIN, SIDETONE_DEVICE_MAX);
        const auto sidetone_value
            = static_cast<uint16_t>(round_to_multiples(mapped_level, 10));

        auto resolved = resolveTarget(device_handle);
        if (!resolved) {
            return resolved.error();
        }
        const uint8_t target = resolved->target;

        if (auto result = writeProperty(device_handle, target, PROP_MODE, MODE_SOFTWARE);
            !result) {
            return result.error();
        }
        SoftwareModeGuard guard { *this, device_handle, target };

        // Level 0 switches sidetone off outright rather than turning it down.
        if (auto result = writeProperty(device_handle, target, PROP_SIDETONE_ENABLED,
                level == 0 ? 0 : 1);
            !result) {
            return result.error();
        }

        if (level > 0) {
            if (auto result
                = writeProperty(device_handle, target, PROP_SIDETONE_VOLUME, sidetone_value);
                !result) {
                return result.error();
            }
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            // The native range is 0-1000, which does not fit the single byte this
            // struct exposes, so report it as a percentage instead.
            .device_max = 100,
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        if (minutes > MAX_INACTIVE_MINUTES) {
            minutes = MAX_INACTIVE_MINUTES;
        }

        auto resolved = resolveTarget(device_handle);
        if (!resolved) {
            return resolved.error();
        }
        const uint8_t target = resolved->target;

        if (auto result = writeProperty(device_handle, target, PROP_MODE, MODE_SOFTWARE);
            !result) {
            return result.error();
        }
        SoftwareModeGuard guard { *this, device_handle, target };

        if (auto result
            = writeProperty(device_handle, target, PROP_SLEEP_ENABLED, minutes == 0 ? 0 : 1);
            !result) {
            return result.error();
        }

        // The timeout itself is stored in milliseconds.
        if (minutes > 0) {
            const uint32_t timeout_ms = static_cast<uint32_t>(minutes) * 60U * 1000U;
            if (auto result
                = writeProperty(device_handle, target, PROP_SLEEP_TIMEOUT, timeout_ms);
                !result) {
                return result.error();
            }
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = MAX_INACTIVE_MINUTES,
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        auto resolved = resolveTarget(device_handle);
        if (!resolved) {
            return resolved.error();
        }
        const uint8_t target = resolved->target;

        if (auto result = writeProperty(device_handle, target, PROP_MODE, MODE_SOFTWARE);
            !result) {
            return result.error();
        }
        SoftwareModeGuard guard { *this, device_handle, target };

        // Brightness is the headset's own persisted setting rather than a frame we
        // paint, so it gates whatever effect the headset is running: 0 switches the
        // LEDs off, full brightness brings back the user's own effect.
        if (auto result
            = writeProperty(device_handle, target, PROP_BRIGHTNESS, on ? BRIGHTNESS_MAX : 0);
            !result) {
            return result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<CapabilityInfo> getCapabilityInfo(enum capabilities cap) override
    {
        auto info = HIDDevice::getCapabilityInfo(cap);
        if (!info) {
            return info;
        }

        switch (cap) {
        case CAP_SIDETONE:
            info->parameter
                = CapabilityInfo::RangeParam { .min = 0, .max = 128, .step = 1, .units = "level" };
            break;

        case CAP_INACTIVE_TIME:
            info->parameter = CapabilityInfo::RangeParam {
                .min = 0, .max = MAX_INACTIVE_MINUTES, .step = 1, .units = "minutes"
            };
            break;

        default:
            break;
        }

        return info;
    }

private:
    static constexpr uint8_t REPORT_ID_OUT = 0x02;
    static constexpr uint8_t REPORT_ID_IN  = 0x01;

    // A wireless receiver relays commands to the headset paired with it, whereas a
    // wired headset answers for itself. Addressing the wrong one is not reported as
    // an error - the device simply stays silent.
    static constexpr uint8_t TARGET_HEADSET     = 0x09;
    static constexpr uint8_t TARGET_SELF        = 0x08;
    static constexpr uint8_t REPLY_FROM_HEADSET = 0x01;
    static constexpr uint8_t REPLY_FROM_SELF    = 0x00;

    static constexpr uint8_t BRAGI_SET = 0x01;
    static constexpr uint8_t BRAGI_GET = 0x02;

    static constexpr uint8_t STATUS_OK          = 0x00;
    static constexpr uint8_t STATUS_NO_PROPERTY = 0x05;

    static constexpr uint8_t PROP_BRIGHTNESS       = 0x02;
    static constexpr uint8_t PROP_MODE             = 0x03;
    static constexpr uint8_t PROP_SLEEP_ENABLED    = 0x0d;
    static constexpr uint8_t PROP_SLEEP_TIMEOUT    = 0x0e;
    static constexpr uint8_t PROP_BATTERY_LEVEL    = 0x0f;
    static constexpr uint8_t PROP_BATTERY_STATUS   = 0x10;
    static constexpr uint8_t PROP_SIDETONE_ENABLED = 0x46;
    static constexpr uint8_t PROP_SIDETONE_VOLUME  = 0x47;

    static constexpr uint16_t MODE_HARDWARE = 1;
    static constexpr uint16_t MODE_SOFTWARE = 2;

    static constexpr uint32_t CHARGE_STATE_CHARGING = 1;

    static constexpr uint16_t BRIGHTNESS_MAX      = 1000;
    static constexpr uint32_t BATTERY_LEVEL_MAX   = 1000;
    static constexpr uint16_t SIDETONE_DEVICE_MIN = 0;
    static constexpr uint16_t SIDETONE_DEVICE_MAX = 1000;
    static constexpr uint8_t MAX_INACTIVE_MINUTES = 90;

    static constexpr size_t MSG_SIZE = 64;
    // Unsolicited reports (volume events) to skip before giving up on a reply
    static constexpr int MAX_READ_ATTEMPTS = 8;
    // Long enough for a reply from a device that is listening, short enough that
    // asking the wrong target does not stall the command. A headset slower than
    // this is reported offline, and its reply can still turn up afterwards: replies
    // carry no property ID, so the next read on this handle for the same target
    // and command would take that late reply for its own.
    static constexpr int TARGET_PROBE_TIMEOUT_MS = 300;

    /**
     * @brief Restores hardware mode when leaving the scope of a write
     *
     * Settings written in software mode persist - sidetone, the sleep timer and
     * brightness all survived a power cycle on the XT this was tested on - so there
     * is nothing to gain by keeping the headset there once the write is done,
     * including a write that failed part way through.
     *
     * That XT also dropped back to hardware mode by itself after a few minutes
     * without host traffic, but that was measured on one unit and nothing here
     * relies on it.
     */
    class SoftwareModeGuard {
    public:
        SoftwareModeGuard(CorsairVirtuosoXT& device, hid_device* device_handle, uint8_t target)
            : device_(device)
            , device_handle_(device_handle)
            , target_(target)
        {
        }

        SoftwareModeGuard(const SoftwareModeGuard&)            = delete;
        SoftwareModeGuard& operator=(const SoftwareModeGuard&) = delete;
        SoftwareModeGuard(SoftwareModeGuard&&)                 = delete;
        SoftwareModeGuard& operator=(SoftwareModeGuard&&)      = delete;

        ~SoftwareModeGuard()
        {
            // Best effort; there is nothing useful to do if the restore fails.
            static_cast<void>(
                device_.writeProperty(device_handle_, target_, PROP_MODE, MODE_HARDWARE));
        }

    private:
        CorsairVirtuosoXT& device_;
        hid_device* device_handle_;
        uint8_t target_;
    };

    static constexpr uint8_t replySourceFor(uint8_t target)
    {
        return target == TARGET_SELF ? REPLY_FROM_SELF : REPLY_FROM_HEADSET;
    }

    /**
     * @brief Work out whether this device answers for itself or relays to a headset
     *
     * The registry hands out a single instance per device class and only records
     * the product ID it last matched on, so that ID is a hint rather than an
     * answer: it goes stale as soon as a wireless receiver and a wired headset are
     * plugged in at the same time. The hint is therefore tried first, but only
     * accepted once the device has answered on it.
     *
     * The battery level is used as the probe because a receiver answers identity
     * properties for itself even when no headset is paired with it, and would
     * otherwise look like a valid target. The level is handed back so that
     * getBattery() does not have to ask for it a second time.
     */
    struct ResolvedTarget {
        uint8_t target;
        uint32_t battery_level;
    };

    [[nodiscard]] Result<ResolvedTarget> resolveTarget(hid_device* device_handle)
    {
        const auto product_id = getMatchedProductId();
        const bool wired      = product_id == PID_XT_WIRED || product_id == PID_SE_WIRED;

        const uint8_t hinted    = wired ? TARGET_SELF : TARGET_HEADSET;
        const uint8_t alternate = wired ? TARGET_HEADSET : TARGET_SELF;

        for (const uint8_t candidate : { hinted, alternate }) {
            if (auto level = readProperty(
                    device_handle, candidate, PROP_BATTERY_LEVEL, TARGET_PROBE_TIMEOUT_MS)) {
                return ResolvedTarget { .target = candidate, .battery_level = *level };
            }
        }

        return DeviceError::deviceOffline("Headset not connected or powered off");
    }

    /**
     * @brief Read a property
     *
     * @return The little-endian value, or an error if the device stays silent or
     *         the property is unknown to this firmware
     */
    [[nodiscard]] Result<uint32_t> readProperty(
        hid_device* device_handle, uint8_t target, uint8_t property, int timeout_ms = 0)
    {
        std::array<uint8_t, MSG_SIZE> request { REPORT_ID_OUT, target, BRAGI_GET, property };
        if (auto result = writeHID(device_handle, request, MSG_SIZE); !result) {
            return result.error();
        }

        auto response = readReply(device_handle, target, BRAGI_GET,
            timeout_ms == 0 ? hsc_device_timeout : timeout_ms);
        if (!response) {
            return response.error();
        }

        const auto& data = *response;
        if (data[3] == STATUS_NO_PROPERTY) {
            return DeviceError::notSupported(
                std::format("Property 0x{:02x} not supported by this firmware", property));
        }
        if (data[3] != STATUS_OK) {
            return DeviceError::protocolError(
                std::format("Read of property 0x{:02x} failed with status 0x{:02x}", property,
                    data[3]));
        }

        return static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8)
            | (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
    }

    /**
     * @brief Write a property
     *
     * Requires software mode; outside it the headset answers with status 0x09.
     */
    [[nodiscard]] Result<void> writeProperty(
        hid_device* device_handle, uint8_t target, uint8_t property, uint32_t value)
    {
        std::array<uint8_t, MSG_SIZE> request { REPORT_ID_OUT, target, BRAGI_SET, property, 0x00,
            static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 24) & 0xFF) };
        if (auto result = writeHID(device_handle, request, MSG_SIZE); !result) {
            return result.error();
        }

        auto response = readReply(device_handle, target, BRAGI_SET, hsc_device_timeout);
        if (!response) {
            return response.error();
        }

        if ((*response)[3] != STATUS_OK) {
            return DeviceError::protocolError(
                std::format("Write of property 0x{:02x} rejected with status 0x{:02x}", property,
                    (*response)[3]));
        }
        return {};
    }

    /**
     * @brief Read the reply to a command, skipping unsolicited reports
     *
     * Volume events arrive on report 0x0e, and when a receiver is in play it also
     * answers some commands on its own behalf, so both are filtered out here.
     */
    [[nodiscard]] Result<std::array<uint8_t, MSG_SIZE>> readReply(
        hid_device* device_handle, uint8_t target, uint8_t command, int timeout_ms)
    {
        std::array<uint8_t, MSG_SIZE> response {};
        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            if (auto result = readHIDTimeout(device_handle, response, timeout_ms); !result) {
                // A headset that is powered off or out of range never answers.
                if (result.error().code == DeviceError::Code::Timeout) {
                    return DeviceError::deviceOffline("Headset not connected or powered off");
                }
                return result.error();
            }

            if (response[0] == REPORT_ID_IN && response[1] == replySourceFor(target)
                && response[2] == command) {
                return response;
            }
        }

        return DeviceError::protocolError(std::format(
            "No reply to command 0x{:02x} after {} reports", command, MAX_READ_ATTEMPTS));
    }
};

} // namespace headsetcontrol

#pragma once

#include "../result_types.hpp"
#include "../utility.hpp"
#include "hid_device.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief FIFINE HT6 Wireless Headset
 *
 * Features:
 * - Battery status (level + charging)
 * - Inactive time (auto power-off)
 * - Lights on/off
 * - Light color
 *
 * Protocol recovered from a USB capture of the vendor's "SoundLink" app and
 * verified on real hardware (dongle firmware 1.8, headset firmware 4.2).
 *
 * The dongle exposes several HID collections; the vendor one is usage page
 * 0xff90 and carries a 64-byte report with report id 0x55 in both directions.
 *
 * State query:  55 65 01 00
 *   reply:      55 65 BB SS MM ... TT ...
 *               BB = battery percent, SS = battery status (3 = charging),
 *               MM = sound mode, TT = inactive time (0xff = disabled)
 *
 * Inactive time: 55 23 01 <minutes>, 0xff disables it.
 *
 * LED writes only take effect while the vendor sequence below is used; the
 * dongle stops the LED engine, accepts the configuration and then starts it
 * again. Writes outside the sequence are silently dropped and the LED query
 * keeps reporting the old value:
 *
 *   55 0c 01                       enable RGB
 *   55 02 00                       stop the LED engine
 *   55 03 MM CC CC II II 00        mode + colour count + interval
 *   55 04 <index> <RGB...>         colour table, four colours per packet
 *   55 05 00                       save to flash
 *   55 01 00                       start the LED engine
 *
 * `55 0c 00` disables the RGB engine entirely (true off), and `55 0b <0|1>`
 * selects the local random mode. Mode values are 0 static, 1 cycle,
 * 2 breathing, 3 rhythm, 4 off, 5 local random.
 *
 * Animated modes take their colours from the uploaded table rather than from
 * a colour register, so the table has to be re-uploaded whenever the colour
 * changes. For breathing and rhythm the vendor app builds a linear fade from
 * the chosen colour to black (128 entries); static mode only uses the first
 * entry, and cycle mode has a fixed rainbow table of its own.
 *
 * The device has no dongle LED control: the green LED on the dongle is a
 * hardware link indicator. `55 0c 00` only turns off the headset LEDs.
 */
class FifineHT6 : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_FIFINE = 0x3142;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x00c3 };

    static constexpr uint16_t USAGE_PAGE = 0xff90;
    static constexpr uint16_t USAGE_ID   = 0x01;

    static constexpr uint8_t REPORT_ID    = 0x55;
    static constexpr size_t REPORT_LENGTH = 64;

    // Command bytes (second byte of the report)
    static constexpr uint8_t CMD_STATE            = 0x65;
    static constexpr uint8_t CMD_LED_STATE        = 0x06;
    static constexpr uint8_t CMD_SLEEP            = 0x23;
    static constexpr uint8_t CMD_LED_ENABLE       = 0x0c;
    static constexpr uint8_t CMD_LED_STOP         = 0x02;
    static constexpr uint8_t CMD_LED_MODE         = 0x03;
    static constexpr uint8_t CMD_LED_TABLE        = 0x04;
    static constexpr uint8_t CMD_LED_SAVE         = 0x05;
    static constexpr uint8_t CMD_LED_START        = 0x01;
    static constexpr uint8_t CMD_LED_LOCAL_SWITCH = 0x0b;

    static constexpr uint8_t STATE_HEADPHONES_OFF = 0x0a;
    static constexpr uint8_t STATE_LINK_LOST      = 0x1a;

    static constexpr uint8_t BATTERY_STATUS_CHARGING = 0x03;

    static constexpr uint8_t INACTIVE_TIME_DISABLED = 0xff;
    static constexpr uint8_t INACTIVE_TIME_MAX      = 90;

    static constexpr uint8_t LED_MODE_STATIC       = 0x00;
    static constexpr uint8_t LED_MODE_CYCLE        = 0x01;
    static constexpr uint8_t LED_MODE_BREATH       = 0x02;
    static constexpr uint8_t LED_MODE_RHYTHM       = 0x03;
    static constexpr uint8_t LED_MODE_OFF          = 0x04;
    static constexpr uint8_t LED_MODE_LOCAL_RANDOM = 0x05;

    static constexpr uint8_t LED_COUNT_STATIC = 0x08;
    static constexpr uint8_t LED_COUNT_CYCLE  = 0x7e;
    static constexpr uint8_t LED_COUNT_ANIM   = 0x80;

    static constexpr int LED_TRANSACTION_DELAY_MS = 10;
    static constexpr int LED_TABLE_DELAY_MS       = 10;
    static constexpr int LED_REPLY_TIMEOUT_MS     = 1000;
    static constexpr int LED_TABLE_ENTRIES        = 128;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_FIFINE;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "FIFINE HT6 Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_LIGHTS) | B(CAP_LIGHT_COLOR);
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Untested on macOS: the dongle exposes several top-level HID
        // collections and macOS opens only the first one, which is not the
        // vendor collection carrying report 0x55.
        return PLATFORM_LINUX | PLATFORM_WINDOWS;
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = USAGE_PAGE, .usageid = USAGE_ID, .interface_id = 0 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto state = queryState(device_handle);
        if (!state) {
            return state.error();
        }

        const auto& reply = *state;
        if (reply[1] == STATE_HEADPHONES_OFF) {
            return DeviceError::timeout("FIFINE HT6: headset is powered off or out of range");
        }
        if (reply[3] == STATE_LINK_LOST) {
            return DeviceError::timeout("FIFINE HT6: wireless link to the headset was lost");
        }

        BatteryResult battery {
            .level_percent = reply[2],
            .status        = reply[3] == BATTERY_STATUS_CHARGING ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { reply.begin(), reply.end() }
        };
        return battery;
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        const uint8_t clamped = std::min<uint8_t>(minutes, INACTIVE_TIME_MAX);
        const uint8_t value   = clamped == 0 ? INACTIVE_TIME_DISABLED : clamped;

        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_SLEEP, 0x01, value }); !result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = clamped,
            .min_minutes = 0,
            .max_minutes = INACTIVE_TIME_MAX
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        if (!on) {
            // 55 0c 00 is the vendor app's RGB disable command.
            if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_ENABLE, 0x00 }); !result) {
                return result.error();
            }
            return LightsResult { .enabled = false, .mode = "off" };
        }

        auto current = readLedState(device_handle);

        // Already on: just make sure the RGB engine is enabled and leave the
        // current effect untouched.
        if (current && current->mode != LED_MODE_OFF) {
            if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_ENABLE, 0x01 }); !result) {
                return result.error();
            }
            return LightsResult { .enabled = true, .mode = std::string(ledModeName(current->mode)) };
        }

        // Was off: restore the colour the device still reports (white if it
        // was black) and start in static mode.
        LightColorSettings color { .r = 0xff, .g = 0xff, .b = 0xff };
        if (current) {
            color = current->color;
            if (color.r == 0 && color.g == 0 && color.b == 0) {
                color = { .r = 0xff, .g = 0xff, .b = 0xff };
            }
        }

        if (auto result = writeLedState(device_handle, color, LED_MODE_STATIC, LED_COUNT_STATIC); !result) {
            return result.error();
        }
        return LightsResult { .enabled = true, .mode = "static" };
    }

    Result<LightColorResult> setLightColor(hid_device* device_handle, const LightColorSettings& color) override
    {
        // Keep breathing/rhythm so changing the colour does not silently stop
        // the effect. Cycle has its own fixed rainbow table and local random
        // ignores the colour, so both fall back to static.
        uint8_t mode = LED_MODE_STATIC;
        if (auto current = readLedState(device_handle); current) {
            if (current->mode == LED_MODE_BREATH || current->mode == LED_MODE_RHYTHM) {
                mode = current->mode;
            }
        }

        if (auto result = writeLedState(device_handle, color, mode, ledCountForMode(mode)); !result) {
            return result.error();
        }

        return LightColorResult { .color = color };
    }

private:
    struct LedState {
        uint8_t mode = LED_MODE_STATIC;
        LightColorSettings color {};
    };

    static constexpr uint8_t ledCountForMode(uint8_t mode)
    {
        switch (mode) {
        case LED_MODE_CYCLE:
            return LED_COUNT_CYCLE;
        case LED_MODE_BREATH:
        case LED_MODE_RHYTHM:
            return LED_COUNT_ANIM;
        default:
            return LED_COUNT_STATIC;
        }
    }

    static std::string_view ledModeName(uint8_t mode)
    {
        switch (mode) {
        case LED_MODE_CYCLE:
            return "cycle"sv;
        case LED_MODE_BREATH:
            return "breathing"sv;
        case LED_MODE_RHYTHM:
            return "rhythm"sv;
        case LED_MODE_OFF:
            return "off"sv;
        case LED_MODE_LOCAL_RANDOM:
            return "local random"sv;
        default:
            return "static"sv;
        }
    }

    Result<void> sendCommand(hid_device* device_handle, std::initializer_list<uint8_t> bytes) const
    {
        std::array<uint8_t, REPORT_LENGTH> report {};
        std::copy(bytes.begin(), bytes.end(), report.begin());
        return writeHID(device_handle, report);
    }

    /**
     * @brief Send a query and wait for the matching reply.
     *
     * The dongle interleaves unsolicited telemetry with replies, so packets
     * whose command byte does not match are skipped until the timeout.
     */
    Result<std::array<uint8_t, REPORT_LENGTH>> readReply(hid_device* device_handle, uint8_t command,
        int timeout_ms) const
    {
        using clock         = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds { timeout_ms };
        std::array<uint8_t, REPORT_LENGTH> response {};

        while (true) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return DeviceError::timeout("No reply from the FIFINE HT6 dongle");
            }

            auto read_result = readHIDTimeout(device_handle, response, static_cast<int>(remaining.count()));
            if (!read_result) {
                return read_result.error();
            }

            if (*read_result >= 2 && response[0] == REPORT_ID && response[1] == command) {
                return response;
            }
        }
    }

    Result<std::array<uint8_t, REPORT_LENGTH>> queryState(hid_device* device_handle) const
    {
        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_STATE, 0x01, 0x00 }); !result) {
            return result.error();
        }

        using clock         = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds { hsc_device_timeout };
        std::array<uint8_t, REPORT_LENGTH> response {};

        while (true) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return DeviceError::timeout("No state reply from the FIFINE HT6 dongle");
            }

            auto read_result = readHIDTimeout(device_handle, response, static_cast<int>(remaining.count()));
            if (!read_result) {
                return read_result.error();
            }

            if (*read_result >= 4 && response[0] == REPORT_ID
                && (response[1] == CMD_STATE || response[1] == STATE_HEADPHONES_OFF
                    || response[3] == STATE_LINK_LOST)) {
                return response;
            }
        }
    }

    Result<LedState> readLedState(hid_device* device_handle) const
    {
        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_STATE, 0x00 }); !result) {
            return result.error();
        }

        auto reply = readReply(device_handle, CMD_LED_STATE, LED_REPLY_TIMEOUT_MS);
        if (!reply) {
            return reply.error();
        }

        LedState state;
        state.mode  = (*reply)[2];
        state.color = { .r = (*reply)[3], .g = (*reply)[4], .b = (*reply)[5] };
        return state;
    }

    /**
     * @brief Upload the colour table used by the LED engine.
     *
     * The vendor app builds a linear fade from the chosen colour to black for
     * breathing/rhythm (128 entries), a single colour for static (8 entries)
     * and a fixed rainbow for cycle. Each packet carries four colours:
     *
     *   55 04 <index_lo> <index_hi> <v0 RGB> <v1 RGB> <v2 RGB> <v3 RGB>
     */
    Result<void> uploadColorTable(hid_device* device_handle, const LightColorSettings& color,
        uint8_t count) const
    {
        for (int base = 0; base < count; base += 4) {
            std::array<uint8_t, REPORT_LENGTH> report {};
            report[0] = REPORT_ID;
            report[1] = CMD_LED_TABLE;
            report[2] = static_cast<uint8_t>(base & 0xff);
            report[3] = static_cast<uint8_t>(base >> 8);

            for (int i = 0; i < 4; ++i) {
                uint8_t r = 0;
                uint8_t g = 0;
                uint8_t b = 0;
                if (base + i < count) {
                    const double scale = 1.0 - static_cast<double>(base + i) / LED_TABLE_ENTRIES;
                    r                  = static_cast<uint8_t>(color.r * scale);
                    g                  = static_cast<uint8_t>(color.g * scale);
                    b                  = static_cast<uint8_t>(color.b * scale);
                }
                report[4 + i * 3 + 0] = r;
                report[4 + i * 3 + 1] = g;
                report[4 + i * 3 + 2] = b;
            }

            if (auto result = writeHID(device_handle, report); !result) {
                return result.error();
            }
            sleep_ms(LED_TABLE_DELAY_MS);
        }
        return {};
    }

    /**
     * @brief Apply colour and mode using the vendor's LED engine sequence.
     *
     * Enable RGB, stop the engine, configure the mode, upload the colour
     * table, save to flash and start the engine. Writes outside this sequence
     * are silently ignored by the dongle.
     */
    Result<void> writeLedState(hid_device* device_handle, const LightColorSettings& color,
        uint8_t mode, uint8_t count) const
    {
        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_ENABLE, 0x01 }); !result) {
            return result.error();
        }
        sleep_ms(LED_TRANSACTION_DELAY_MS);

        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_STOP, 0x00 }); !result) {
            return result.error();
        }
        sleep_ms(LED_TRANSACTION_DELAY_MS);

        const std::array<uint8_t, 7> mode_packet {
            REPORT_ID, CMD_LED_MODE, mode, count, 0x00, 0x0a, 0x00
        };
        std::array<uint8_t, REPORT_LENGTH> report {};
        std::copy(mode_packet.begin(), mode_packet.end(), report.begin());
        if (auto result = writeHID(device_handle, report); !result) {
            return result.error();
        }
        sleep_ms(LED_TRANSACTION_DELAY_MS);

        if (auto result = uploadColorTable(device_handle, color, count); !result) {
            return result.error();
        }

        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_SAVE, 0x00 }); !result) {
            return result.error();
        }
        sleep_ms(LED_TRANSACTION_DELAY_MS);

        if (auto result = sendCommand(device_handle, { REPORT_ID, CMD_LED_START, 0x00 }); !result) {
            return result.error();
        }
        return {};
    }
};

} // namespace headsetcontrol

#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud Alpha 2 Wireless Gaming Headset
 *
 * Features:
 * - Battery status (percentage, charging, voltage)
 *
 * The base station enumerates as two USB devices: 0x0abe (audio) and 0x08be (control MCU).
 * Commands go to the control MCU's vendor interface (interface 2, usage page 0xff13 / usage 0xff00)
 * as 64-byte reports laid out as [command][sub-command][payload...]. Replies use command + 1:
 *   52 01 -> 53 01 <connected>
 *   50 02 -> 51 02 <level %> <charge state> <temp °C, i16 LE> <voltage mV, u16 LE> ...
 * The MCU also acknowledges every command (FF 01, command echoed at offset 14), rejects with
 * FF 02, and pushes unsolicited notifications (FB xx) - e.g. FB 0A 01/00 when the headset
 * links/unlinks. NGENUITY polls the same interface continuously, so replies have to be picked
 * out of unrelated traffic.
 *
 * Hardware verification was on Windows only (on battery, charging, and with the headset powered
 * off); all platforms stay declared.
 *
 * Vendor: HP, Inc (0x03f0)
 */
class HyperXCloudAlpha2Wireless : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_HP = 0x03f0;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x08be // Cloud Alpha 2 Wireless base station (control MCU)
    };

    static constexpr int TIMEOUT_MS  = 1000;
    static constexpr size_t MSG_SIZE = 64;

    static constexpr uint8_t CMD_BATTERY     = 0x50;
    static constexpr uint8_t SUB_BATTERY     = 0x02;
    static constexpr uint8_t CMD_CONNECTION  = 0x52;
    static constexpr uint8_t SUB_CONNECTION  = 0x01;
    static constexpr uint8_t REPORT_RESPONSE = 0xff;
    static constexpr uint8_t RESPONSE_NACK   = 0x02;

    static constexpr uint8_t CHARGE_STATE_CHARGING = 0x01;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_HP;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud Alpha 2 Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff13, .usageid = 0xff00, .interface_id = 2 };
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto connection = query(device_handle, CMD_CONNECTION, SUB_CONNECTION);
        if (!connection) {
            return connection.error();
        }
        if ((*connection)[2] == 0x00) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        auto battery = query(device_handle, CMD_BATTERY, SUB_BATTERY);
        if (!battery) {
            return battery.error();
        }

        const auto& r     = *battery;
        const int level   = r[2];
        const uint8_t chg = r[3];
        if (level > 100) {
            return DeviceError::protocolError("Battery level out of range");
        }

        return BatteryResult {
            .level_percent = level,
            .status        = chg == CHARGE_STATE_CHARGING ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .voltage_mv    = r[6] | (r[7] << 8),
            .raw_data      = std::vector<uint8_t> { r.begin(), r.begin() + 12 }
        };
    }

private:
    /**
     * Send [cmd][sub] and wait for the matching [cmd + 1][sub] reply, skipping ACKs,
     * notifications and replies to other software's requests.
     */
    Result<std::array<uint8_t, MSG_SIZE>> query(hid_device* device_handle, uint8_t cmd, uint8_t sub) const
    {
        std::array<uint8_t, MSG_SIZE> request {};
        request[0] = cmd;
        request[1] = sub;
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_MS);
        std::array<uint8_t, MSG_SIZE> response {};
        while (true) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0) {
                return DeviceError::timeout("No reply from base station");
            }

            response.fill(0);
            auto read = readHIDTimeout(device_handle, response, static_cast<int>(left));
            if (!read) {
                return read.error();
            }

            if (response[0] == cmd + 1 && response[1] == sub) {
                return response;
            }
            if (response[0] == REPORT_RESPONSE && response[1] == RESPONSE_NACK
                && response[14] == cmd && response[15] == sub) {
                return DeviceError::protocolError("Base station rejected the request");
            }
        }
    }
};
} // namespace headsetcontrol

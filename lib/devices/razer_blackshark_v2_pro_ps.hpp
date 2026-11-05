#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Razer BlackShark V2 Pro for PlayStation (2.4 GHz dongle, 1532:1014)
 *
 * Features:
 * - Battery status (voltage, converted to an approximate percentage)
 *
 * The dongle is built on an Airoha chip and speaks the framing used by
 * Razer's own firmware updater. Every request and reply is a 64-byte report
 * with ID 0x02 on the vendor collection (usage page 0xFF00, usage 0x0001,
 * interface 3):
 *
 *   request: 02 80 LL CC CC <payload: LL bytes>
 *            CC CC = CRC16-CCITT (poly 0x1021, init 0) of all 64 bytes with
 *            the CRC field zeroed, little endian
 *   ack:     input report 0x02 fetched over the control pipe; byte 1 is
 *            'O' (0x4F) when the frame was accepted, 'F' (0x46) when not
 *   reply:   02 LL 50 49 .. .. .. .. .. .. .. .. TT RR SS <data>
 *            TT = command type, RR = 0x80 | sequence number of the request,
 *            SS = status (0 = OK); LL counts the bytes after itself
 *
 * Payloads start with "PA" (50 41), then type, sequence number and the body
 * length: one byte for type 0x0E, which talks to the dongle itself, and two
 * bytes (little endian) for type 0x06, which is relayed to the headset while
 * the dongle is in "remote" mode. getBattery() therefore:
 *
 *   1. reads the link status (0x0E, 0xE6); bit 0x08 means a headset is linked
 *   2. switches the dongle to remote mode (0x0E, 0xE1 01) if it is not
 *      already, followed by the "read max length" ioctl the updater sends
 *   3. asks the headset for its battery voltage (0x06, 0x31): uint16 mV
 *   4. always switches the dongle back to local mode (0xE1 00), which is
 *      its normal state, and checks that it took effect
 *
 * Only read commands and this mode switch are sent. Charging state is not
 * reported by the headset, so the status is always "available" while linked.
 */
class RazerBlackSharkV2ProPS : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_RAZER = 0x1532;
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x1014 };

    static constexpr uint16_t USAGE_PAGE   = 0xff00;
    static constexpr uint16_t USAGE_ID     = 0x0001;
    static constexpr int INTERFACE_ID      = 3;
    static constexpr size_t REPORT_SIZE    = 64;
    static constexpr uint8_t REPORT_ID     = 0x02;
    static constexpr uint8_t FRAME_OUT     = 0x80;
    static constexpr uint8_t ACK_OK        = 0x4f;
    static constexpr size_t PAYLOAD_OFFSET = 5;
    static constexpr size_t REPLY_TYPE     = 12;
    static constexpr size_t REPLY_SEQUENCE = 13;
    static constexpr size_t REPLY_STATUS   = 14;
    static constexpr uint8_t TYPE_OTA      = 0x06;
    static constexpr uint8_t TYPE_LINK     = 0x0e;
    static constexpr uint8_t LINK_STATUS   = 0xe6;
    static constexpr uint8_t GET_MODE      = 0xe0;
    static constexpr uint8_t SET_MODE      = 0xe1;
    static constexpr uint8_t MODE_LOCAL    = 0x00;
    static constexpr uint8_t MODE_REMOTE   = 0x01;
    static constexpr uint8_t LINK_HEADSET  = 0x08;
    static constexpr uint8_t GET_BATTERY   = 0x31;
    static constexpr int REPLY_TIMEOUT_MS  = 1500;
    static constexpr int IOCTL_TIMEOUT_MS  = 2500;
    static constexpr int MODE_SETTLE_MS    = 300;
    static constexpr int ATTEMPTS          = 3;
    static constexpr uint16_t MIN_VOLTAGE  = 2500;
    static constexpr uint16_t MAX_VOLTAGE  = 4500;

    // Typical single-cell Li-ion discharge curve; the headset reports no percentage
    static constexpr std::array<std::pair<uint8_t, uint16_t>, 11> CALIBRATION { {
        { 0, 3300 },
        { 5, 3500 },
        { 12, 3600 },
        { 20, 3650 },
        { 30, 3700 },
        { 40, 3750 },
        { 50, 3800 },
        { 65, 3900 },
        { 80, 4000 },
        { 90, 4100 },
        { 100, 4180 },
    } };

    constexpr uint16_t getVendorId() const override { return VENDOR_RAZER; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Razer BlackShark V2 Pro (PlayStation)"sv; }

    constexpr int getCapabilities() const override { return B(CAP_BATTERY_STATUS); }

    // The interface also carries a consumer-control collection; the usage
    // page selects the vendor one, and on Linux the report ID tells them apart
    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = USAGE_PAGE, .usageid = USAGE_ID, .interface_id = INTERFACE_ID };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        const std::lock_guard lock(mutex_);
        const auto start = std::chrono::steady_clock::now();

        auto link = getLinkValue(device_handle, LINK_STATUS);
        if (!link) {
            return link.error();
        }
        if ((*link & LINK_HEADSET) == 0) {
            return BatteryResult { .level_percent = -1, .status = BATTERY_UNAVAILABLE };
        }

        auto voltage = readVoltageInRemoteMode(device_handle);
        if (!voltage) {
            return voltage.error();
        }

        return BatteryResult {
            .level_percent  = voltageToPercent(*voltage, CALIBRATION),
            .status         = BATTERY_AVAILABLE,
            .voltage_mv     = *voltage,
            .query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start),
        };
    }

    /// CRC16-CCITT (poly 0x1021, init 0, no reflection)
    static constexpr uint16_t crc16(std::span<const uint8_t> data)
    {
        uint16_t crc = 0;
        for (uint8_t byte : data) {
            crc ^= static_cast<uint16_t>(byte << 8);
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
            }
        }
        return crc;
    }

    /// Wrap a payload into a 64-byte output report
    static std::array<uint8_t, REPORT_SIZE> buildFrame(std::span<const uint8_t> payload)
    {
        std::array<uint8_t, REPORT_SIZE> frame {};
        frame[0] = REPORT_ID;
        frame[1] = FRAME_OUT;
        frame[2] = static_cast<uint8_t>(payload.size());
        std::copy(payload.begin(), payload.end(), frame.begin() + PAYLOAD_OFFSET);
        const uint16_t crc = crc16(frame);
        frame[3]           = static_cast<uint8_t>(crc & 0xff);
        frame[4]           = static_cast<uint8_t>(crc >> 8);
        return frame;
    }

protected:
    // Overridden in tests to skip the delay
    virtual void settle(std::chrono::milliseconds delay) const { std::this_thread::sleep_for(delay); }

private:
    mutable std::mutex mutex_;
    uint8_t sequence_ = 0;

    Result<uint16_t> readVoltageInRemoteMode(hid_device* device_handle)
    {
        auto mode = getLinkValue(device_handle, GET_MODE);
        if (!mode) {
            return mode.error();
        }

        Result<uint16_t> voltage = DeviceError::protocolError("Dongle did not enter remote mode");
        if (*mode == MODE_REMOTE || (setMode(device_handle, MODE_REMOTE) && isMode(device_handle, MODE_REMOTE))) {
            voltage = requestVoltage(device_handle);
        }

        // Local is the dongle's normal mode; restore it even if the read failed
        for (int attempt = 0; attempt < ATTEMPTS && !isMode(device_handle, MODE_LOCAL); ++attempt) {
            (void)setMode(device_handle, MODE_LOCAL);
        }
        return voltage;
    }

    Result<uint16_t> requestVoltage(hid_device* device_handle)
    {
        Result<uint16_t> last_error = DeviceError::timeout("No battery reply from the headset");
        for (int attempt = 0; attempt < ATTEMPTS; ++attempt) {
            auto reply = command(device_handle, TYPE_OTA, std::array<uint8_t, 1> { GET_BATTERY }, REPLY_TIMEOUT_MS);
            if (!reply) {
                last_error = reply.error();
                continue;
            }
            if (reply->size() < 3 || (*reply)[0] != 0) {
                last_error = DeviceError::protocolError("Headset rejected the battery request");
                continue;
            }
            const uint16_t mv = static_cast<uint16_t>((*reply)[1] | ((*reply)[2] << 8));
            if (mv < MIN_VOLTAGE || mv > MAX_VOLTAGE) {
                last_error = DeviceError::protocolError(std::format("Implausible battery voltage {} mV", mv));
                continue;
            }
            return mv;
        }
        return last_error;
    }

    bool setMode(hid_device* device_handle, uint8_t mode)
    {
        if (!command(device_handle, TYPE_LINK, std::array<uint8_t, 2> { SET_MODE, mode }, REPLY_TIMEOUT_MS)) {
            return false;
        }
        settle(std::chrono::milliseconds { MODE_SETTLE_MS });
        // IOCTL "read max length" (0x5A5A1234, 240): the updater sends it after every switch
        constexpr std::array<uint8_t, 13> read_max_len {
            0x25, 0x34, 0x12, 0x5a, 0x5a, 0x01, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00
        };
        return command(device_handle, TYPE_OTA, read_max_len, IOCTL_TIMEOUT_MS).hasValue();
    }

    bool isMode(hid_device* device_handle, uint8_t mode)
    {
        auto current = getLinkValue(device_handle, GET_MODE);
        return current && *current == mode;
    }

    /// Link-layer GET; the reply data echoes a "PI" frame with the id at [10] and the value at [11]
    Result<uint8_t> getLinkValue(hid_device* device_handle, uint8_t id)
    {
        auto reply = command(device_handle, TYPE_LINK, std::array<uint8_t, 1> { id }, REPLY_TIMEOUT_MS);
        if (!reply) {
            return reply.error();
        }
        const auto& data = *reply;
        if (data.size() < 13 || data[0] != 0 || data[1] != 0x50 || data[2] != 0x49 || data[11] != id) {
            return DeviceError::protocolError(std::format("Unexpected reply to link query 0x{:02x}", id));
        }
        return data[12];
    }

    /**
     * Send one command and wait for its reply.
     * @return reply bytes starting at the status byte
     */
    Result<std::vector<uint8_t>> command(hid_device* device_handle, uint8_t type,
        std::span<const uint8_t> body, int timeout_ms)
    {
        sequence_            = static_cast<uint8_t>(sequence_ % 127 + 1);
        const uint8_t seq    = sequence_;
        const uint8_t expect = static_cast<uint8_t>(0x80 | seq);

        std::vector<uint8_t> payload { 0x50, 0x41, type, seq, static_cast<uint8_t>(body.size() & 0xff) };
        if (type == TYPE_OTA) {
            payload.push_back(static_cast<uint8_t>(body.size() >> 8));
        }
        payload.insert(payload.end(), body.begin(), body.end());

        const auto frame = buildFrame(payload);
        if (auto result = writeHID(device_handle, frame); !result) {
            return result.error();
        }

        std::array<uint8_t, REPORT_SIZE> ack {};
        ack[0] = REPORT_ID;
        if (auto result = getInputReport(device_handle, ack); !result) {
            return result.error();
        }
        if (ack[1] != ACK_OK) {
            return DeviceError::protocolError(std::format("Dongle rejected command 0x{:02x}", type));
        }

        using clock         = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds { timeout_ms };
        std::array<uint8_t, REPORT_SIZE> reply {};
        while (true) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return DeviceError::timeout(std::format("No reply from the dongle to command 0x{:02x}", type));
            }
            auto read = readHIDTimeout(device_handle, reply, static_cast<int>(remaining.count()));
            if (!read) {
                return read.error();
            }

            // Byte 1 counts the valid bytes after it; the rest of the report is stale
            const size_t end = std::min<size_t>(*read, 2 + static_cast<size_t>(reply[1]));
            const bool ours  = end > REPLY_STATUS && reply[0] == REPORT_ID && reply[2] == 0x50
                && reply[3] == 0x49 && reply[REPLY_TYPE] == type && reply[REPLY_SEQUENCE] == expect;
            if (ours) {
                return std::vector<uint8_t>(reply.begin() + REPLY_STATUS, reply.begin() + end);
            }
        }
    }
};

} // namespace headsetcontrol

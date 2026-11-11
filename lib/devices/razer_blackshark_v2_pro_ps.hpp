#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Razer BlackShark V2 Pro for PlayStation (2.4 GHz dongle, 1532:1014)
 *
 * Features:
 * - Battery status: level, charging state and voltage
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
 *   3. asks the headset for its battery voltage (0x06, 0x31): uint16 mV, then
 *      for its charge state and battery percentage (see below)
 *   4. always switches the dongle back to local mode (0xE1 00), which is
 *      its normal state, and checks that it took effect; if it did not, the
 *      call fails even when the voltage was read
 *
 * Only read commands and this mode switch are sent.
 *
 * The headset keeps its own battery percentage and charge state, which it
 * serves over "customer" (type 0x08) frames. A request body is
 * <id> 00 00; the answer, and the notifications the headset pushes unasked,
 * look like
 *
 *   02 0e 50 49 08 .. .. .. .. 00 04 00 <id> <dir> 01 <value>
 *   dir 1 = reply, 2 = notification
 *   id 0x21 = battery percent
 *   id 0x2a = charge state: 0 no charger, 1 charging, 2 full on the charger,
 *             3 unknown (forgets the state); notifications only say 0 or 1
 *
 * Only state 1 is reported as charging: "full on the charger" is available.
 *
 * Replies carry no sequence number and only come while the dongle is in
 * remote mode, so both are asked for right after the voltage, unless the
 * dongle was already in remote mode when the read started. Notifications
 * arrive on link-up, on every percent change and when the charger is plugged
 * in or out; they are picked up from whatever the dongle sends while a
 * command waits for its reply, so a long-running process sees them within a
 * poll. The headset's own figure is preferred over the voltage, which reads
 * high on the charger; if neither has arrived, the level is estimated from
 * the voltage with the battery table from the headset firmware.
 *
 * Tray frontends may poll every second. The link status is a cheap query and
 * is checked on every call, but the voltage is re-read (and the dongle
 * switched) at most once per VOLTAGE_CACHE; in between the last voltage is
 * returned. A failed read is not retried within FAILURE_BACKOFF, so an
 * unresponsive headset does not make every poll switch the dongle; the one
 * exception is a dongle that could not be switched back to local mode, which
 * is retried on the next call. The cache is kept per device handle, so two
 * dongles do not share it, and is dropped whenever the headset is seen
 * unlinked. It lives in the process: a frontend that starts headsetcontrol
 * for every poll gets a full read (and a mode switch) each time.
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
    static constexpr std::chrono::seconds VOLTAGE_CACHE { 60 };
    static constexpr std::chrono::seconds FAILURE_BACKOFF { 10 };

    static constexpr uint8_t FRAME_CUSTOM    = 0x08;
    static constexpr uint8_t CUSTOM_REPLY    = 0x01;
    static constexpr uint8_t CUSTOM_NOTIFY   = 0x02;
    static constexpr uint8_t IND_BATTERY     = 0x21;
    static constexpr uint8_t IND_CHARGING    = 0x2a;
    static constexpr uint8_t CHARGE_NONE     = 0x00;
    static constexpr uint8_t CHARGE_CHARGING = 0x01;
    static constexpr uint8_t CHARGE_FULL     = 0x02;
    static constexpr uint8_t CHARGE_FAULT    = 0x03;
    static constexpr int CUSTOM_TIMEOUT_MS   = 500;

    // 0-90 % in steps of 10: the battery table in the customer data of the
    // headset firmware 1.0.7 (record 5). It has no point for 100 %, so the
    // last one is an estimate.
    static constexpr std::array<std::pair<uint8_t, uint16_t>, 11> CALIBRATION { {
        { 0, 3304 },
        { 10, 3683 },
        { 20, 3734 },
        { 30, 3766 },
        { 40, 3782 },
        { 50, 3809 },
        { 60, 3869 },
        { 70, 3929 },
        { 80, 3982 },
        { 90, 4074 },
        { 100, 4150 },
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
            voltage_cache_.erase(device_handle);
            reported_.erase(device_handle);
            restoreLocalWhileUnlinked(device_handle);
            return BatteryResult { .level_percent = -1, .status = BATTERY_UNAVAILABLE };
        }

        auto cached        = voltage_cache_.find(device_handle);
        const bool expired = cached == voltage_cache_.end()
            || now() - cached->second.read_at >= (cached->second.voltage ? VOLTAGE_CACHE : FAILURE_BACKOFF);
        if (expired) {
            bool restored = true;
            auto voltage  = readVoltageInRemoteMode(device_handle, restored);
            if (!restored) {
                voltage_cache_.erase(device_handle);
                return voltage.error();
            }
            cached = voltage_cache_.insert_or_assign(device_handle, CachedVoltage { voltage, now() }).first;
        }
        if (!cached->second.voltage) {
            return cached->second.voltage.error();
        }
        const uint16_t mv = *cached->second.voltage;

        const auto reported = reported_.find(device_handle);
        const bool known    = reported != reported_.end();
        const bool charging = known && reported->second.charge_state == CHARGE_CHARGING;
        const int level     = known && reported->second.percent ? *reported->second.percent
                                                                : voltageToPercent(mv, CALIBRATION);

        return BatteryResult {
            .level_percent  = level,
            .status         = charging ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .voltage_mv     = mv,
            .query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start),
        };
    }

    void onConnectionClosed(hid_device* device_handle) const override
    {
        const std::lock_guard lock(mutex_);
        voltage_cache_.erase(device_handle);
        reported_.erase(device_handle);
        unlinked_restore_failed_.erase(device_handle);
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
    // Overridden in tests to move time forward
    virtual std::chrono::steady_clock::time_point now() const { return std::chrono::steady_clock::now(); }

private:
    struct CachedVoltage {
        Result<uint16_t> voltage; // the last read, successful or not
        std::chrono::steady_clock::time_point read_at;
    };

    // Last values the headset reported, asked for or not
    struct Reported {
        std::optional<uint8_t> percent;
        std::optional<uint8_t> charge_state;
    };

    mutable std::mutex mutex_;
    uint8_t sequence_ = 0;
    mutable std::unordered_map<hid_device*, CachedVoltage> voltage_cache_;
    mutable std::unordered_map<hid_device*, Reported> reported_;
    mutable std::unordered_map<hid_device*, std::chrono::steady_clock::time_point> unlinked_restore_failed_;

    /// @param restored set to false when the dongle may have been left in remote mode
    Result<uint16_t> readVoltageInRemoteMode(hid_device* device_handle, bool& restored)
    {
        auto mode = getLinkValue(device_handle, GET_MODE);
        if (!mode) {
            return mode.error();
        }
        if (*mode != MODE_LOCAL && *mode != MODE_REMOTE) {
            // Not a state this driver puts the dongle in (a firmware update?); leave it alone
            return DeviceError::protocolError(std::format("Dongle is in unknown mode 0x{:02x}", *mode));
        }

        Result<uint16_t> voltage = DeviceError::protocolError("Dongle did not enter remote mode");
        if (*mode == MODE_REMOTE || (setMode(device_handle, MODE_REMOTE) && isMode(device_handle, MODE_REMOTE))) {
            voltage = requestVoltage(device_handle);
            // Optional extras: on failure the last known values (or the
            // voltage) are used. Not asked when the dongle was already in
            // remote mode, which may be another tool's firmware update.
            if (voltage && *mode == MODE_LOCAL) {
                (void)customGet(device_handle, IND_CHARGING);
                (void)customGet(device_handle, IND_BATTERY);
            }
        }

        // Local is the dongle's normal mode; restore it even if the read failed.
        // Leaving the dongle in remote mode outlives this call, so it is
        // reported even when the voltage read succeeded.
        if (!restoreLocal(device_handle)) {
            restored = false;
            return DeviceError::protocolError("Could not switch the dongle back to local mode");
        }
        return voltage;
    }

    /**
     * A dongle left in remote mode (say, by a crash in the middle of a read)
     * is put back even though there is nothing to read. A failed attempt is
     * not repeated within FAILURE_BACKOFF, so a dongle that refuses does not
     * make every poll of a switched-off headset run the switch.
     */
    void restoreLocalWhileUnlinked(hid_device* device_handle)
    {
        const auto failed = unlinked_restore_failed_.find(device_handle);
        if (failed != unlinked_restore_failed_.end() && now() - failed->second < FAILURE_BACKOFF) {
            return;
        }
        auto mode = getLinkValue(device_handle, GET_MODE);
        if (!mode || *mode != MODE_REMOTE) {
            return; // local, or a mode this driver does not touch
        }
        if (restoreLocal(device_handle)) {
            unlinked_restore_failed_.erase(device_handle);
        } else {
            unlinked_restore_failed_.insert_or_assign(device_handle, now());
        }
    }

    /// Switch back to local mode, verifying every attempt including the last
    bool restoreLocal(hid_device* device_handle)
    {
        bool local = isMode(device_handle, MODE_LOCAL);
        for (int attempt = 0; attempt < ATTEMPTS && !local; ++attempt) {
            (void)setMode(device_handle, MODE_LOCAL);
            local = isMode(device_handle, MODE_LOCAL);
        }
        return local;
    }

    /// Customer GET; the value is stored by noteIndication() when the reply arrives
    Result<std::vector<uint8_t>> customGet(hid_device* device_handle, uint8_t id)
    {
        return command(device_handle, FRAME_CUSTOM, std::array<uint8_t, 3> { id, 0x00, 0x00 }, CUSTOM_TIMEOUT_MS, id);
    }

    /// A one-byte customer frame from the headset: <id> <dir> 01 <value>
    static bool isCustomFrame(std::span<const uint8_t> report)
    {
        return report.size() == 16 && report[4] == FRAME_CUSTOM && report[10] == 0x04 && report[11] == 0x00
            && (report[13] == CUSTOM_REPLY || report[13] == CUSTOM_NOTIFY) && report[14] == 0x01;
    }

    /// Remember a battery percentage or charge state reported by the headset
    void noteIndication(hid_device* device_handle, std::span<const uint8_t> report)
    {
        if (!isCustomFrame(report)) {
            return;
        }
        const uint8_t value = report[15];
        if (report[12] == IND_BATTERY && value <= 100) {
            reported_[device_handle].percent = value;
        } else if (report[12] == IND_CHARGING && value <= CHARGE_FULL) {
            reported_[device_handle].charge_state = value;
        } else if (report[12] == IND_CHARGING && value == CHARGE_FAULT) {
            reported_[device_handle].charge_state.reset(); // meaning unknown: claim nothing
        }
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
        auto reply = command(device_handle, TYPE_LINK, std::array<uint8_t, 2> { SET_MODE, mode }, REPLY_TIMEOUT_MS);
        if (!reply || reply->empty() || (*reply)[0] != 0) {
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
     * @param custom_id for a customer GET, the id its reply carries (it has no sequence number)
     * @return reply bytes starting at the status byte, or at the id for a customer GET
     */
    Result<std::vector<uint8_t>> command(hid_device* device_handle, uint8_t type,
        std::span<const uint8_t> body, int timeout_ms, std::optional<uint8_t> custom_id = std::nullopt)
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
            if (ours && !custom_id) {
                return std::vector<uint8_t>(reply.begin() + REPLY_STATUS, reply.begin() + end);
            }
            if (reply[0] == REPORT_ID && reply[2] == 0x50 && reply[3] == 0x49) {
                const std::span<const uint8_t> frame(reply.data(), end);
                noteIndication(device_handle, frame);
                if (custom_id && isCustomFrame(frame) && frame[12] == *custom_id && frame[13] == CUSTOM_REPLY) {
                    return std::vector<uint8_t>(frame.begin() + 12, frame.end());
                }
            }
        }
    }
};

} // namespace headsetcontrol

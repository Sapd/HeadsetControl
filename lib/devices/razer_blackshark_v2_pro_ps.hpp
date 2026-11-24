#pragma once

#include "../result_types.hpp"
#include "../utility.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
 *            'O' (0x4F) when the frame was accepted, 'F' (0x46) when not.
 *            After an idle gap of about half a second the dongle may reject
 *            the next frame without acting on it, so a rejected frame is
 *            sent again after each of RESEND_DELAYS_MS
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
 * Only read commands and this mode switch are sent. (One side effect in the
 * headset firmware: a percent GET while charging starts a 4-second timer
 * whose purpose is unknown.)
 *
 * The headset keeps its own battery percentage and charge state, which it
 * serves over "customer" (type 0x08) frames. A request body is
 * <id> 00 00; the answer, and the notifications the headset pushes unasked,
 * look like
 *
 *   02 0e 50 49 08 .. .. .. .. .. 04 00 <id> <dir> 01 <value>
 *   dir 1 = reply, 2 = notification
 *   id 0x21 = battery percent: a GET returns the raw figure the headset works
 *             out from the voltage; notifications carry its filtered figure,
 *             which only rises on the charger and only falls off it
 *   id 0x2a = charge state: 0 no charger, 1 charging, 2 full on the charger,
 *             3 unknown (forgets the state); notifications only say 0 or 1
 *
 * Only state 1 is reported as charging: "full on the charger" is available.
 *
 * Replies carry no sequence number and only come while the dongle is in
 * remote mode, so both are asked for right after the voltage. A charge state
 * that neither the GET (asked twice) nor a notification confirmed since the
 * previous read is forgotten. Notifications arrive in either mode, on
 * link-up, on every percent change and when the charger is plugged in or
 * out; they are picked up from whatever the dongle sends while a
 * command waits for its reply, so a long-running process sees them within a
 * poll. The headset's own figures are preferred over the voltage, which
 * reads high on the charger, while they are younger than STALE_AFTER; if
 * there are none, the level is estimated from the voltage with the battery
 * table from the headset firmware.
 *
 * Every call holds a ProcessLock for the whole conversation: the dongle
 * acknowledges frames through one shared input report and sends each reply
 * to every open handle, so a tray app and a CLI call would otherwise take
 * each other's acks and replies. After taking it, the reports that queued up
 * on our handle meanwhile are discarded (the headset's figures among them are
 * kept). If another process holds it past LOCK_TIMEOUT, what this handle
 * learnt within STALE_AFTER is returned, or an error if there is nothing;
 * the next calls within BUSY_BACKOFF do not wait for the lock.
 *
 * Tray frontends may poll every second. The link status is a cheap query and
 * is checked on every call, but the voltage is re-read (and the dongle
 * switched) at most once per VOLTAGE_CACHE; in between, the last voltage and
 * the notifications are used. Every switch to remote mode is a window in
 * which a killed process leaves the dongle there, so it is kept rare. A
 * failed read is not retried within FAILURE_BACKOFF, so an unresponsive
 * headset does not make every poll switch the dongle. The mode is checked
 * once per MODE_CHECK, with the headset off too, and a dongle left in remote
 * mode is put back; but remote mode with live traffic belongs to another
 * program (the vendor's firmware updater) and is left alone. Quit
 * HeadsetControl while updating the firmware all the same: a session that
 * pauses looks like a leftover. The cache is kept per device handle, so two
 * dongles do not share it, and is dropped whenever the headset is seen
 * unlinked. A frontend that closes the device after every poll gets it back
 * on the next handle for the same device path (within STALE_AFTER), but it
 * misses the notifications in between, so for it the voltage and the
 * headset's figures are re-read once per REOPENED_CACHE. Kept open, the
 * Headset (or hsc_headset_t) sees the notifications and re-reads once per
 * VOLTAGE_CACHE; either way works. The cache lives in the process: a
 * frontend that starts headsetcontrol for every poll gets a full read, with
 * a mode switch, each time.
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
    // The dongle answers its own link queries within milliseconds; a reply
    // not there by then is waited for no longer before asking again
    static constexpr int LINK_FIRST_TIMEOUT_MS = 300;
    static constexpr int IOCTL_TIMEOUT_MS      = 2500;
    static constexpr int MODE_SETTLE_MS        = 300;
    // Waits before resending a frame the dongle rejected (see the ack in the
    // description above); the second covers a coarse OS timer
    static constexpr std::array<int, 2> RESEND_DELAYS_MS { 20, 60 };
    static constexpr int ATTEMPTS         = 3;
    static constexpr uint16_t MIN_VOLTAGE = 2500;
    static constexpr uint16_t MAX_VOLTAGE = 4500;
    static constexpr std::chrono::seconds VOLTAGE_CACHE { 300 };
    static constexpr std::chrono::seconds REOPENED_CACHE { 60 };
    static constexpr std::chrono::seconds FAILURE_BACKOFF { 10 };
    static constexpr std::chrono::seconds MODE_CHECK { 10 };
    static constexpr std::chrono::seconds STALE_AFTER { 3 * VOLTAGE_CACHE };
    static constexpr std::chrono::seconds LOCK_TIMEOUT { 5 };
    static constexpr std::chrono::seconds BUSY_BACKOFF { 10 };
    static constexpr int FOREIGN_LISTEN_MS = 400;
    static constexpr int MAX_DRAINED       = 256;

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
    static constexpr int FILTER_ESCAPE       = 10;

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

        // The whole conversation, mode switch included, is serialized across
        // processes: the dongle's ack is shared, and another HeadsetControl
        // (a tray app next to a CLI call) would otherwise take our replies.
        // After a busy lock, the next calls do not wait for it for a while.
        const auto busy        = busy_until_.find(device_handle);
        const bool backing_off = busy != busy_until_.end() && now() < busy->second;
        const ProcessLock dongle_lock(lockName(), backing_off ? std::chrono::milliseconds::zero() : lockTimeout());
        if (dongle_lock.status() == ProcessLock::Status::Busy) {
            if (!backing_off) {
                busy_until_.insert_or_assign(device_handle, now() + BUSY_BACKOFF);
            }
            // A dongle we could not switch back stays reported, as it does with the lock
            if (const auto cached = voltage_cache_.find(device_handle);
                cached != voltage_cache_.end() && cached->second.stuck_remote) {
                return cached->second.voltage.error();
            }
            // Another process is in a long conversation, or stuck in one: what we read lately is the best we have
            if (auto known = lastKnown(device_handle, start)) {
                return *known;
            }
            return DeviceError::timeout("The dongle is busy with another HeadsetControl process");
        }
        busy_until_.erase(device_handle);
        if (dongle_lock.recovered()) {
            // The previous holder died mid-conversation and may have left the dongle in remote mode
            voltage_cache_.erase(device_handle);
            mode_checked_.erase(device_handle);
        }
        // Every open handle receives every reply, so while we waited for the
        // lock, other processes' replies piled up here; one with a sequence
        // number that happens to be ours would pass for our reply
        drain(device_handle);

        auto link = getLinkValue(device_handle, LINK_STATUS);
        if (!link) {
            return link.error();
        }
        if ((*link & LINK_HEADSET) == 0) {
            voltage_cache_.erase(device_handle);
            reported_.erase(device_handle);
            (void)checkMode(device_handle);
            return BatteryResult { .level_percent = -1, .status = BATTERY_UNAVAILABLE };
        }

        auto cached        = voltage_cache_.find(device_handle);
        const bool expired = cached == voltage_cache_.end()
            || now() - cached->second.read_at >= (cached->second.voltage ? refreshInterval(device_handle) : FAILURE_BACKOFF);
        if (expired) {
            bool restored       = true;
            const auto previous = cached == voltage_cache_.end() ? std::nullopt : std::optional(cached->second.read_at);
            auto voltage        = readVoltageInRemoteMode(device_handle, restored, previous);
            cached              = voltage_cache_.insert_or_assign(device_handle, CachedVoltage { voltage, now(), !restored }).first;
            mode_checked_.insert_or_assign(device_handle, now());
            reopened_.erase(device_handle); // until the next adoption: a handle that stays open sees the notifications
        } else if (!checkMode(device_handle)) {
            // Reported like a failed switch back after a read, and retried by the next one
            const auto error = DeviceError::protocolError("Could not switch the dongle back to local mode");
            cached->second   = CachedVoltage { error, now(), true };
        }
        if (cached->second.stuck_remote) {
            // Reported until the next read (after FAILURE_BACKOFF) puts it back, figures or not
            return cached->second.voltage.error();
        }
        if (!cached->second.voltage) {
            // The headset's own figures do not need the voltage
            if (auto own = makeResult(device_handle, std::nullopt, start)) {
                return *own;
            }
            return cached->second.voltage.error();
        }
        return *makeResult(device_handle, *cached->second.voltage, start);
    }

    void onConnectionOpened(hid_device* device_handle, std::string_view path) const override
    {
        const std::lock_guard lock(mutex_);
        forget(device_handle); // a reused handle address
        paths_.insert_or_assign(device_handle, std::string(path));
        const auto parked = parked_.find(std::string(path));
        if (parked == parked_.end()) {
            return;
        }
        // The same dongle, closed and opened again by a frontend that does so for every poll
        if (now() - parked->second.closed_at < STALE_AFTER) {
            auto& state = parked->second;
            if (state.voltage) {
                voltage_cache_.insert_or_assign(device_handle, *state.voltage);
            }
            if (state.reported) {
                reported_.insert_or_assign(device_handle, *state.reported);
            }
            if (state.mode_checked) {
                mode_checked_.insert_or_assign(device_handle, *state.mode_checked);
            }
            if (state.busy_until) {
                busy_until_.insert_or_assign(device_handle, *state.busy_until);
            }
            reopened_.insert(device_handle);
        } else {
            parked_.erase(parked);
        }
        // Otherwise left parked, for a second handle on the same path; the next close replaces it
    }

    void onConnectionClosed(hid_device* device_handle) const override
    {
        const std::lock_guard lock(mutex_);
        if (const auto path = paths_.find(device_handle); path != paths_.end()) {
            // Kept for the next handle on the same device path
            Parked state { .closed_at = now() };
            if (auto it = voltage_cache_.find(device_handle); it != voltage_cache_.end()) {
                state.voltage = it->second;
            }
            if (auto it = reported_.find(device_handle); it != reported_.end()) {
                state.reported = it->second;
            }
            if (auto it = mode_checked_.find(device_handle); it != mode_checked_.end()) {
                state.mode_checked = it->second;
            }
            if (auto it = busy_until_.find(device_handle); it != busy_until_.end()) {
                state.busy_until = it->second;
            }
            std::erase_if(parked_, [&](const auto& entry) { return now() - entry.second.closed_at >= STALE_AFTER; });
            parked_.insert_or_assign(path->second, std::move(state));
        }
        forget(device_handle);
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
        payload  = payload.first(std::min(payload.size(), REPORT_SIZE - PAYLOAD_OFFSET));
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
    // Overridden in tests, so that they neither wait nor clash with a running HeadsetControl
    virtual std::chrono::milliseconds lockTimeout() const { return LOCK_TIMEOUT; }
    virtual std::string lockName() const { return std::format("razer-{:04x}-{:04x}", VENDOR_RAZER, PRODUCT_IDS[0]); }
    // Random, so that a process that does not take the lock (an older build)
    // is unlikely to share our sequence numbers; overridden in tests
    virtual uint8_t firstSequence() const
    {
        try {
            std::random_device random;
            return static_cast<uint8_t>(random() % 127);
        } catch (const std::exception&) {
            return static_cast<uint8_t>(std::chrono::steady_clock::now().time_since_epoch().count() % 127);
        }
    }

private:
    struct CachedVoltage {
        Result<uint16_t> voltage; // the last read, successful or not
        std::chrono::steady_clock::time_point read_at;
        bool stuck_remote = false; // the dongle could not be switched back to local mode
    };

    // Last values the headset reported, asked for or not, and when
    struct Reported {
        std::optional<uint8_t> percent;
        std::chrono::steady_clock::time_point percent_at;
        std::optional<uint8_t> charge_state;
        std::chrono::steady_clock::time_point charge_at;
    };

    mutable std::mutex mutex_;
    uint8_t sequence_     = 0;
    bool sequence_seeded_ = false;
    mutable std::unordered_map<hid_device*, CachedVoltage> voltage_cache_;
    mutable std::unordered_map<hid_device*, Reported> reported_;
    mutable std::unordered_map<hid_device*, std::chrono::steady_clock::time_point> mode_checked_;
    mutable std::unordered_map<hid_device*, std::chrono::steady_clock::time_point> busy_until_;

    // State of closed handles, by device path, for the next handle on the same dongle
    struct Parked {
        std::optional<CachedVoltage> voltage;
        std::optional<Reported> reported;
        std::optional<std::chrono::steady_clock::time_point> mode_checked;
        std::optional<std::chrono::steady_clock::time_point> busy_until;
        std::chrono::steady_clock::time_point closed_at;
    };
    mutable std::unordered_map<hid_device*, std::string> paths_;
    mutable std::unordered_map<std::string, Parked> parked_;
    mutable std::unordered_set<hid_device*> reopened_; // its frontend reopens it for every poll

    void forget(hid_device* device_handle) const
    {
        voltage_cache_.erase(device_handle);
        reported_.erase(device_handle);
        mode_checked_.erase(device_handle);
        busy_until_.erase(device_handle);
        paths_.erase(device_handle);
        reopened_.erase(device_handle);
    }

    /// A handle that is closed between polls misses the notifications, so it re-reads more often
    std::chrono::seconds refreshInterval(hid_device* device_handle) const
    {
        return reopened_.contains(device_handle) ? REOPENED_CACHE : VOLTAGE_CACHE;
    }

    /// The headset's own figures while they are recent, else the voltage estimate; nullopt if neither
    std::optional<BatteryResult> makeResult(
        hid_device* device_handle, std::optional<uint16_t> mv, std::chrono::steady_clock::time_point start) const
    {
        std::optional<uint8_t> percent;
        bool charging = false;
        if (const auto reported = reported_.find(device_handle); reported != reported_.end()) {
            const auto& r = reported->second;
            if (r.percent && now() - r.percent_at < STALE_AFTER) {
                percent = r.percent;
            }
            charging = r.charge_state == CHARGE_CHARGING && now() - r.charge_at < STALE_AFTER;
        }
        if (!percent && !mv) {
            return std::nullopt;
        }
        return BatteryResult {
            .level_percent  = percent ? *percent : voltageToPercent(*mv, CALIBRATION),
            .status         = charging ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .voltage_mv     = mv ? std::optional<int>(*mv) : std::nullopt,
            .query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start),
        };
    }

    /// What is known without talking to the dongle, for when another process holds it
    std::optional<BatteryResult> lastKnown(hid_device* device_handle, std::chrono::steady_clock::time_point start) const
    {
        std::optional<uint16_t> mv;
        if (const auto cached = voltage_cache_.find(device_handle);
            cached != voltage_cache_.end() && cached->second.voltage && now() - cached->second.read_at < STALE_AFTER) {
            mv = *cached->second.voltage;
        }
        return makeResult(device_handle, mv, start);
    }

    /// @param restored set to false when the dongle may have been left in remote mode
    /// @param previous when the previous read on this handle was
    Result<uint16_t> readVoltageInRemoteMode(hid_device* device_handle, bool& restored,
        std::optional<std::chrono::steady_clock::time_point> previous)
    {
        auto mode = getLinkValue(device_handle, GET_MODE);
        if (!mode) {
            return mode.error();
        }
        if (*mode != MODE_LOCAL && *mode != MODE_REMOTE) {
            // Not a state this driver puts the dongle in (a firmware update?); leave it alone
            return DeviceError::protocolError(std::format("Dongle is in unknown mode 0x{:02x}", *mode));
        }

        if (*mode == MODE_REMOTE && foreignSession(device_handle)) {
            // Another program (the vendor's firmware updater?) is talking to the headset; leave it alone
            return DeviceError::protocolError("The dongle is in use by another program");
        }

        Result<uint16_t> voltage = DeviceError::protocolError("Dongle did not enter remote mode");
        if (*mode == MODE_REMOTE || (setMode(device_handle, MODE_REMOTE) && isMode(device_handle, MODE_REMOTE))) {
            voltage = requestVoltage(device_handle);
            // Optional extras: on failure the last known percent (or the voltage)
            // is used. A charge state that neither this GET nor a notification
            // confirmed since the previous read is forgotten, so that it
            // neither claims charging nor steers the percent filter.
            if (voltage) {
                // One retry: a lost reply would otherwise forget a charger that is there
                if (!customGet(device_handle, IND_CHARGING) && !customGet(device_handle, IND_CHARGING)) {
                    auto& reported = reported_[device_handle];
                    if (previous && reported.charge_at <= *previous) {
                        reported.charge_state.reset();
                    }
                }
                if (!customGet(device_handle, IND_BATTERY)) {
                    (void)customGet(device_handle, IND_BATTERY);
                }
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
     * A dongle left in remote mode (say, by a process killed in the middle of
     * a read) is put back, at most once per MODE_CHECK, even when there is
     * nothing to read: with the headset off, or between voltage reads. A
     * remote mode another program is using is left alone.
     *
     * @return false when the dongle is in remote mode and could not be put back
     */
    bool checkMode(hid_device* device_handle)
    {
        const auto checked = mode_checked_.find(device_handle);
        if (checked != mode_checked_.end() && now() - checked->second < MODE_CHECK) {
            return true;
        }
        mode_checked_.insert_or_assign(device_handle, now());
        auto mode = getLinkValue(device_handle, GET_MODE);
        if (!mode || *mode != MODE_REMOTE || foreignSession(device_handle)) {
            return true; // local, a mode this driver does not touch, or someone else's session
        }
        return restoreLocal(device_handle);
    }

    /**
     * Whether another program is talking to the dongle in remote mode. We hold
     * the lock, so it is not a HeadsetControl: a remote mode left over from a
     * dead process is silent, while a live session (a firmware update) makes
     * the dongle send replies, which every handle receives.
     */
    bool foreignSession(hid_device* device_handle)
    {
        using clock         = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds { FOREIGN_LISTEN_MS };
        std::array<uint8_t, REPORT_SIZE> report {};
        while (true) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
            if (remaining <= std::chrono::milliseconds::zero()) {
                return false;
            }
            auto read = readHIDTimeout(device_handle, report, static_cast<int>(remaining.count()));
            if (!read) {
                return false;
            }
            const size_t end = std::min<size_t>(*read, 2 + static_cast<size_t>(report[1]));
            if (report[0] != REPORT_ID || end <= REPLY_STATUS || report[2] != 0x50 || report[3] != 0x49) {
                continue;
            }
            const std::span<const uint8_t> frame(report.data(), end);
            noteIndication(device_handle, frame);
            const bool reply = report[4] == 0x01 && (report[REPLY_TYPE] == TYPE_OTA || report[REPLY_TYPE] == TYPE_LINK);
            if (reply || (isCustomFrame(frame) && frame[13] == CUSTOM_REPLY)) {
                return true;
            }
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

    /// Discard queued input reports, keeping what the headset reported in them
    void drain(hid_device* device_handle)
    {
        std::array<uint8_t, REPORT_SIZE> report {};
        for (int i = 0; i < MAX_DRAINED; ++i) {
            auto read = readHIDTimeout(device_handle, report, 0);
            if (!read) {
                return;
            }
            const size_t end = std::min<size_t>(*read, 2 + static_cast<size_t>(report[1]));
            if (report[0] == REPORT_ID && end > 4 && report[2] == 0x50 && report[3] == 0x49) {
                noteIndication(device_handle, std::span<const uint8_t>(report.data(), end));
            }
        }
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
        const auto at       = now();
        auto& reported      = reported_[device_handle];
        if (report[12] == IND_BATTERY && value <= 100) {
            // A notification carries the headset's filtered figure. A GET returns
            // the raw one, worked out from the voltage, so it is filtered the
            // way the firmware does: only up on the charger, only down off it.
            // Without a known charge state there is no direction to filter in,
            // and a jump larger than jitter is a change nobody saw happen (the
            // headset charged or was used while not asked), so it is taken.
            const bool charge_known = reported.charge_state && at - reported.charge_at < STALE_AFTER;
            const bool on_charger
                = charge_known && (*reported.charge_state == CHARGE_CHARGING || *reported.charge_state == CHARGE_FULL);
            const bool filter = report[13] == CUSTOM_REPLY && charge_known && reported.percent
                && at - reported.percent_at < STALE_AFTER && std::abs(value - *reported.percent) <= FILTER_ESCAPE;
            if (filter) {
                reported.percent = on_charger ? std::max(*reported.percent, value) : std::min(*reported.percent, value);
            } else {
                reported.percent = value;
            }
            reported.percent_at = at;
        } else if (report[12] == IND_CHARGING && value <= CHARGE_FULL) {
            reported.charge_state = value;
            reported.charge_at    = at;
        } else if (report[12] == IND_CHARGING && value == CHARGE_FAULT) {
            reported.charge_state.reset(); // meaning unknown: claim nothing
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
        auto reply = command(device_handle, TYPE_LINK, std::array<uint8_t, 1> { id }, LINK_FIRST_TIMEOUT_MS);
        if (!reply && reply.error().code == DeviceError::Code::Timeout) {
            // An accepted query whose reply never came (seen on hardware, rarely);
            // it is a read answered by the dongle itself, so it is asked once more
            reply = command(device_handle, TYPE_LINK, std::array<uint8_t, 1> { id }, REPLY_TIMEOUT_MS);
        }
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
        if (!sequence_seeded_) {
            sequence_        = firstSequence();
            sequence_seeded_ = true;
        }
        sequence_            = static_cast<uint8_t>(sequence_ % 127 + 1);
        const uint8_t seq    = sequence_;
        const uint8_t expect = static_cast<uint8_t>(0x80 | seq);

        std::vector<uint8_t> payload { 0x50, 0x41, type, seq, static_cast<uint8_t>(body.size() & 0xff) };
        if (type == TYPE_OTA) {
            payload.push_back(static_cast<uint8_t>(body.size() >> 8));
        }
        payload.insert(payload.end(), body.begin(), body.end());

        // After about half a second without frames the dongle may reject the
        // next one ('F') without acting on it; the same frame sent again a
        // little later goes through, so a rejected frame is resent
        const auto frame = buildFrame(payload);
        std::array<uint8_t, REPORT_SIZE> ack {};
        for (size_t copy = 0; copy <= RESEND_DELAYS_MS.size() && ack[1] != ACK_OK; ++copy) {
            if (copy > 0) {
                settle(std::chrono::milliseconds { RESEND_DELAYS_MS[copy - 1] });
            }
            if (auto result = writeHID(device_handle, frame); !result) {
                return result.error();
            }
            ack.fill(0);
            ack[0] = REPORT_ID;
            if (auto result = getInputReport(device_handle, ack); !result) {
                return result.error();
            }
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
                && reply[3] == 0x49 && reply[4] == 0x01 && reply[REPLY_TYPE] == type && reply[REPLY_SEQUENCE] == expect;
            if (ours && !custom_id) {
                return std::vector<uint8_t>(reply.begin() + REPLY_STATUS, reply.begin() + end);
            }
            if (reply[0] == REPORT_ID && reply[2] == 0x50 && reply[3] == 0x49) {
                const std::span<const uint8_t> received(reply.data(), end);
                noteIndication(device_handle, received);
                if (custom_id && isCustomFrame(received) && received[12] == *custom_id
                    && received[13] == CUSTOM_REPLY) {
                    return std::vector<uint8_t>(received.begin() + 12, received.end());
                }
            }
        }
    }
};

} // namespace headsetcontrol

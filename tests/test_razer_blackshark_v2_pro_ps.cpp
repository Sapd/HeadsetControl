#include "device_registry.hpp"
#include "devices/razer_blackshark_v2_pro_ps.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace headsetcontrol::testing {

class BlackSharkTestFailure : public std::runtime_error {
public:
    explicit BlackSharkTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define BLACKSHARK_ASSERT(condition, message)                                           \
    do {                                                                                \
        if (!(condition))                                                               \
            throw BlackSharkTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

using Bytes  = std::vector<uint8_t>;
using Device = RazerBlackSharkV2ProPS;

Bytes padded(Bytes bytes)
{
    bytes.resize(Device::REPORT_SIZE, 0);
    return bytes;
}

/**
 * Simulated dongle. Either replays a scripted list of input reports, or answers
 * each written frame the way the real dongle does. An empty input queue reads
 * as a timeout.
 */
class BlackSharkMockHID final : public HIDInterface {
public:
    // Simulated state
    uint8_t link_status        = 0x1b; // headset linked
    uint8_t mode               = Device::MODE_LOCAL;
    uint16_t voltage_mv        = 3948;
    int silent_battery_replies = 0;
    uint8_t ack                = Device::ACK_OK;
    bool send_noise            = false;
    bool ignore_local          = false; // acks the switch to local mode but stays in remote
    size_t fail_writes_from    = SIZE_MAX; // writes from this index on fail, as if unplugged
    std::optional<uint8_t> custom_percent; // answer to the customer GET 0x21, in remote mode
    std::optional<uint8_t> custom_charge; // answer to the customer GET 0x2a, in remote mode

    std::deque<Bytes> scripted; // used instead of the simulation when not empty
    std::deque<Bytes> pending;
    std::vector<Bytes> writes;
    bool bad_crc = false;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        if (writes.size() >= fail_writes_from)
            return DeviceError::hidError("Simulated unplug");
        Bytes frame(data.begin(), data.end());
        writes.push_back(frame);

        Bytes zeroed = padded(frame);
        zeroed[3] = zeroed[4] = 0;
        const uint16_t crc    = Device::crc16(zeroed);
        if (frame.size() != Device::REPORT_SIZE || frame[3] != (crc & 0xff) || frame[4] != (crc >> 8)) {
            bad_crc = true;
        }

        if (scripted.empty()) {
            respond(frame);
        } else {
            // Replayed with the sequence number of the request it answers
            Bytes r = scripted.front();
            scripted.pop_front();
            if (r.size() > Device::REPLY_SEQUENCE && r[4] == 0x01 && (r[Device::REPLY_SEQUENCE] & 0x80))
                r[Device::REPLY_SEQUENCE] = static_cast<uint8_t>(0x80 | frame[8]);
            pending.push_back(r);
        }
        return {};
    }

    Result<void> write(hid_device* handle, std::span<const uint8_t> data, size_t) override
    {
        return write(handle, data);
    }

    Result<size_t> readTimeout(hid_device*, std::span<uint8_t> data, int) override
    {
        if (pending.empty())
            return DeviceError::timeout("Simulated timeout");
        Bytes report = padded(pending.front());
        pending.pop_front();
        std::copy_n(report.begin(), data.size(), data.begin());
        return data.size();
    }

    Result<size_t> getInputReport(hid_device*, std::span<uint8_t> data) override
    {
        std::fill(data.begin(), data.end(), 0);
        data[0] = Device::REPORT_ID;
        data[1] = ack;
        return data.size();
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this device");
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>, size_t) override
    {
        return DeviceError::notSupported("Not used by this device");
    }

    Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this device");
    }

    bool sent(uint8_t type, Bytes body) const
    {
        return std::any_of(writes.begin(), writes.end(), [&](const Bytes& f) {
            const size_t at = f[7] == Device::TYPE_OTA ? 11 : 10;
            return f[7] == type && std::equal(body.begin(), body.end(), f.begin() + at);
        });
    }

    // Queue an indication the headset pushes on its own (captured format)
    void indicate(uint8_t id, uint8_t value)
    {
        pending.push_back({ 0x02, 0x0e, 0x50, 0x49, 0x08, 0xd8, 0xde, 0x35, 0x20, 0x00, 0x04, 0x00, id, 0x02, 0x01,
            value });
    }

private:
    void reply(uint8_t type, uint8_t seq, Bytes data)
    {
        if (ack != Device::ACK_OK)
            return;
        if (send_noise) // a reply to some other request
            pending.push_back({ 0x02, 0x0d, 0x50, 0x49, 0x01, 0xc0, 0, 0, 0, 0x02, 0x03, 0x00, type, 0x80, 0x00 });
        Bytes r { 0x02, static_cast<uint8_t>(13 + data.size()), 0x50, 0x49, 0x01, 0xc0, 0, 0, 0, 0x02, 0x03, 0x00,
            type, static_cast<uint8_t>(0x80 | seq), 0x00 };
        r.insert(r.end(), data.begin(), data.end());
        pending.push_back(r);
    }

    void respond(const Bytes& f)
    {
        const uint8_t type = f[7];
        const uint8_t seq  = f[8];
        if (type == Device::FRAME_CUSTOM) {
            const uint8_t id                   = f[10];
            const std::optional<uint8_t> value = id == Device::IND_BATTERY ? custom_percent
                : id == Device::IND_CHARGING                               ? custom_charge
                                                                           : std::nullopt;
            if (mode == Device::MODE_REMOTE && value && ack == Device::ACK_OK) // format captured on hardware
                pending.push_back({ 0x02, 0x0e, 0x50, 0x49, 0x08, 0xe5, 0x30, 0x0a, 0x2d, 0x00, 0x04, 0x00, id, 0x01,
                    0x01, *value });
            return;
        }
        if (type == Device::TYPE_LINK) {
            const uint8_t id = f[10];
            if (id == Device::SET_MODE) {
                if (!(ignore_local && f[11] == Device::MODE_LOCAL))
                    mode = f[11];
                reply(type, seq, {});
                return;
            }
            const uint8_t value = id == Device::LINK_STATUS ? link_status : mode;
            reply(type, seq, { 0x50, 0x49, 0x0e, 0xe0, 0, 0, 0, 0, 0x02, 0x00, id, value });
            return;
        }
        if (f[11] == Device::GET_BATTERY) {
            if (mode != Device::MODE_REMOTE || silent_battery_replies-- > 0)
                return; // not relayed to the headset
            reply(type, seq, { static_cast<uint8_t>(voltage_mv & 0xff), static_cast<uint8_t>(voltage_mv >> 8), 0, 0 });
            return;
        }
        reply(type, seq, { 0xf0, 0x00, 0x00, 0x00 }); // read-max-length ioctl
    }
};

class TestableBlackShark final : public RazerBlackSharkV2ProPS {
public:
    explicit TestableBlackShark(BlackSharkMockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }
    void settle(std::chrono::milliseconds) const override { }
    std::chrono::steady_clock::time_point now() const override { return clock; }
    uint8_t firstSequence() const override { return 0; } // the capture started at 1
    std::chrono::milliseconds lockTimeout() const override { return std::chrono::milliseconds { 100 }; }

public:
    std::chrono::steady_clock::time_point clock {};

private:
    BlackSharkMockHID& hid_;
};

auto* const TEST_HANDLE = reinterpret_cast<hid_device*>(0x1);

// Frames written and reports received during one battery read on real hardware
// (Windows 11, dongle firmware 1.0.8). Only the meaningful prefix is listed;
// the rest of each 64-byte report is zero (writes) or stale (reads).
const Bytes TX_LINK_STATUS { 0x02, 0x80, 0x06, 0x1a, 0x40, 0x50, 0x41, 0x0e, 0x01, 0x01, 0xe6 };
const Bytes TX_REMOTE { 0x02, 0x80, 0x07, 0xbd, 0xbd, 0x50, 0x41, 0x0e, 0x03, 0x02, 0xe1, 0x01 };
const Bytes TX_IOCTL { 0x02, 0x80, 0x13, 0xbd, 0xdb, 0x50, 0x41, 0x06, 0x04, 0x0d, 0x00, 0x25, 0x34, 0x12, 0x5a,
    0x5a, 0x01, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00 };
const Bytes TX_BATTERY { 0x02, 0x80, 0x07, 0xf6, 0x5d, 0x50, 0x41, 0x06, 0x06, 0x01, 0x00, 0x31 };
const Bytes TX_LOCAL { 0x02, 0x80, 0x07, 0x72, 0xde, 0x50, 0x41, 0x0e, 0x08, 0x02, 0xe1, 0x00 };

const std::vector<Bytes> CAPTURED_REPLIES {
    { 0x02, 0x19, 0x50, 0x49, 0x01, 0xc0, 0x99, 0x5c, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x81, 0x00, 0x50, 0x49, 0x0e, 0xe0,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe6, 0x1b, 0xce },
    { 0x02, 0x19, 0x50, 0x49, 0x01, 0xc0, 0x9e, 0x5c, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x82, 0x00, 0x50, 0x49, 0x0e, 0xe1,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x00, 0xce },
    { 0x02, 0x0d, 0x50, 0x49, 0x01, 0xc0, 0xa4, 0x5c, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x83, 0x00, 0x50, 0x49, 0x0e, 0xe1,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x00, 0xce },
    { 0x02, 0x11, 0x50, 0x49, 0x01, 0xe2, 0xaf, 0xcd, 0xd3, 0x01, 0x07, 0x00, 0x06, 0x84, 0x00, 0xf0, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x00, 0xce },
    { 0x02, 0x19, 0x50, 0x49, 0x01, 0xc0, 0xa6, 0x5d, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x85, 0x00, 0x50, 0x49, 0x0e, 0xe3,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x01, 0xce },
    { 0x02, 0x11, 0x50, 0x49, 0x01, 0xe4, 0xbd, 0xcd, 0xd3, 0x01, 0x07, 0x00, 0x06, 0x86, 0x00, 0x6c, 0x0f, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x01, 0xce },
    // The customer GETs were captured separately, with the headset on the charger
    { 0x02, 0x0e, 0x50, 0x49, 0x08, 0xe5, 0x30, 0x0a, 0x2d, 0x00, 0x04, 0x00, 0x2a, 0x01, 0x01, 0x01 },
    { 0x02, 0x0e, 0x50, 0x49, 0x08, 0xe6, 0xf7, 0x0e, 0x2d, 0x00, 0x04, 0x00, 0x21, 0x01, 0x01, 0x3b },
    { 0x02, 0x19, 0x50, 0x49, 0x01, 0xc0, 0xb7, 0x5d, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x87, 0x00, 0x50, 0x49, 0x0e, 0xe5,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x01, 0xce },
    { 0x02, 0x0d, 0x50, 0x49, 0x01, 0xc0, 0xbc, 0x5d, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x88, 0x00, 0x50, 0x49, 0x0e, 0xe5,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x01, 0xce },
    { 0x02, 0x11, 0x50, 0x49, 0x01, 0xc0, 0xbb, 0x5e, 0x56, 0x02, 0x07, 0x00, 0x06, 0x89, 0x00, 0xf0, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x01, 0xce },
    { 0x02, 0x19, 0x50, 0x49, 0x01, 0xc0, 0xc1, 0x5e, 0x56, 0x02, 0x03, 0x00, 0x0e, 0x8a, 0x00, 0x50, 0x49, 0x0e, 0xe6,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xe0, 0x00, 0xce },
};

void testBlackSharkRegistered()
{
    auto* device = DeviceRegistry::instance().getDevice(0x1532, 0x1014);
    BLACKSHARK_ASSERT(device != nullptr, "BlackShark V2 Pro (PlayStation) should be registered");
    BLACKSHARK_ASSERT(device->getCapabilities() == B(CAP_BATTERY_STATUS), "battery is the only capability");
    auto detail = device->getCapabilityDetail(CAP_BATTERY_STATUS);
    BLACKSHARK_ASSERT(detail.usagepage == 0xff00 && detail.usageid == 0x0001 && detail.interface_id == 3,
        "should select the vendor collection");
}

void testBlackSharkFramesMatchCapture()
{
    auto frame = Device::buildFrame(Bytes(TX_BATTERY.begin() + 5, TX_BATTERY.end()));
    BLACKSHARK_ASSERT(Bytes(frame.begin(), frame.end()) == padded(TX_BATTERY), "battery request frame and CRC");
    frame = Device::buildFrame(Bytes(TX_IOCTL.begin() + 5, TX_IOCTL.end()));
    BLACKSHARK_ASSERT(Bytes(frame.begin(), frame.end()) == padded(TX_IOCTL), "ioctl frame and CRC");
}

void testBlackSharkCapturedSession()
{
    BlackSharkMockHID hid;
    hid.scripted.assign(CAPTURED_REPLIES.begin(), CAPTURED_REPLIES.end());
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue(), "captured session should give a battery level");
    BLACKSHARK_ASSERT(result->voltage_mv == 3948, "voltage is 0x0f6c mV");
    BLACKSHARK_ASSERT(result->level_percent == 59, "the headset's own level");
    BLACKSHARK_ASSERT(result->status == BATTERY_CHARGING, "charge state 1 is charging");
    BLACKSHARK_ASSERT(hid.writes.size() == CAPTURED_REPLIES.size(), "same number of requests as the capture");
    BLACKSHARK_ASSERT(hid.writes[0] == padded(TX_LINK_STATUS), "first request reads the link status");
    BLACKSHARK_ASSERT(hid.writes[2] == padded(TX_REMOTE), "switches to remote mode");
    BLACKSHARK_ASSERT(hid.writes[3] == padded(TX_IOCTL), "sends the ioctl after switching");
    BLACKSHARK_ASSERT(hid.writes[5] == padded(TX_BATTERY), "asks for the battery voltage");
    BLACKSHARK_ASSERT(hid.writes[6][7] == 0x08 && hid.writes[6][9] == 3 && hid.writes[6][10] == 0x2a,
        "asks for the charge state");
    BLACKSHARK_ASSERT(hid.writes[7][7] == 0x08 && hid.writes[7][10] == 0x21, "asks for the battery percent");
    // Same frame as captured, two sequence numbers later because of the customer GETs
    Bytes local_payload(TX_LOCAL.begin() + 5, TX_LOCAL.end());
    local_payload[3] = 10;
    const auto local = Device::buildFrame(local_payload);
    BLACKSHARK_ASSERT(hid.writes[9] == Bytes(local.begin(), local.end()), "switches back to local mode");
}

void testBlackSharkSimulatedRead()
{
    BlackSharkMockHID hid;
    hid.voltage_mv = 3800;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->level_percent == 46, "3800 mV is 46% on the firmware curve");
    BLACKSHARK_ASSERT(result->status == BATTERY_AVAILABLE, "no charging state reported yet");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "dongle is back in local mode");
    BLACKSHARK_ASSERT(!hid.bad_crc, "every frame carries a valid CRC");

    // A later read on the same connection uses new sequence numbers; the cache
    // expires at exactly VOLTAGE_CACHE
    hid.voltage_mv = 3810;
    device.clock += Device::VOLTAGE_CACHE;
    auto again = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(again.hasValue() && again->voltage_mv == 3810, "second read goes to the headset");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "still local after the second read");
}

void testBlackSharkHeadsetOff()
{
    BlackSharkMockHID hid;
    hid.link_status = 0x13;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue(), "an unlinked headset is not an error");
    BLACKSHARK_ASSERT(result->status == BATTERY_UNAVAILABLE && result->level_percent == -1, "headset off");
    BLACKSHARK_ASSERT(hid.writes.size() == 2, "only the link status and the mode are queried");
    BLACKSHARK_ASSERT(!hid.sent(Device::TYPE_LINK, { Device::SET_MODE }), "a local dongle is not switched");
}

void testBlackSharkRestoresLocalWhileUnlinked()
{
    // Left in remote mode by someone else, with the headset off
    BlackSharkMockHID hid;
    hid.link_status = 0x13;
    hid.mode        = Device::MODE_REMOTE;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->status == BATTERY_UNAVAILABLE, "headset off");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "the dongle is put back in local mode");

    // A dongle that refuses is not switched on every poll
    BlackSharkMockHID stuck;
    stuck.link_status  = 0x13;
    stuck.mode         = Device::MODE_REMOTE;
    stuck.ignore_local = true;
    TestableBlackShark device2(stuck);
    const auto switches = [&] {
        return std::count_if(stuck.writes.begin(), stuck.writes.end(), [](const Bytes& f) {
            return f[7] == Device::TYPE_LINK && f[10] == Device::SET_MODE;
        });
    };
    BLACKSHARK_ASSERT(device2.getBattery(TEST_HANDLE).hasValue(), "headset off");
    BLACKSHARK_ASSERT(switches() == Device::ATTEMPTS, "tried");
    device2.clock += std::chrono::seconds { 1 };
    BLACKSHARK_ASSERT(device2.getBattery(TEST_HANDLE).hasValue(), "headset off");
    BLACKSHARK_ASSERT(switches() == Device::ATTEMPTS, "not again within the backoff");
    device2.clock += Device::FAILURE_BACKOFF;
    BLACKSHARK_ASSERT(device2.getBattery(TEST_HANDLE).hasValue(), "headset off");
    BLACKSHARK_ASSERT(switches() == 2 * Device::ATTEMPTS, "retried after it");
}

void testBlackSharkIgnoresStaleReplies()
{
    // Another process's reply to its first request (sequence 1, "headset off"),
    // queued on our handle while we waited for the lock
    BlackSharkMockHID hid;
    hid.pending.push_back({ 0x02, 0x19, 0x50, 0x49, 0x01, 0xc0, 0, 0, 0, 0x02, 0x03, 0x00, Device::TYPE_LINK, 0x81, 0x00,
        0x50, 0x49, 0x0e, 0xe0, 0, 0, 0, 0, 0x02, 0x00, Device::LINK_STATUS, 0x13 });
    hid.indicate(Device::IND_CHARGING, 1); // queued alongside it
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->status == BATTERY_CHARGING, "stale reply skipped, notification kept");
    BLACKSHARK_ASSERT(result->voltage_mv == 3948, "a full read follows");
}

void testBlackSharkBusyLock()
{
    BlackSharkMockHID hid;
    TestableBlackShark device(hid);
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE).hasValue(), "first read");
    const size_t writes = hid.writes.size();

    // Another process in a long conversation on the same dongle
    std::promise<void> held, release;
    auto release_future = release.get_future();
    std::thread other([&] {
        ProcessLock lock("razer-1532-1014", std::chrono::seconds { 10 });
        held.set_value();
        release_future.wait();
    });
    held.get_future().wait();

    auto busy = device.getBattery(TEST_HANDLE);
    BlackSharkMockHID hid2;
    TestableBlackShark fresh(hid2);
    auto none = fresh.getBattery(TEST_HANDLE);
    release.set_value();
    other.join();

    BLACKSHARK_ASSERT(busy.hasValue() && busy->voltage_mv == 3948, "the last read is returned while busy");
    BLACKSHARK_ASSERT(hid.writes.size() == writes, "nothing is sent without the lock");
    BLACKSHARK_ASSERT(none.hasError() && none.error().code == DeviceError::Code::Timeout, "busy without a cache");
    BLACKSHARK_ASSERT(hid2.writes.empty(), "nothing sent there either");
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE).hasValue(), "works again once released");
}

void testBlackSharkCustomGets()
{
    BlackSharkMockHID hid;
    hid.voltage_mv     = 4000;
    hid.custom_charge  = Device::CHARGE_NONE;
    hid.custom_percent = 95;
    TestableBlackShark device(hid);

    auto first = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(first.hasValue() && first->level_percent == 95, "level asked from the headset");
    BLACKSHARK_ASSERT(first->status == BATTERY_AVAILABLE, "not on the charger");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "back in local mode");

    // Asked again with the voltage, once the cache expires
    hid.custom_charge  = Device::CHARGE_CHARGING;
    hid.custom_percent = 97;
    device.clock += Device::VOLTAGE_CACHE;
    auto charging = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(charging.hasValue() && charging->status == BATTERY_CHARGING && charging->level_percent == 97,
        "updated with the voltage");

    // State 3 (meaning unknown) forgets the charge state rather than keep claiming one
    hid.custom_charge = Device::CHARGE_FAULT;
    device.clock += Device::VOLTAGE_CACHE;
    auto fault = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(fault.hasValue() && fault->status == BATTERY_AVAILABLE, "state 3 is not charging");
    hid.custom_charge = Device::CHARGE_CHARGING;
    device.clock += Device::VOLTAGE_CACHE;
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE)->status == BATTERY_CHARGING, "charging again");

    // Out-of-range answers are ignored and the last values kept
    hid.custom_charge  = 9;
    hid.custom_percent = 150;
    device.clock += Device::VOLTAGE_CACHE;
    auto odd = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(odd.hasValue() && odd->status == BATTERY_CHARGING && odd->level_percent == 97, "ignored");

    // Full on the charger is not charging
    hid.custom_charge  = Device::CHARGE_FULL;
    hid.custom_percent = 100;
    device.clock += Device::VOLTAGE_CACHE;
    auto full = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(full.hasValue() && full->status == BATTERY_AVAILABLE && full->level_percent == 100, "full");

    // No answer at all: the voltage estimate, and the read still succeeds
    BlackSharkMockHID silent;
    silent.voltage_mv = 3809;
    TestableBlackShark device2(silent);
    auto estimate = device2.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(estimate.hasValue() && estimate->level_percent == 50 && estimate->status == BATTERY_AVAILABLE,
        "falls back to the voltage");
}

void testBlackSharkPercentFilter()
{
    // GETs return the raw percentage; it is filtered the way the firmware filters its own
    BlackSharkMockHID hid;
    hid.custom_charge  = Device::CHARGE_NONE;
    hid.custom_percent = 80;
    TestableBlackShark device(hid);
    const auto read = [&](uint8_t raw) {
        hid.custom_percent = raw;
        device.clock += Device::VOLTAGE_CACHE;
        return device.getBattery(TEST_HANDLE)->level_percent;
    };
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE)->level_percent == 80, "first reading taken as is");
    BLACKSHARK_ASSERT(read(78) == 78, "falls off the charger");
    BLACKSHARK_ASSERT(read(79) == 78, "does not rise off the charger");

    hid.custom_charge = Device::CHARGE_CHARGING;
    BLACKSHARK_ASSERT(read(81) == 81, "rises on the charger");
    BLACKSHARK_ASSERT(read(80) == 81, "does not fall on the charger");

    // A notification is the headset's filtered figure and is taken as is
    hid.indicate(Device::IND_BATTERY, 79);
    device.clock += std::chrono::seconds { 1 };
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE)->level_percent == 79, "notification wins");
}

void testBlackSharkIndications()
{
    BlackSharkMockHID hid;
    hid.voltage_mv = 4070; // reads high on the charger
    TestableBlackShark device(hid);

    hid.indicate(Device::IND_CHARGING, 1);
    hid.indicate(Device::IND_BATTERY, 45);
    auto charging = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(charging.hasValue() && charging->status == BATTERY_CHARGING, "charger reported");
    BLACKSHARK_ASSERT(charging->level_percent == 45, "the headset's own level wins over the voltage");
    BLACKSHARK_ASSERT(charging->voltage_mv == 4070, "voltage still reported");

    // Picked up between voltage reads, at the start of the next call
    hid.indicate(Device::IND_CHARGING, 0);
    device.clock += std::chrono::seconds { 1 };
    const size_t before = hid.writes.size();
    auto unplugged      = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(unplugged.hasValue() && unplugged->status == BATTERY_AVAILABLE, "charger removed");
    BLACKSHARK_ASSERT(unplugged->level_percent == 45, "level kept");
    BLACKSHARK_ASSERT(hid.writes.size() == before + 1, "seen without a voltage read");

    // Out-of-range values and other ids are ignored
    hid.indicate(Device::IND_BATTERY, 200);
    hid.indicate(Device::IND_CHARGING, 7);
    hid.indicate(0x20, 1);
    auto odd = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(odd.hasValue() && odd->level_percent == 45 && odd->status == BATTERY_AVAILABLE, "ignored");

    // Forgotten when the headset is switched off, then estimated from the voltage again
    hid.link_status = 0x13;
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE).hasValue(), "headset off");
    hid.link_status = 0x1b;
    auto back       = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(back.hasValue() && back->status == BATTERY_AVAILABLE, "charging state forgotten");
    BLACKSHARK_ASSERT(back->level_percent == voltageToPercent(4070, Device::CALIBRATION), "voltage estimate");

    // Kept per handle, and dropped with the connection
    auto* const other = reinterpret_cast<hid_device*>(0x2);
    hid.indicate(Device::IND_CHARGING, 1);
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE)->status == BATTERY_CHARGING, "first dongle charging");
    BLACKSHARK_ASSERT(device.getBattery(other)->status == BATTERY_AVAILABLE, "second dongle not");
    device.onConnectionClosed(TEST_HANDLE);
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE)->status == BATTERY_AVAILABLE, "dropped on close");
}

void testBlackSharkAlreadyRemote()
{
    BlackSharkMockHID hid;
    hid.mode = Device::MODE_REMOTE;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->voltage_mv == 3948, "reads the battery directly");
    BLACKSHARK_ASSERT(!hid.sent(Device::TYPE_LINK, { Device::SET_MODE, Device::MODE_REMOTE }), "no switch to remote");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "left in local mode afterwards");
    BLACKSHARK_ASSERT(!hid.sent(Device::FRAME_CUSTOM, { Device::IND_CHARGING })
            && !hid.sent(Device::FRAME_CUSTOM, { Device::IND_BATTERY }),
        "no customer requests into a remote mode someone else set up");
}

void testBlackSharkRetriesBattery()
{
    BlackSharkMockHID hid;
    hid.silent_battery_replies = 2;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->voltage_mv == 3948, "third attempt succeeds");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "back in local mode");
}

void testBlackSharkRestoresLocalAfterFailure()
{
    BlackSharkMockHID hid;
    hid.silent_battery_replies = 99;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasError() && result.error().code == DeviceError::Code::Timeout, "no reply is a timeout");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "local mode is restored even after a failure");

    BlackSharkMockHID bogus;
    bogus.voltage_mv = 0;
    TestableBlackShark device2(bogus);
    auto bad = device2.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(bad.hasError() && bad.error().code == DeviceError::Code::ProtocolError, "0 mV is rejected");
    BLACKSHARK_ASSERT(bogus.mode == Device::MODE_LOCAL, "local mode is restored after a bad value");
}

void testBlackSharkReportsStuckRemote()
{
    BlackSharkMockHID hid;
    hid.ignore_local = true;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasError() && result.error().code == DeviceError::Code::ProtocolError,
        "a dongle left in remote mode fails the read even though the voltage arrived");
    const auto local_switches = std::count_if(hid.writes.begin(), hid.writes.end(), [](const Bytes& f) {
        return f[7] == Device::TYPE_LINK && f[10] == Device::SET_MODE && f[11] == Device::MODE_LOCAL;
    });
    BLACKSHARK_ASSERT(local_switches == Device::ATTEMPTS, "the switch back is retried");
    BLACKSHARK_ASSERT(hid.writes.back()[10] == Device::GET_MODE, "the last switch is verified too");
    BLACKSHARK_ASSERT(hid.sent(Device::TYPE_OTA, { Device::GET_BATTERY }), "the voltage was asked for");

    hid.ignore_local = false;
    hid.voltage_mv   = 3900;
    auto retry       = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(retry.hasValue() && retry->voltage_mv == 3900, "the next call reads again");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "and restores local mode");

    // A failed restore is reported in preference to a failed read
    BlackSharkMockHID both;
    both.ignore_local           = true;
    both.silent_battery_replies = 99;
    TestableBlackShark device2(both);
    auto failed = device2.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(failed.hasError() && failed.error().code == DeviceError::Code::ProtocolError,
        "restore failure wins over the read timeout");
}

void testBlackSharkBacksOffAfterFailure()
{
    BlackSharkMockHID hid;
    hid.silent_battery_replies = 99;
    TestableBlackShark device(hid);

    auto first = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(first.hasError() && first.error().code == DeviceError::Code::Timeout, "headset does not answer");
    const size_t after_first = hid.writes.size();

    hid.silent_battery_replies = 0;
    device.clock += std::chrono::seconds { 1 };
    auto polled = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(polled.hasError() && polled.error().code == DeviceError::Code::Timeout, "failure is remembered");
    BLACKSHARK_ASSERT(hid.writes.size() == after_first + 1, "and the dongle is not switched again");

    device.clock += Device::FAILURE_BACKOFF;
    auto later = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(later.hasValue() && later->voltage_mv == 3948, "retried after the backoff");
    const size_t after_retry = hid.writes.size();

    device.clock += Device::FAILURE_BACKOFF;
    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE).hasValue(), "a good read");
    BLACKSHARK_ASSERT(hid.writes.size() == after_retry + 1, "is then cached for the full window");
}

void testBlackSharkUnknownMode()
{
    BlackSharkMockHID hid;
    hid.mode = 0x05;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasError() && result.error().code == DeviceError::Code::ProtocolError, "unknown mode");
    BLACKSHARK_ASSERT(!hid.sent(Device::TYPE_LINK, { Device::SET_MODE }), "the dongle is left alone");
    BLACKSHARK_ASSERT(hid.mode == 0x05, "mode unchanged");
}

void testBlackSharkUnplugged()
{
    // Unplugged just before the battery request: the error surfaces, nothing is cached
    BlackSharkMockHID hid;
    hid.fail_writes_from = 4; // link status, mode, switch to remote, ioctl
    TestableBlackShark device(hid);
    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasError(), "a HID error fails the read");

    hid.fail_writes_from = SIZE_MAX;
    hid.mode             = Device::MODE_LOCAL;
    hid.voltage_mv       = 3700;
    auto replugged       = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(replugged.hasValue() && replugged->voltage_mv == 3700, "no stale voltage after the failure");
}

void testBlackSharkRejectedFrame()
{
    BlackSharkMockHID hid;
    hid.ack = 0x46; // 'F'
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasError() && result.error().code == DeviceError::Code::ProtocolError, "rejected frame");
    BLACKSHARK_ASSERT(hid.writes.size() == 1, "stops at the first rejected frame");
}

void testBlackSharkSkipsOtherReplies()
{
    BlackSharkMockHID hid;
    hid.send_noise = true;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->voltage_mv == 3948, "replies to other requests are skipped");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "back in local mode");
}

void testBlackSharkCachesVoltage()
{
    BlackSharkMockHID hid;
    TestableBlackShark device(hid);

    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE).hasValue(), "first read");
    const size_t full_read = hid.writes.size();

    hid.voltage_mv = 3700;
    device.clock += std::chrono::seconds { 30 };
    auto cached = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(cached.hasValue() && cached->voltage_mv == 3948, "voltage is reused within the cache window");
    BLACKSHARK_ASSERT(hid.writes.size() == full_read + 1, "only the link status is queried");

    device.clock += std::chrono::seconds { 31 };
    auto fresh = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(fresh.hasValue() && fresh->voltage_mv == 3700, "re-read after the cache window");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "back in local mode");

    hid.link_status = 0x13;
    auto off        = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(off.hasValue() && off->status == BATTERY_UNAVAILABLE, "headset switched off");

    hid.link_status     = 0x1b;
    hid.voltage_mv      = 4100;
    const size_t before = hid.writes.size();
    auto back           = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(back.hasValue() && back->voltage_mv == 4100, "cache is dropped while the headset is off");
    BLACKSHARK_ASSERT(hid.writes.size() > before + 1, "a full read follows");
}

void testBlackSharkCachePerHandle()
{
    BlackSharkMockHID hid;
    TestableBlackShark device(hid);
    auto* const other = reinterpret_cast<hid_device*>(0x2);

    BLACKSHARK_ASSERT(device.getBattery(TEST_HANDLE).hasValue(), "first dongle");
    hid.voltage_mv = 3700;
    auto second    = device.getBattery(other);
    BLACKSHARK_ASSERT(second.hasValue() && second->voltage_mv == 3700, "a second dongle is read, not served the cache");
    auto first = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(first.hasValue() && first->voltage_mv == 3948, "the first dongle keeps its own cache");

    hid.voltage_mv = 3600;
    device.onConnectionClosed(TEST_HANDLE);
    auto reopened = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(reopened.hasValue() && reopened->voltage_mv == 3600, "closing the handle drops its cache");
    auto kept = device.getBattery(other);
    BLACKSHARK_ASSERT(kept.hasValue() && kept->voltage_mv == 3700, "and only its own");
}

void testBlackSharkVoltageCurve()
{
    BLACKSHARK_ASSERT(voltageToPercent(4200, Device::CALIBRATION) == 100, "full");
    BLACKSHARK_ASSERT(voltageToPercent(3200, Device::CALIBRATION) == 0, "empty");
    BLACKSHARK_ASSERT(voltageToPercent(3782, Device::CALIBRATION) == 40, "firmware table point");
    BLACKSHARK_ASSERT(voltageToPercent(4074, Device::CALIBRATION) == 90, "last firmware table point");
}

void runAllRazerBlackSharkV2ProPSTests()
{
    std::cout << "\n=== Razer BlackShark V2 Pro (PlayStation) Tests ===" << std::endl;
    testBlackSharkRegistered();
    testBlackSharkFramesMatchCapture();
    testBlackSharkCapturedSession();
    testBlackSharkSimulatedRead();
    testBlackSharkHeadsetOff();
    testBlackSharkRestoresLocalWhileUnlinked();
    testBlackSharkIndications();
    testBlackSharkCustomGets();
    testBlackSharkPercentFilter();
    testBlackSharkIgnoresStaleReplies();
    testBlackSharkBusyLock();
    testBlackSharkAlreadyRemote();
    testBlackSharkRetriesBattery();
    testBlackSharkRestoresLocalAfterFailure();
    testBlackSharkReportsStuckRemote();
    testBlackSharkUnplugged();
    testBlackSharkBacksOffAfterFailure();
    testBlackSharkUnknownMode();
    testBlackSharkRejectedFrame();
    testBlackSharkSkipsOtherReplies();
    testBlackSharkCachesVoltage();
    testBlackSharkCachePerHandle();
    testBlackSharkVoltageCurve();
    std::cout << "  Razer BlackShark V2 Pro (PlayStation) tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

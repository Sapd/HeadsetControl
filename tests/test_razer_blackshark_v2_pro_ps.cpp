#include "device_registry.hpp"
#include "devices/razer_blackshark_v2_pro_ps.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
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

    std::deque<Bytes> scripted; // used instead of the simulation when not empty
    std::deque<Bytes> pending;
    std::vector<Bytes> writes;
    bool bad_crc = false;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
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
            pending.push_back(scripted.front());
            scripted.pop_front();
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

private:
    void reply(uint8_t type, uint8_t seq, Bytes data)
    {
        if (ack != Device::ACK_OK)
            return;
        if (send_noise) // a reply to some other request
            pending.push_back({ 0x02, 0x0d, 0x50, 0x49, 0x01, 0xc0, 0, 0, 0, 0x02, 0x03, 0x00, type, 0xff, 0x00 });
        Bytes r { 0x02, static_cast<uint8_t>(13 + data.size()), 0x50, 0x49, 0x01, 0xc0, 0, 0, 0, 0x02, 0x03, 0x00,
            type, static_cast<uint8_t>(0x80 | seq), 0x00 };
        r.insert(r.end(), data.begin(), data.end());
        pending.push_back(r);
    }

    void respond(const Bytes& f)
    {
        const uint8_t type = f[7];
        const uint8_t seq  = f[8];
        if (type == Device::TYPE_LINK) {
            const uint8_t id = f[10];
            if (id == Device::SET_MODE) {
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
    BLACKSHARK_ASSERT(result->level_percent == 72, "3948 mV is about 72%");
    BLACKSHARK_ASSERT(result->status == BATTERY_AVAILABLE, "linked headset is available");
    BLACKSHARK_ASSERT(hid.writes.size() == CAPTURED_REPLIES.size(), "same number of requests as the capture");
    BLACKSHARK_ASSERT(hid.writes[0] == padded(TX_LINK_STATUS), "first request reads the link status");
    BLACKSHARK_ASSERT(hid.writes[2] == padded(TX_REMOTE), "switches to remote mode");
    BLACKSHARK_ASSERT(hid.writes[3] == padded(TX_IOCTL), "sends the ioctl after switching");
    BLACKSHARK_ASSERT(hid.writes[5] == padded(TX_BATTERY), "asks for the battery voltage");
    BLACKSHARK_ASSERT(hid.writes[7] == padded(TX_LOCAL), "switches back to local mode");
}

void testBlackSharkSimulatedRead()
{
    BlackSharkMockHID hid;
    hid.voltage_mv = 3800;
    TestableBlackShark device(hid);

    auto result = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(result.hasValue() && result->level_percent == 50, "3800 mV is 50%");
    BLACKSHARK_ASSERT(hid.mode == Device::MODE_LOCAL, "dongle is back in local mode");
    BLACKSHARK_ASSERT(!hid.bad_crc, "every frame carries a valid CRC");

    // A second read on the same connection uses new sequence numbers
    auto again = device.getBattery(TEST_HANDLE);
    BLACKSHARK_ASSERT(again.hasValue() && again->voltage_mv == 3800, "second read works too");
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
    BLACKSHARK_ASSERT(hid.writes.size() == 1, "nothing but the link status is requested");
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

void testBlackSharkVoltageCurve()
{
    BLACKSHARK_ASSERT(voltageToPercent(4200, Device::CALIBRATION) == 100, "full");
    BLACKSHARK_ASSERT(voltageToPercent(3200, Device::CALIBRATION) == 0, "empty");
    BLACKSHARK_ASSERT(voltageToPercent(3750, Device::CALIBRATION) == 40, "calibration point");
}

void runAllRazerBlackSharkV2ProPSTests()
{
    std::cout << "\n=== Razer BlackShark V2 Pro (PlayStation) Tests ===" << std::endl;
    testBlackSharkRegistered();
    testBlackSharkFramesMatchCapture();
    testBlackSharkCapturedSession();
    testBlackSharkSimulatedRead();
    testBlackSharkHeadsetOff();
    testBlackSharkAlreadyRemote();
    testBlackSharkRetriesBattery();
    testBlackSharkRestoresLocalAfterFailure();
    testBlackSharkRejectedFrame();
    testBlackSharkSkipsOtherReplies();
    testBlackSharkVoltageCurve();
    std::cout << "  Razer BlackShark V2 Pro (PlayStation) tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

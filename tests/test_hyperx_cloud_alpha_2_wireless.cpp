#include "devices/hyperx_cloud_alpha_2_wireless.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class Alpha2TestFailure : public std::runtime_error {
public:
    explicit Alpha2TestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define ALPHA2_ASSERT(condition, message)                                           \
    do {                                                                            \
        if (!(condition))                                                           \
            throw Alpha2TestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

class Alpha2MockHID final : public HIDInterface {
public:
    std::deque<std::vector<uint8_t>> reads;
    std::vector<std::vector<uint8_t>> writes;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        return {};
    }

    Result<void> write(hid_device* handle, std::span<const uint8_t> data, size_t size) override
    {
        std::vector<uint8_t> padded(size);
        std::copy_n(data.begin(), std::min(data.size(), size), padded.begin());
        return write(handle, padded);
    }

    Result<size_t> readTimeout(hid_device*, std::span<uint8_t> data, int) override
    {
        if (reads.empty())
            return DeviceError::timeout("Simulated timeout");
        auto response = std::move(reads.front());
        reads.pop_front();
        const size_t size = std::min(response.size(), data.size());
        std::copy_n(response.begin(), size, data.begin());
        return size;
    }

    Result<void> sendFeatureReport(hid_device* handle, std::span<const uint8_t> data) override { return write(handle, data); }
    Result<void> sendFeatureReport(hid_device* handle, std::span<const uint8_t> data, size_t size) override { return write(handle, data, size); }
    Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override { return DeviceError::notSupported("Not used"); }
    Result<size_t> getInputReport(hid_device*, std::span<uint8_t>) override { return DeviceError::notSupported("Not used"); }
};

class TestableAlpha2 final : public HyperXCloudAlpha2Wireless {
public:
    explicit TestableAlpha2(Alpha2MockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    Alpha2MockHID& hid_;
};

// ACK the base station sends for every command: FF 01, then the command echoed at offset 14
std::vector<uint8_t> ack(uint8_t cmd, uint8_t sub)
{
    std::vector<uint8_t> r(64, 0);
    r[0]  = 0xff;
    r[1]  = 0x01;
    r[14] = cmd;
    r[15] = sub;
    return r;
}

void testBatteryConnected()
{
    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    hid.reads.push_back(ack(0x52, 0x01));
    hid.reads.push_back({ 0x45, 0x02, 0x01 }); // reply to NGENUITY's own polling, must be skipped
    hid.reads.push_back({ 0x53, 0x01, 0x01 });
    hid.reads.push_back(ack(0x50, 0x02));
    hid.reads.push_back({ 0xfb, 0x13, 0x00, 0x5d }); // notification, must be skipped
    // Captured from a real headset: 93%, not charging, 25 °C, 4193 mV
    hid.reads.push_back({ 0x51, 0x02, 0x5d, 0x00, 0x19, 0x00, 0x61, 0x10, 0x01, 0x01, 0x0a });

    auto result = device.getBattery(nullptr);
    ALPHA2_ASSERT(result.hasValue(), "connected headset should report battery");
    ALPHA2_ASSERT(result->level_percent == 93, "level should be byte 2");
    ALPHA2_ASSERT(result->status == BATTERY_AVAILABLE, "not charging");
    ALPHA2_ASSERT(result->voltage_mv == 4193, "voltage should be little-endian bytes 6-7");
    ALPHA2_ASSERT(hid.writes.size() == 2, "should send connection then battery query");
    ALPHA2_ASSERT(hid.writes[0].size() == 64 && hid.writes[0][0] == 0x52 && hid.writes[0][1] == 0x01, "connection query is 52 01");
    ALPHA2_ASSERT(hid.writes[1][0] == 0x50 && hid.writes[1][1] == 0x02, "battery query is 50 02");
}

void testBatteryCharging()
{
    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    hid.reads.push_back({ 0x53, 0x01, 0x01 });
    // Captured from a real headset on USB power: 91%, charging, 28 °C, 4330 mV
    hid.reads.push_back({ 0x51, 0x02, 0x5b, 0x01, 0x1c, 0x00, 0xea, 0x10, 0x01, 0x01, 0x0a });

    auto result = device.getBattery(nullptr);
    ALPHA2_ASSERT(result.hasValue(), "charging headset should report battery");
    ALPHA2_ASSERT(result->status == BATTERY_CHARGING, "charge state 1 is charging");
    ALPHA2_ASSERT(result->level_percent == 91, "level is still reported while charging");
    ALPHA2_ASSERT(result->voltage_mv == 4330, "voltage while charging");
}

void testHeadsetOff()
{
    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    hid.reads.push_back(ack(0x52, 0x01));
    hid.reads.push_back({ 0x53, 0x01, 0x00 });

    auto result = device.getBattery(nullptr);
    ALPHA2_ASSERT(result.hasError(), "powered-off headset should be an error");
    ALPHA2_ASSERT(result.error().code == DeviceError::Code::DeviceOffline, "error should be DeviceOffline");
    ALPHA2_ASSERT(hid.writes.size() == 1, "battery must not be queried when offline");
}

void testRejectedAndTimeout()
{
    {
        Alpha2MockHID hid;
        TestableAlpha2 device(hid);
        auto nack = ack(0x52, 0x01);
        nack[1]   = 0x02;
        hid.reads.push_back(nack);
        auto result = device.getBattery(nullptr);
        ALPHA2_ASSERT(result.hasError(), "NACK should fail the query");
        ALPHA2_ASSERT(result.error().code == DeviceError::Code::ProtocolError, "NACK is a protocol error");
    }
    {
        Alpha2MockHID hid;
        TestableAlpha2 device(hid);
        auto other_nack = ack(0x0e, 0x00); // NACK of another program's command - ignore
        other_nack[1]   = 0x02;
        hid.reads.push_back(other_nack);
        auto result = device.getBattery(nullptr);
        ALPHA2_ASSERT(result.hasError(), "no reply should fail");
        ALPHA2_ASSERT(result.error().code == DeviceError::Code::Timeout, "missing reply is a timeout");
    }
}

void runAllHyperXCloudAlpha2WirelessTests()
{
    std::cout << "\n=== HyperX Cloud Alpha 2 Wireless Tests ===" << std::endl;
    testBatteryConnected();
    testBatteryCharging();
    testHeadsetOff();
    testRejectedAndTimeout();
    std::cout << "  HyperX Cloud Alpha 2 Wireless tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

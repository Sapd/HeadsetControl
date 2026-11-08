#include "device_registry.hpp"
#include "devices/mchose_v9_pro.hpp"
#include "devices/mchose_x9.hpp"

#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class MchoseTestFailure : public std::runtime_error {
public:
    explicit MchoseTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define MCHOSE_ASSERT(condition, message)                                           \
    do {                                                                            \
        if (!(condition))                                                           \
            throw MchoseTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

using Bytes = std::vector<uint8_t>;

auto* const MCHOSE_TEST_HANDLE = reinterpret_cast<hid_device*>(0x1);

Bytes report(Bytes head)
{
    head.resize(MchoseX9::REPORT_LENGTH, 0);
    return head;
}

/**
 * Simulated M HUB dongle: answers the battery query with the unrelated
 * reports the real dongle interleaves (a heartbeat and another field id)
 * before the battery value. An empty queue reads as a timeout.
 */
class MchoseMockHID final : public HIDInterface {
public:
    uint8_t battery = 50;
    bool answer     = true;
    std::vector<Bytes> writes;
    std::deque<Bytes> pending;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        if (answer && data.size() >= 3 && data[0] == 0x55 && data[1] == 0x65 && data[2] == 0x01) {
            pending.push_back(report({ 0x55, 0x00, 0x01 })); // heartbeat
            pending.push_back(report({ 0x55, 0x65, 0x37, 0x05 })); // other telemetry field
            pending.push_back(report({ 0x55, 0x65, battery, 0x02 })); // battery
        }
        return {};
    }

    Result<void> write(hid_device* handle, std::span<const uint8_t> data, size_t size) override
    {
        return write(handle, data.first(size));
    }

    Result<size_t> readTimeout(hid_device*, std::span<uint8_t> data, int) override
    {
        if (pending.empty())
            return DeviceError::timeout("Simulated silence");
        const Bytes next = pending.front();
        pending.pop_front();
        std::fill(data.begin(), data.end(), 0);
        std::copy_n(next.begin(), std::min(next.size(), data.size()), data.begin());
        return std::min(next.size(), data.size());
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

    Result<size_t> getInputReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this device");
    }
};

template <typename Base>
class TestableMchose final : public Base {
public:
    explicit TestableMchose(MchoseMockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    MchoseMockHID& hid_;
};

void testMchoseRegistered()
{
    auto* x9 = DeviceRegistry::instance().getDevice(0x3837, 0x6045);
    MCHOSE_ASSERT(x9 != nullptr && x9->getDeviceName() == "MCHOSE X9 Wireless", "X9 stays registered");

    auto* v9 = DeviceRegistry::instance().getDevice(0x291d, 0x385d);
    MCHOSE_ASSERT(v9 != nullptr, "V9 Pro should be registered");
    MCHOSE_ASSERT(v9->getDeviceName() == "MCHOSE V9 Pro Wireless", "V9 Pro name");
    MCHOSE_ASSERT(v9->getCapabilities() == B(CAP_BATTERY_STATUS), "battery is the only capability");
    auto detail = v9->getCapabilityDetail(CAP_BATTERY_STATUS);
    MCHOSE_ASSERT(detail.usagepage == 0xff90 && detail.usageid == 0x01, "should select the vendor collection");

    MCHOSE_ASSERT(DeviceRegistry::instance().getDevice(0x3837, 0x385d) == nullptr, "ids are not mixed up");
    MCHOSE_ASSERT(DeviceRegistry::instance().getDevice(0x291d, 0x6045) == nullptr, "ids are not mixed up");
}

void testMchoseV9ProBattery()
{
    MchoseMockHID hid;
    TestableMchose<MchoseV9Pro> device(hid);

    auto battery = device.getBattery(MCHOSE_TEST_HANDLE);
    MCHOSE_ASSERT(battery.hasValue(), "battery read should succeed");
    MCHOSE_ASSERT(battery->level_percent == 50, "55 65 32 02 is 50%, as seen on real hardware");
    MCHOSE_ASSERT(battery->status == BATTERY_AVAILABLE, "status available");

    MCHOSE_ASSERT(hid.writes.size() == 1, "one request");
    const Bytes& request = hid.writes.front();
    MCHOSE_ASSERT(request.size() == MchoseX9::REPORT_LENGTH, "64-byte request, like the real one");
    MCHOSE_ASSERT(request[0] == 0x55 && request[1] == 0x65 && request[2] == 0x01 && request[3] == 0x00,
        "request is 55 65 01 00 ...");
}

void testMchoseX9BatterySkipsOtherReports()
{
    MchoseMockHID hid;
    hid.battery = 20;
    TestableMchose<MchoseX9> device(hid);
    auto battery = device.getBattery(MCHOSE_TEST_HANDLE);
    MCHOSE_ASSERT(battery.hasValue() && battery->level_percent == 20, "heartbeat and field 0x05 are skipped");
}

void testMchoseNoAnswer()
{
    MchoseMockHID hid;
    hid.answer = false;
    TestableMchose<MchoseV9Pro> device(hid);
    MCHOSE_ASSERT(!device.getBattery(MCHOSE_TEST_HANDLE).hasValue(), "silence is an error, not 0%");
}

void runAllMchoseTests()
{
    std::cout << "\n=== MCHOSE Tests ===" << std::endl;
    testMchoseRegistered();
    testMchoseV9ProBattery();
    testMchoseX9BatterySkipsOtherReports();
    testMchoseNoAnswer();
    std::cout << "  MCHOSE tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

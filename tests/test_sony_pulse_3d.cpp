#include "device_registry.hpp"
#include "devices/sony_pulse_3d.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class Pulse3DTestFailure : public std::runtime_error {
public:
    explicit Pulse3DTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define PULSE_ASSERT(condition, message)                                             \
    do {                                                                             \
        if (!(condition))                                                            \
            throw Pulse3DTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

// Queued input reports; an empty queue reads as a timeout, like an idle dongle
class Pulse3DMockHID final : public HIDInterface {
public:
    std::deque<Result<std::vector<uint8_t>>> reads;
    int writes = 0;

    Result<void> write(hid_device*, std::span<const uint8_t>) override
    {
        ++writes;
        return {};
    }

    Result<void> write(hid_device*, std::span<const uint8_t>, size_t) override
    {
        ++writes;
        return {};
    }

    Result<size_t> readTimeout(hid_device*, std::span<uint8_t> data, int) override
    {
        if (reads.empty())
            return DeviceError::timeout("Simulated timeout");

        auto result = std::move(reads.front());
        reads.pop_front();
        if (!result)
            return result.error();

        const size_t size = std::min(result->size(), data.size());
        std::copy_n(result->begin(), size, data.begin());
        return size;
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>) override
    {
        ++writes;
        return {};
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>, size_t) override
    {
        ++writes;
        return {};
    }

    Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this test");
    }

    Result<size_t> getInputReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this test");
    }
};

class TestablePulse3D final : public SonyPulse3D {
public:
    explicit TestablePulse3D(Pulse3DMockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    Pulse3DMockHID& hid_;
};

// Reports captured from a real headset
const std::vector<uint8_t> AT_80 { 0xb0, 0x05, 0x0a, 0x50, 0xef, 0x11, 0x11, 0x64 };
const std::vector<uint8_t> AT_30 { 0xb0, 0x05, 0x00, 0x1e, 0xef, 0x15, 0x11, 0x64 };
const std::vector<uint8_t> CHARGING { 0xb0, 0x05, 0x00, 0x80, 0xed, 0x58, 0x11, 0x64 };
const std::vector<uint8_t> OFF { 0xb0, 0x05, 0x14, 0x28, 0xe1, 0x5b, 0x11, 0x64 };
const std::vector<uint8_t> MEDIA_KEY { 0x01, 0x00, 0x00, 0x00, 0x00 };
// Same length as a status report but another collection's report ID
const std::vector<uint8_t> OTHER_COLLECTION_8B { 0x01, 0x05, 0x00, 0x64, 0xef, 0x15, 0x11, 0x64 };

auto* const HANDLE_A = reinterpret_cast<hid_device*>(0x1);
auto* const HANDLE_B = reinterpret_cast<hid_device*>(0x2);

void testPulse3DRegistered()
{
    auto* device = DeviceRegistry::instance().getDevice(0x054c, 0x0d5e);
    PULSE_ASSERT(device != nullptr, "PULSE 3D should be registered");
    PULSE_ASSERT(std::string(device->getDeviceName()) == "Sony PULSE 3D", "device name");
    PULSE_ASSERT(device->getCapabilities() == B(CAP_BATTERY_STATUS), "battery is the only capability");
    auto detail = device->getCapabilityDetail(CAP_BATTERY_STATUS);
    PULSE_ASSERT(detail.usagepage == 0xff01 && detail.usageid == 0x0020 && detail.interface_id == 3,
        "should select the status collection");
}

void testPulse3DLatestQueuedReportWins()
{
    Pulse3DMockHID hid;
    TestablePulse3D device(hid);
    hid.reads.emplace_back(AT_80);
    hid.reads.emplace_back(MEDIA_KEY);
    hid.reads.emplace_back(AT_30);
    hid.reads.emplace_back(OTHER_COLLECTION_8B);

    auto result = device.getBattery(HANDLE_A);
    PULSE_ASSERT(result.hasValue(), "queued status should be read");
    PULSE_ASSERT(result->level_percent == 30, "newest report should win");
    PULSE_ASSERT(result->status == BATTERY_AVAILABLE, "connected and not charging");
    PULSE_ASSERT(result->raw_data && *result->raw_data == AT_30, "raw data is the newest report");
    PULSE_ASSERT(hid.writes == 0, "nothing may be sent to the dongle");
}

void testPulse3DRemembersStatusBetweenCalls()
{
    Pulse3DMockHID hid;
    TestablePulse3D device(hid);
    hid.reads.emplace_back(AT_80);
    PULSE_ASSERT(device.getBattery(HANDLE_A).hasValue(), "first call reads the report");

    // No new report: the dongle stays silent until something changes
    auto again = device.getBattery(HANDLE_A);
    PULSE_ASSERT(again.hasValue(), "second call should reuse the last status");
    PULSE_ASSERT(again->level_percent == 80, "level is remembered");

    // A different connection has seen nothing yet
    auto other = device.getBattery(HANDLE_B);
    PULSE_ASSERT(other.hasError() && other.error().code == DeviceError::Code::Timeout,
        "an unused connection should time out");

    device.onConnectionClosed(HANDLE_A);
    auto closed = device.getBattery(HANDLE_A);
    PULSE_ASSERT(closed.hasError(), "status is forgotten when the connection closes");
}

void testPulse3DChargingKeepsLastLevel()
{
    Pulse3DMockHID hid;
    TestablePulse3D device(hid);
    hid.reads.emplace_back(AT_30);
    hid.reads.emplace_back(CHARGING);

    auto result = device.getBattery(HANDLE_A);
    PULSE_ASSERT(result.hasValue(), "charging report should be read");
    PULSE_ASSERT(result->status == BATTERY_CHARGING, "0x80 means charging");
    PULSE_ASSERT(result->level_percent == 30, "level from before plugging in is kept");

    Pulse3DMockHID fresh_hid;
    TestablePulse3D fresh(fresh_hid);
    fresh_hid.reads.emplace_back(CHARGING);
    auto unknown = fresh.getBattery(HANDLE_A);
    PULSE_ASSERT(unknown.hasValue() && unknown->status == BATTERY_CHARGING, "charging without a level");
    PULSE_ASSERT(unknown->level_percent == -1, "level is unknown until a level report");
}

void testPulse3DPoweredOff()
{
    Pulse3DMockHID hid;
    TestablePulse3D device(hid);
    hid.reads.emplace_back(AT_80);
    hid.reads.emplace_back(OFF);

    auto result = device.getBattery(HANDLE_A);
    PULSE_ASSERT(result.hasValue(), "power-off report should be read");
    PULSE_ASSERT(result->status == BATTERY_UNAVAILABLE, "headset off is unavailable");
    PULSE_ASSERT(result->level_percent == -1, "no level while off");
}

void testPulse3DHIDErrorPropagates()
{
    Pulse3DMockHID hid;
    TestablePulse3D device(hid);
    hid.reads.emplace_back(DeviceError::hidError("unplugged"));

    auto result = device.getBattery(HANDLE_A);
    PULSE_ASSERT(result.hasError() && result.error().code == DeviceError::Code::HIDError,
        "read errors other than timeouts are returned");
}

void runAllSonyPulse3DTests()
{
    std::cout << "\n=== Sony PULSE 3D Tests ===" << std::endl;
    testPulse3DRegistered();
    testPulse3DLatestQueuedReportWins();
    testPulse3DRemembersStatusBetweenCalls();
    testPulse3DChargingKeepsLastLevel();
    testPulse3DPoweredOff();
    testPulse3DHIDErrorPropagates();
    std::cout << "  Sony PULSE 3D tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

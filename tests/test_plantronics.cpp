#include "device_registry.hpp"
#include "devices/plantronics_bt600.hpp"
#include "devices/plantronics_bt700.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class PlantronicsTestFailure : public std::runtime_error {
public:
    explicit PlantronicsTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define PLT_ASSERT(condition, message)                                                   \
    do {                                                                                 \
        if (!(condition))                                                                \
            throw PlantronicsTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

using Bytes = std::vector<uint8_t>;

auto* const PLT_TEST_HANDLE = reinterpret_cast<hid_device*>(0x1);

/**
 * Simulated Poly dongle with a headset behind it. The replies are the first
 * bytes of the INPUT reports (63 bytes on the wire) captured from a BT700
 * with a Voyager Focus 2; everything written to it is recorded.
 *
 * It is as strict as the real thing: a BT700 stalls an OUTPUT report 0x07
 * that is not 504 bytes, and on Windows report 0x06 cannot be written to the
 * 0xFFA2 collection at all.
 */
class PolyMockHID final : public HIDInterface {
public:
    static constexpr size_t IN_SIZE = 63;

    size_t out_size    = PlantronicsBT600::MSG_SIZE;
    bool accepts_nudge = true;
    bool headset_on    = true;

    std::vector<Bytes> writes;
    std::deque<Bytes> pending;

    void reply(Bytes head)
    {
        head.resize(IN_SIZE, 0);
        pending.push_back(std::move(head));
    }

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        Bytes packet(data.begin(), data.end());
        writes.push_back(packet);

        if (packet.size() >= 2 && packet[0] == 0x06) {
            if (!accepts_nudge)
                return DeviceError::hidError("WriteFile: (0x00000001) Incorrect function.");
            return {};
        }
        if (packet.size() != out_size || packet[0] != PlantronicsBT600::REPORT_ID)
            return DeviceError::hidError("Simulated stall (EPIPE)");

        const uint8_t addr = packet[PlantronicsBT600::OFF_ADDR];
        const uint8_t op   = packet[PlantronicsBT600::OFF_OP];
        const uint8_t grp  = packet[PlantronicsBT600::OFF_GRP];
        const uint8_t item = packet[PlantronicsBT600::OFF_ITEM];

        if (op == 0x01 && addr == 0x00) {
            // Dongle: hello ack, its 3-fragment catalogue, "headset attached"
            reply({ 0x07, 0x01, 0x01, 0x10, 0x06, 0x00, 0x00, 0x00, 0x08, 0x01, 0x02 });
            reply({ 0x07, 0x01, 0x03, 0x10, 0xae, 0x00, 0x00, 0x00, 0x09, 0x00, 0x19, 0x00, 0x01, 0x03 });
            reply({ 0x07, 0x02, 0x03, 0x03, 0x0e, 0x0e, 0x3b, 0x04, 0x2e, 0x0a, 0x00, 0x0a, 0x01, 0x0a });
            reply({ 0x07, 0x03, 0x03, 0x0e, 0x20, 0x0e, 0x07, 0x0a, 0x61, 0x0a, 0x55, 0x00, 0x15, 0x03 });
            if (headset_on)
                reply({ 0x07, 0x01, 0x01, 0x10, 0x07, 0x00, 0x00, 0x00, 0x0a, 0x0c, 0x00, 0x02 });
        } else if (!headset_on) {
            // nothing answers for the headset
        } else if (op == 0x01 && addr == 0x20) {
            // Headset: hello ack and its 7-fragment catalogue
            reply({ 0x07, 0x01, 0x01, 0x10, 0x06, 0x02, 0x00, 0x00, 0x08, 0x01, 0x02 });
            reply({ 0x07, 0x01, 0x07, 0x11, 0x82, 0x02, 0x00, 0x00, 0x09, 0x00, 0x2e, 0x00, 0x01, 0x02 });
            reply({ 0x07, 0x02, 0x07, 0x0e, 0x1a, 0x0f, 0x0c, 0x0f, 0x0e, 0x0f, 0x10, 0x0f, 0x13, 0x0f });
            reply({ 0x07, 0x03, 0x07, 0x03, 0x16, 0x04, 0x03, 0x04, 0x06, 0x04, 0x08, 0x04, 0x09, 0x04 });
            reply({ 0x07, 0x04, 0x07, 0x0a, 0x22, 0x0a, 0x26, 0x0a, 0x36, 0x0a, 0x38, 0x0a, 0x52, 0x0a });
            reply({ 0x07, 0x05, 0x07, 0x0a, 0x25, 0x0a, 0x4e, 0x04, 0x17, 0x0a, 0x55, 0x00, 0x42, 0x00 });
            reply({ 0x07, 0x06, 0x07, 0x08, 0x16, 0x0a, 0x01, 0x0a, 0x03, 0x0a, 0x1c, 0x0a, 0x54, 0x0a });
            reply({ 0x07, 0x07, 0x07, 0x0a, 0x1d, 0x0a, 0x21, 0x0e, 0x2c, 0x0e, 0x2d, 0x0e, 0x2e, 0x0e });
        } else if (op == 0x02 && grp == 0x0a && item == 0x1a) {
            // Battery: level 10 of 11, not charging, 1014 minutes left
            reply({ 0x07, 0x01, 0x01, 0x10, 0x0c, 0x02, 0x00, 0x00, 0x03, 0x0a, 0x1a, 0x0a, 0x0b, 0x00, 0x03, 0xf6, 0x01 });
        } else if (op == 0x05) {
            // SET: an attention pulse, the "changed" event, then the ack
            reply({ 0x9b, 0x01 });
            reply({ 0x07, 0x01, 0x01, 0x10, 0x07, 0x02, 0x00, 0x00, 0x0a, grp, item, packet[PlantronicsBT600::OFF_VALUE] });
            reply({ 0x07, 0x01, 0x01, 0x10, 0x06, 0x02, 0x00, 0x00, 0x06, grp, item });
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

template <typename Device>
class TestablePoly final : public Device {
public:
    explicit TestablePoly(PolyMockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    PolyMockHID& hid_;
};

PolyMockHID makeBT700Mock()
{
    PolyMockHID hid;
    hid.out_size      = PlantronicsBT700::OUT_SIZE;
    hid.accepts_nudge = false;
    return hid;
}

void testBT700Registered()
{
    auto& registry = DeviceRegistry::instance();
    auto* bt600    = registry.getDevice(0x047f, 0x02ee);
    auto* bt700    = registry.getDevice(0x047f, 0x02e6);
    PLT_ASSERT(bt600 != nullptr && bt700 != nullptr, "both dongles should be registered");
    PLT_ASSERT(bt600 != bt700, "BT600 and BT700 are separate devices");
    PLT_ASSERT(bt700->getDeviceName() == "Poly Voyager Focus 2 (BT700)", "device name");
    PLT_ASSERT(bt700->getCapabilities() == bt600->getCapabilities(), "same capabilities as the BT600");

    auto detail = bt700->getCapabilityDetail(CAP_BATTERY_STATUS);
    PLT_ASSERT(detail.usagepage == 0xffa2 && detail.usageid == 0x03, "should select the Poly control collection");
}

void testBT700Battery()
{
    auto hid = makeBT700Mock();
    TestablePoly<PlantronicsBT700> device(hid);

    auto result = device.getBattery(PLT_TEST_HANDLE);
    PLT_ASSERT(result.hasValue(), "battery should be read");
    PLT_ASSERT(result->status == BATTERY_AVAILABLE, "battery status");
    PLT_ASSERT(result->level_percent == 100, "level 10 of 11 is 100%");
    PLT_ASSERT(result->time_to_empty_min == 1014, "talk time");

    // dongle hello, headset hello, GET: no 06 01 report, each padded with zeros to 504 bytes
    PLT_ASSERT(hid.writes.size() == 3, "three frames and nothing else");
    for (const auto& frame : hid.writes) {
        PLT_ASSERT(frame.size() == PlantronicsBT700::OUT_SIZE, "frame padded to the OUTPUT report size");
        PLT_ASSERT(frame[0] == PlantronicsBT600::REPORT_ID, "report id 0x07");
        PLT_ASSERT(std::all_of(frame.begin() + 16, frame.end(), [](uint8_t b) { return b == 0; }), "padding is zero");
    }
    PLT_ASSERT(hid.writes[0][PlantronicsBT600::OFF_ADDR] == 0x00, "dongle hello first");
    PLT_ASSERT(hid.writes[1][PlantronicsBT600::OFF_ADDR] == 0x20, "then the headset hello");
    PLT_ASSERT(hid.writes[2][PlantronicsBT600::OFF_OP] == 0x02, "then the GET");
}

void testBT700Sidetone()
{
    auto hid = makeBT700Mock();
    TestablePoly<PlantronicsBT700> device(hid);

    auto result = device.setSidetone(PLT_TEST_HANDLE, 128);
    PLT_ASSERT(result.hasValue(), "sidetone should be set");
    PLT_ASSERT(result->current_level == 2, "128 maps to the highest of 3 levels");

    const auto& set = hid.writes.back();
    PLT_ASSERT(set.size() == PlantronicsBT700::OUT_SIZE, "SET padded to the OUTPUT report size");
    PLT_ASSERT(set[PlantronicsBT600::OFF_ADDR] == 0x20 && set[PlantronicsBT600::OFF_OP] == 0x05, "SET to the headset");
    PLT_ASSERT(set[PlantronicsBT600::OFF_GRP] == 0x04 && set[PlantronicsBT600::OFF_ITEM] == 0x10, "sidetone setting");
    PLT_ASSERT(set[PlantronicsBT600::OFF_VALUE] == 0x02, "level high");
}

void testBT700HeadsetOff()
{
    auto hid       = makeBT700Mock();
    hid.headset_on = false;
    TestablePoly<PlantronicsBT700> device(hid);

    auto result = device.getBattery(PLT_TEST_HANDLE);
    PLT_ASSERT(!result.hasValue(), "no battery without a headset");
    PLT_ASSERT(result.error().code == DeviceError::Code::DeviceOffline, "reported as offline");
}

void testBT600KeepsNudgeAndFrameSize()
{
    PolyMockHID hid;
    TestablePoly<PlantronicsBT600> device(hid);

    auto result = device.getBattery(PLT_TEST_HANDLE);
    PLT_ASSERT(result.hasValue(), "battery should be read");
    PLT_ASSERT(hid.writes.size() == 4, "06 01 and three frames");
    PLT_ASSERT((hid.writes[0] == Bytes { 0x06, 0x01 }), "06 01 report first");
    for (size_t i = 1; i < hid.writes.size(); ++i)
        PLT_ASSERT(hid.writes[i].size() == PlantronicsBT600::MSG_SIZE, "64-byte frames");
}

void runAllPlantronicsTests()
{
    std::cout << "\n=== Plantronics / Poly Tests ===" << std::endl;
    testBT700Registered();
    testBT700Battery();
    testBT700Sidetone();
    testBT700HeadsetOff();
    testBT600KeepsNudgeAndFrameSize();
    std::cout << "  Plantronics / Poly tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

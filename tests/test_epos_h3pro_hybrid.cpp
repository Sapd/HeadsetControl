#include "device_registry.hpp"
#include "devices/epos_h3pro_hybrid.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class H3ProTestFailure : public std::runtime_error {
public:
    explicit H3ProTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define H3PRO_ASSERT(condition, message)                                           \
    do {                                                                           \
        if (!(condition))                                                          \
            throw H3ProTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

// Queued input reports; an empty queue reads as a timeout, like a silent dongle
class H3ProMockHID final : public HIDInterface {
public:
    std::deque<Result<std::vector<uint8_t>>> reads;
    std::vector<std::vector<uint8_t>> writes;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        return {};
    }

    Result<void> write(hid_device*, std::span<const uint8_t> data, size_t size) override
    {
        writes.emplace_back(data.begin(), data.begin() + size);
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
        return DeviceError::notSupported("Not used by this test");
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>, size_t) override
    {
        return DeviceError::notSupported("Not used by this test");
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

class TestableH3Pro final : public EposH3ProHybrid {
public:
    explicit TestableH3Pro(H3ProMockHID& hid, uint16_t product_id = PRODUCT_ID_DONGLE)
        : hid_(hid)
    {
        setMatchedProductId(product_id);
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    H3ProMockHID& hid_;
};

// Frames captured from EPOS Gaming Suite, through the dongle and over the cable
const std::vector<uint8_t> DONGLE_CONNECTED_REQUEST { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x04, 0x00, 0x7a, 0x00, 0x01, 0x00, 0x09, 0x00 };
const std::vector<uint8_t> DONGLE_CONNECTED { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x35, 0x00, 0x01, 0x00, 0x09, 0x00, 0x01, 0x00 };
const std::vector<uint8_t> DONGLE_DISCONNECTED { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x20, 0x00, 0x01, 0x00, 0x09, 0x00, 0x00, 0x00 };
const std::vector<uint8_t> HEADSET_FIRMWARE_REQUEST { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0xf8, 0x00, 0x01, 0x00, 0x03, 0x00 };
const std::vector<uint8_t> HEADSET_CONNECTED_EVENT { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x10, 0x01, 0x00, 0x00, 0x08, 0x00, 0xf4, 0x00, 0x01, 0x00, 0xf0, 0x00, 0x01, 0x00, 0x01, 0x00 };
const std::vector<uint8_t> CABLE_BATTERY_REQUEST { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x04, 0x00, 0xa5, 0x00, 0x12, 0x00, 0x03, 0x00 };
const std::vector<uint8_t> CABLE_BATTERY_40 { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x5f, 0x00, 0x12, 0x00, 0x03, 0x00, 0x28, 0x00 };
const std::vector<uint8_t> CABLE_SIDETONE_AD { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0xab, 0x00, 0x0a, 0x00, 0x0c, 0x00, 0xad, 0x00 };
const std::vector<uint8_t> CABLE_SIDETONE_SET_OFF { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x06, 0x00, 0x38, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x00, 0x00 };
const std::vector<uint8_t> CABLE_SIDETONE_SET_MAX { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x06, 0x00, 0xef, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0xff, 0x00 };
const std::vector<uint8_t> CABLE_SIDETONE_SET_OK { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0x38, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x00, 0x00 };
const std::vector<uint8_t> CABLE_NOISE_FILTER_OFF { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x06, 0x00, 0xc3, 0x00, 0x0a, 0x00, 0x11, 0x00, 0x00, 0x00 };
const std::vector<uint8_t> CABLE_NOISE_FILTER_ON { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x06, 0x00, 0xd6, 0x00, 0x0a, 0x00, 0x11, 0x00, 0x01, 0x00 };
const std::vector<uint8_t> CABLE_NOISE_FILTER_OK { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x06, 0x00, 0xc3, 0x00, 0x0a, 0x00, 0x11, 0x00, 0x00, 0x00 };
const std::vector<uint8_t> MEDIA_KEY { 0x05, 0x01 };

auto* const UNUSED_HID = reinterpret_cast<hid_device*>(0x1);

// A reply from the headset behind the dongle, built like the captured ones
std::vector<uint8_t> headsetReply(EposH3ProHybrid::Command command, uint16_t value)
{
    auto frame = EposH3ProHybrid::buildRequest(EposH3ProHybrid::TARGET_HEADSET - 1, command, value);
    return { frame.begin(), frame.end() };
}

bool startsWith(const std::vector<uint8_t>& report, const std::vector<uint8_t>& frame)
{
    return report.size() == EposH3ProHybrid::REPORT_SIZE && std::equal(frame.begin(), frame.end(), report.begin())
        && std::all_of(report.begin() + frame.size(), report.end(), [](uint8_t byte) { return byte == 0; });
}

void testH3ProRegistered()
{
    DeviceRegistry::instance().initialize();
    for (uint16_t product_id : { 0x0303, 0x0101 }) {
        auto* device = DeviceRegistry::instance().getDevice(0x1395, product_id);
        H3PRO_ASSERT(device != nullptr, "dongle and cabled headset are registered");
        H3PRO_ASSERT(std::string(device->getDeviceName()) == "EPOS H3PRO Hybrid", "device name");
        auto detail = device->getCapabilityDetail(CAP_BATTERY_STATUS);
        H3PRO_ASSERT(detail.usagepage == 0xFFFF && detail.usageid == 0x0001 && detail.interface_id == 3,
            "uses the vendor collection");
        H3PRO_ASSERT(device->canRead(CAP_SIDETONE), "sidetone can be read back");
    }
}

void testH3ProBuildsCapturedFrames()
{
    using Device   = EposH3ProHybrid;
    auto as_vector = [](const auto& report) { return std::vector<uint8_t>(report.begin(), report.end()); };
    H3PRO_ASSERT(startsWith(as_vector(Device::buildRequest(Device::TARGET_DEVICE, Device::CMD_CONNECTED)), DONGLE_CONNECTED_REQUEST),
        "connection query to the dongle");
    H3PRO_ASSERT(startsWith(as_vector(Device::buildRequest(Device::TARGET_HEADSET, { 0x01, 0x03 })), HEADSET_FIRMWARE_REQUEST),
        "query to the headset behind the dongle");
    H3PRO_ASSERT(startsWith(as_vector(Device::buildRequest(Device::TARGET_DEVICE, Device::CMD_BATTERY)), CABLE_BATTERY_REQUEST),
        "battery query");
    H3PRO_ASSERT(startsWith(as_vector(Device::buildRequest(Device::TARGET_DEVICE, Device::CMD_SIDETONE_SET, 0xFF)), CABLE_SIDETONE_SET_MAX),
        "sidetone with a value");
}

void testH3ProParsesFrames()
{
    using Device = EposH3ProHybrid;

    auto battery = Device::parseFrame(CABLE_BATTERY_40);
    H3PRO_ASSERT(battery && battery->type == Device::TYPE_REQUEST && battery->target == 0, "reply header");
    H3PRO_ASSERT(battery->group == 0x12 && battery->command == 0x03, "reply command");
    H3PRO_ASSERT(battery->values == std::vector<uint16_t> { 0x28 }, "reply value");

    auto event = Device::parseFrame(HEADSET_CONNECTED_EVENT);
    H3PRO_ASSERT(event && event->type == 0x0110 && (event->values == std::vector<uint16_t> { 1, 1 }), "notification");

    auto corrupted = CABLE_BATTERY_40;
    corrupted.back() ^= 0x01;
    H3PRO_ASSERT(!Device::parseFrame(corrupted), "frames with a bad CRC are dropped");

    auto truncated = std::vector<uint8_t>(CABLE_BATTERY_40.begin(), CABLE_BATTERY_40.end() - 2);
    H3PRO_ASSERT(!Device::parseFrame(truncated), "incomplete frames are dropped");
    H3PRO_ASSERT(!Device::parseFrame(MEDIA_KEY), "other reports are ignored");
}

void testH3ProSidetoneLevels()
{
    using Device = EposH3ProHybrid;
    H3PRO_ASSERT(Device::sidetoneDeviceLevel(0) == 0, "0 is off");
    H3PRO_ASSERT(Device::sidetoneDeviceLevel(128) == 0xFF, "128 is the highest level");
    H3PRO_ASSERT(Device::sidetoneDeviceLevel(255) == 0xFF, "values above 128 are capped");
    H3PRO_ASSERT(Device::sidetoneLevel(0xFF) == 128, "highest level reads as 128");
    for (int level = 0; level <= 128; ++level) {
        H3PRO_ASSERT(Device::sidetoneLevel(Device::sidetoneDeviceLevel(static_cast<uint8_t>(level))) == level,
            "level " + std::to_string(level) + " reads back unchanged");
    }
}

void testH3ProBatteryThroughDongle()
{
    H3ProMockHID hid;
    TestableH3Pro device(hid);
    hid.reads.emplace_back(DONGLE_CONNECTED);
    hid.reads.emplace_back(MEDIA_KEY);
    hid.reads.emplace_back(HEADSET_CONNECTED_EVENT);
    auto other_target = EposH3ProHybrid::buildRequest(EposH3ProHybrid::TARGET_HEADSET, EposH3ProHybrid::CMD_BATTERY, 99);
    hid.reads.emplace_back(std::vector<uint8_t>(other_target.begin(), other_target.end()));
    hid.reads.emplace_back(headsetReply(EposH3ProHybrid::CMD_BATTERY, 0x3c));

    auto result = device.getBattery(UNUSED_HID);
    H3PRO_ASSERT(result.hasValue(), "battery is read");
    H3PRO_ASSERT(result->level_percent == 60, "value is the percentage");
    H3PRO_ASSERT(result->status == BATTERY_AVAILABLE, "headset is on");
    H3PRO_ASSERT(hid.writes.size() == 2, "connection and battery queries");
    H3PRO_ASSERT(startsWith(hid.writes[0], DONGLE_CONNECTED_REQUEST), "dongle is asked first");
    H3PRO_ASSERT(startsWith(hid.writes[1], { 0x01, 0xeb, 0xfe, 0x01, 0x00, 0x04, 0x00, 0x05, 0x00, 0x04, 0x00, 0xa5, 0x00, 0x12, 0x00, 0x03, 0x00 }),
        "battery query goes to the headset");
}

void testH3ProBatteryOverCable()
{
    H3ProMockHID hid;
    TestableH3Pro device(hid, EposH3ProHybrid::PRODUCT_ID_CABLE);
    hid.reads.emplace_back(CABLE_BATTERY_40);

    auto result = device.getBattery(UNUSED_HID);
    H3PRO_ASSERT(result.hasValue() && result->level_percent == 40, "battery is read");
    H3PRO_ASSERT(hid.writes.size() == 1 && startsWith(hid.writes[0], CABLE_BATTERY_REQUEST),
        "only the battery query, sent to the headset itself");
}

void testH3ProHeadsetOff()
{
    H3ProMockHID hid;
    TestableH3Pro device(hid);
    hid.reads.emplace_back(DONGLE_DISCONNECTED);

    auto result = device.getBattery(UNUSED_HID);
    H3PRO_ASSERT(result.hasError() && result.error().code == DeviceError::Code::DeviceOffline,
        "dongle reports the headset as disconnected");
    H3PRO_ASSERT(hid.writes.size() == 1, "nothing is sent to the headset");

    // Seen right after the headset switched itself off: the dongle still
    // reported it as connected, but returned the request unanswered
    H3ProMockHID returned_hid;
    TestableH3Pro returned(returned_hid);
    returned_hid.reads.emplace_back(DONGLE_CONNECTED);
    auto request = EposH3ProHybrid::buildRequest(EposH3ProHybrid::TARGET_DEVICE, EposH3ProHybrid::CMD_BATTERY);
    returned_hid.reads.emplace_back(std::vector<uint8_t>(request.begin(), request.end()));

    auto unanswered = returned.getBattery(UNUSED_HID);
    H3PRO_ASSERT(unanswered.hasError() && unanswered.error().code == DeviceError::Code::DeviceOffline,
        "a returned request means the headset is off");
}

void testH3ProUnexpectedReplies()
{
    H3ProMockHID hid;
    TestableH3Pro device(hid);
    hid.reads.emplace_back(DONGLE_CONNECTED);
    hid.reads.emplace_back(headsetReply(EposH3ProHybrid::CMD_BATTERY, 101));
    auto above_100 = device.getBattery(UNUSED_HID);
    H3PRO_ASSERT(above_100.hasError() && above_100.error().code == DeviceError::Code::ProtocolError,
        "values above 100% are rejected");

    hid.reads.emplace_back(DONGLE_CONNECTED);
    auto no_reply = device.getBattery(UNUSED_HID);
    H3PRO_ASSERT(no_reply.hasError() && no_reply.error().code == DeviceError::Code::Timeout, "missing reply times out");

    hid.reads.emplace_back(DONGLE_CONNECTED);
    hid.reads.emplace_back(headsetReply(EposH3ProHybrid::CMD_SIDETONE_SET, 1));
    auto failed = device.setSidetone(UNUSED_HID, 64);
    H3PRO_ASSERT(failed.hasError() && failed.error().code == DeviceError::Code::ProtocolError,
        "a failure status is reported");

    hid.reads.emplace_back(DeviceError::hidError("unplugged"));
    auto unplugged = device.getBattery(UNUSED_HID);
    H3PRO_ASSERT(unplugged.hasError() && unplugged.error().code == DeviceError::Code::HIDError,
        "read errors are returned, not turned into a timeout");
}

void testH3ProSidetone()
{
    H3ProMockHID hid;
    TestableH3Pro device(hid, EposH3ProHybrid::PRODUCT_ID_CABLE);
    hid.reads.emplace_back(CABLE_SIDETONE_SET_OK);
    auto set = device.setSidetone(UNUSED_HID, 128);
    H3PRO_ASSERT(set.hasValue() && set->current_level == 128 && set->device_level == 0xFF, "128 is the maximum");
    H3PRO_ASSERT(startsWith(hid.writes.back(), CABLE_SIDETONE_SET_MAX), "same frame as Gaming Suite");

    hid.reads.emplace_back(CABLE_SIDETONE_SET_OK);
    auto off = device.setSidetone(UNUSED_HID, 0);
    H3PRO_ASSERT(off.hasValue() && off->device_level == 0, "0 is off");
    H3PRO_ASSERT(startsWith(hid.writes.back(), CABLE_SIDETONE_SET_OFF), "same frame as Gaming Suite");

    hid.reads.emplace_back(CABLE_SIDETONE_AD);
    auto read = device.getSidetone(UNUSED_HID);
    H3PRO_ASSERT(read.hasValue() && read->device_level == 0xAD && read->current_level == 87, "level is read back");
}

void testH3ProNoiseFilter()
{
    H3ProMockHID hid;
    TestableH3Pro device(hid, EposH3ProHybrid::PRODUCT_ID_CABLE);
    hid.reads.emplace_back(CABLE_NOISE_FILTER_OK);
    auto off = device.setNoiseFilter(UNUSED_HID, 0);
    H3PRO_ASSERT(off.hasValue() && off->level == 0, "switched off");
    H3PRO_ASSERT(startsWith(hid.writes.back(), CABLE_NOISE_FILTER_OFF), "same frame as Gaming Suite");

    hid.reads.emplace_back(CABLE_NOISE_FILTER_OK);
    auto on = device.setNoiseFilter(UNUSED_HID, 1);
    H3PRO_ASSERT(on.hasValue() && on->level == 1, "switched on");
    H3PRO_ASSERT(startsWith(hid.writes.back(), CABLE_NOISE_FILTER_ON), "same frame as Gaming Suite");

    const auto sent = hid.writes.size();
    auto high       = device.setNoiseFilter(UNUSED_HID, 2);
    H3PRO_ASSERT(high.hasError() && high.error().code == DeviceError::Code::InvalidParameter, "only on and off exist");
    H3PRO_ASSERT(hid.writes.size() == sent, "nothing is sent for an invalid level");
}

void runAllEposH3ProHybridTests()
{
    std::cout << "\n=== EPOS H3PRO Hybrid Tests ===" << std::endl;
    testH3ProRegistered();
    testH3ProBuildsCapturedFrames();
    testH3ProParsesFrames();
    testH3ProSidetoneLevels();
    testH3ProBatteryThroughDongle();
    testH3ProBatteryOverCable();
    testH3ProHeadsetOff();
    testH3ProUnexpectedReplies();
    testH3ProSidetone();
    testH3ProNoiseFilter();
    std::cout << "  EPOS H3PRO Hybrid tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

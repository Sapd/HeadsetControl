#include "device_registry.hpp"
#include "devices/sennheiser_gsp370.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class GSP370TestFailure : public std::runtime_error {
public:
    explicit GSP370TestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define GSP370_ASSERT(condition, message)                                           \
    do {                                                                            \
        if (!(condition))                                                           \
            throw GSP370TestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

// Queued chunks of serial input; an empty queue reads as a timeout, like a silent dongle
class GSP370MockSerial final : public SerialInterface {
public:
    static constexpr int HANDLE = 7;

    std::deque<std::vector<uint8_t>> reads;
    std::vector<std::vector<uint8_t>> writes;
    std::optional<DeviceError> open_error;
    std::optional<DeviceError> read_error;
    uint16_t opened_vid  = 0;
    uint16_t opened_pid  = 0;
    uint32_t opened_baud = 0;
    int opened           = 0;
    int closed           = 0;

    Result<int> open(uint16_t vid, uint16_t pid, uint32_t baud_rate, std::chrono::milliseconds) override
    {
        if (open_error)
            return *open_error;
        opened_vid  = vid;
        opened_pid  = pid;
        opened_baud = baud_rate;
        ++opened;
        return HANDLE;
    }

    void close(int handle) override
    {
        if (handle == HANDLE)
            ++closed;
    }

    Result<void> write(int, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        return {};
    }

    Result<size_t> readTimeout(int, std::span<uint8_t> data, int) override
    {
        if (read_error)
            return *read_error;
        if (reads.empty())
            return DeviceError::timeout("Simulated timeout");

        auto chunk = std::move(reads.front());
        reads.pop_front();
        const size_t size = std::min(chunk.size(), data.size());
        std::copy_n(chunk.begin(), size, data.begin());
        return size;
    }
};

class TestableGSP370 final : public SennheiserGSP370 {
public:
    explicit TestableGSP370(GSP370MockSerial& serial)
        : serial_(serial)
    {
    }

protected:
    SerialInterface& getSerialInterface() const override { return serial_; }

private:
    GSP370MockSerial& serial_;
};

// Replies captured from a real dongle
const std::vector<uint8_t> BATTERY_80 { 0x04, 0x0E, 0x05, 0x01, 0x9C, 0xFC, 0x00, 0x50 };
const std::vector<uint8_t> BATTERY_OFF { 0x04, 0x0E, 0x04, 0x01, 0x9C, 0xFC, 0x0C };
const std::vector<uint8_t> SIDETONE_ON_ACK { 0x04, 0x0E, 0x04, 0x01, 0xA0, 0xFC, 0x00 };
const std::vector<uint8_t> SIDETONE_OFF_ACK { 0x04, 0x0E, 0x04, 0x01, 0xA1, 0xFC, 0x00 };
const std::vector<uint8_t> SIDETONE_SET_ACK { 0x04, 0x0E, 0x04, 0x01, 0xA2, 0xFC, 0x00 };
const std::vector<uint8_t> SIDETONE_ON_OFF { 0x04, 0x0E, 0x04, 0x01, 0xA0, 0xFC, 0x0C };
const std::vector<uint8_t> VENDOR_EVENT { 0x04, 0xE9, 0x00 };

auto* const UNUSED_HID = reinterpret_cast<hid_device*>(0x1);

void testGSP370Registered()
{
    DeviceRegistry::instance().initialize();
    auto* device = DeviceRegistry::instance().getDevice(0x1395, 0x009A);
    GSP370_ASSERT(device != nullptr, "dongle is registered");
    GSP370_ASSERT(device->usesSerialPort(), "serial port is added to the udev rules");
    GSP370_ASSERT(device->getCapabilityDetail(CAP_BATTERY_STATUS).interface_id == 3, "uses the HID interface");
}

void testGSP370BuildsCapturedCommands()
{
    using Device = SennheiserGSP370;
    GSP370_ASSERT((Device::buildCommand(Device::OPCODE_BATTERY) == std::vector<uint8_t> { 0x01, 0x9C, 0xFC, 0x00 }),
        "battery query");
    GSP370_ASSERT((Device::buildCommand(Device::OPCODE_SIDETONE_SET, std::array<uint8_t, 1> { 0x09 })
                      == std::vector<uint8_t> { 0x01, 0xA2, 0xFC, 0x01, 0x09 }),
        "sidetone level");
}

void testGSP370ParsesReplies()
{
    using Device = SennheiserGSP370;

    std::vector<uint8_t> battery { 0x04, 0x0E, 0x05, 0x01, 0x9C, 0xFC, 0x00, 0x51 };
    auto reply = Device::parseReply(battery, Device::OPCODE_BATTERY);
    GSP370_ASSERT(reply && *reply == (std::vector<uint8_t> { 0x00, 0x51 }), "status and value");

    std::vector<uint8_t> acknowledged { 0x04, 0x0E, 0x04, 0x01, 0xA2, 0xFC, 0x00 };
    reply = Device::parseReply(acknowledged, Device::OPCODE_SIDETONE_SET);
    GSP370_ASSERT(reply && *reply == std::vector<uint8_t> { 0x00 }, "status without a value");

    std::vector<uint8_t> headset_off { 0x04, 0x0E, 0x04, 0x01, 0x9C, 0xFC, 0x0C };
    reply = Device::parseReply(headset_off, Device::OPCODE_BATTERY);
    GSP370_ASSERT(reply && *reply == std::vector<uint8_t> { Device::STATUS_HEADSET_OFF }, "failure status");

    std::vector<uint8_t> after_event { 0x04, 0xE9, 0x00, 0x04, 0x0E, 0x05, 0x01, 0x9C, 0xFC, 0x00, 0x50 };
    reply = Device::parseReply(after_event, Device::OPCODE_BATTERY);
    GSP370_ASSERT(reply && *reply == (std::vector<uint8_t> { 0x00, 0x50 }), "events before the reply are skipped");

    std::vector<uint8_t> partial { 0x04, 0x0E, 0x05, 0x01, 0x9C, 0xFC, 0x00 };
    GSP370_ASSERT(!Device::parseReply(partial, Device::OPCODE_BATTERY), "incomplete reply");

    GSP370_ASSERT(!Device::parseReply(battery, Device::OPCODE_SIDETONE_SET), "reply to another command");
}

void testGSP370SidetoneLevels()
{
    using Device = SennheiserGSP370;
    GSP370_ASSERT(Device::sidetoneDeviceLevel(0) == 0x00, "0 is off");
    GSP370_ASSERT(Device::sidetoneDeviceLevel(1) == 0x06, "lowest audible level");
    GSP370_ASSERT(Device::sidetoneDeviceLevel(128) == 0x10, "128 is the highest level");
    GSP370_ASSERT(Device::sidetoneDeviceLevel(255) == 0x10, "values above 128 are capped");
}

void testGSP370ReadsBattery()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back(BATTERY_80);

    auto result = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(result.hasValue(), "battery is read");
    GSP370_ASSERT(result->level_percent == 80, "value is the percentage");
    GSP370_ASSERT(result->status == BATTERY_AVAILABLE, "headset is on");
    GSP370_ASSERT(result->raw_data && *result->raw_data == std::vector<uint8_t> { 0x50 }, "raw data is the value");
    GSP370_ASSERT((serial.writes == std::vector<std::vector<uint8_t>> { { 0x01, 0x9C, 0xFC, 0x00 } }), "only the battery query is sent");
    GSP370_ASSERT(serial.opened_vid == 0x1395 && serial.opened_pid == 0x009A, "opens the dongle's port");
    GSP370_ASSERT(serial.opened_baud == 921600, "at the baud rate EPOS Gaming Suite uses");
    GSP370_ASSERT(serial.opened == 1 && serial.closed == 1, "port is closed again");
}

void testGSP370BatteryReplyInPieces()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back({ 0x04, 0xE9, 0x00, 0x04, 0x0E });
    serial.reads.push_back({ 0x05, 0x01, 0x9C, 0xFC, 0x00, 0x51 });

    auto result = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(result.hasValue() && result->level_percent == 81, "event is skipped and the split reply joined");
}

void testGSP370BatteryHeadsetOff()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back(BATTERY_OFF);

    auto result = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(result.hasError() && result.error().code == DeviceError::Code::DeviceOffline,
        "status 0x0C means the headset is off");
    GSP370_ASSERT(serial.closed == 1, "port is closed after an error");
}

void testGSP370BatteryUnexpectedReplies()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back({ 0x04, 0x0E, 0x05, 0x01, 0x9C, 0xFC, 0x00, 0x65 });
    auto above_100 = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(above_100.hasError() && above_100.error().code == DeviceError::Code::ProtocolError,
        "values above 100% are rejected");

    serial.reads.push_back({ 0x04, 0x0E, 0x04, 0x01, 0x9C, 0xFC, 0x01 });
    auto failed = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(failed.hasError() && failed.error().code == DeviceError::Code::ProtocolError,
        "unknown failure statuses are protocol errors");
}

void testGSP370NoReply()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back(VENDOR_EVENT);

    auto result = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(result.hasError() && result.error().code == DeviceError::Code::Timeout, "missing reply times out");
    GSP370_ASSERT(serial.closed == 1, "port is closed after a timeout");
}

void testGSP370SerialErrorsPropagate()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.open_error = DeviceError::deviceOffline("Serial port not found");
    auto not_found    = device.getBattery(UNUSED_HID);
    GSP370_ASSERT(not_found.hasError() && not_found.error().code == DeviceError::Code::DeviceOffline,
        "open error is returned");
    GSP370_ASSERT(serial.writes.empty() && serial.closed == 0, "nothing is sent or closed without a port");

    serial.open_error = std::nullopt;
    serial.read_error = DeviceError::hidError("unplugged");
    auto unplugged    = device.setSidetone(UNUSED_HID, 40);
    GSP370_ASSERT(unplugged.hasError() && unplugged.error().code == DeviceError::Code::HIDError,
        "read error is returned, not turned into a timeout");
    GSP370_ASSERT(serial.closed == 1, "port is closed after a read error");
}

void testGSP370SetsSidetone()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back(SIDETONE_ON_ACK);
    serial.reads.push_back(SIDETONE_SET_ACK);

    auto result = device.setSidetone(UNUSED_HID, 40);
    GSP370_ASSERT(result.hasValue(), "sidetone is set");
    GSP370_ASSERT(result->current_level == 40 && result->device_level == 0x09, "40 is device level 0x09");
    GSP370_ASSERT((serial.writes == std::vector<std::vector<uint8_t>> { { 0x01, 0xA0, 0xFC, 0x00 }, { 0x01, 0xA2, 0xFC, 0x01, 0x09 } }),
        "0xA0 precedes a non-zero level");
    GSP370_ASSERT(serial.closed == 1, "port is closed again");
}

void testGSP370TurnsSidetoneOff()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back(SIDETONE_OFF_ACK);
    serial.reads.push_back(SIDETONE_SET_ACK);

    auto result = device.setSidetone(UNUSED_HID, 0);
    GSP370_ASSERT(result.hasValue() && result->device_level == 0x00, "0 is off");
    GSP370_ASSERT((serial.writes == std::vector<std::vector<uint8_t>> { { 0x01, 0xA1, 0xFC, 0x00 }, { 0x01, 0xA2, 0xFC, 0x01, 0x00 } }),
        "0xA1 precedes level 0");
}

void testGSP370SidetoneHeadsetOff()
{
    GSP370MockSerial serial;
    TestableGSP370 device(serial);
    serial.reads.push_back(SIDETONE_ON_OFF);

    auto result = device.setSidetone(UNUSED_HID, 128);
    GSP370_ASSERT(result.hasError() && result.error().code == DeviceError::Code::DeviceOffline,
        "status 0x0C means the headset is off");
    GSP370_ASSERT(serial.writes.size() == 1, "the level is not sent after a failure");
    GSP370_ASSERT(serial.closed == 1, "port is closed after an error");
}

void runAllSennheiserGSP370Tests()
{
    std::cout << "\n=== Sennheiser GSP 370 Tests ===" << std::endl;
    testGSP370Registered();
    testGSP370BuildsCapturedCommands();
    testGSP370ParsesReplies();
    testGSP370SidetoneLevels();
    testGSP370ReadsBattery();
    testGSP370BatteryReplyInPieces();
    testGSP370BatteryHeadsetOff();
    testGSP370BatteryUnexpectedReplies();
    testGSP370NoReply();
    testGSP370SerialErrorsPropagate();
    testGSP370SetsSidetone();
    testGSP370TurnsSidetoneOff();
    testGSP370SidetoneHeadsetOff();
    std::cout << "  Sennheiser GSP 370 tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

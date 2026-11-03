#include "device_registry.hpp"
#include "devices/sennheiser_gsp370.hpp"

#include <array>
#include <cstdint>
#include <iostream>
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

void runAllSennheiserGSP370Tests()
{
    std::cout << "\n=== Sennheiser GSP 370 Tests ===" << std::endl;
    testGSP370Registered();
    testGSP370BuildsCapturedCommands();
    testGSP370ParsesReplies();
    testGSP370SidetoneLevels();
    std::cout << "  Sennheiser GSP 370 tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

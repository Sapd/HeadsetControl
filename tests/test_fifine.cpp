#include "device_registry.hpp"
#include "devices/fifine_ht6.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class FifineTestFailure : public std::runtime_error {
public:
    explicit FifineTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define FIFINE_ASSERT(condition, message)                                           \
    do {                                                                            \
        if (!(condition))                                                           \
            throw FifineTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

using Bytes = std::vector<uint8_t>;

auto* const FIFINE_TEST_HANDLE = reinterpret_cast<hid_device*>(0x1);

Bytes fifineReport(Bytes head)
{
    head.resize(FifineHT6::REPORT_LENGTH, 0);
    return head;
}

/**
 * Simulated HT6 dongle. It answers the state and LED queries with the packets
 * captured from the real device and records everything written to it.
 */
class FifineMockHID final : public HIDInterface {
public:
    Bytes state_reply = fifineReport({ 0x55, 0x65, 0x50, 0x02, 0x02, 0x00, 0x00, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x04, 0x02, 0x01, 0x00, 0x01 });
    Bytes led_reply   = fifineReport({ 0x55, 0x06, 0x03, 0xff, 0x69, 0xb4, 0x0a, 0x00 });
    bool answer_state = true;
    bool answer_led   = true;

    std::vector<Bytes> writes;
    std::deque<Bytes> pending;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        Bytes packet(data.begin(), data.end());
        writes.push_back(packet);

        if (packet.size() >= 2 && packet[0] == FifineHT6::REPORT_ID) {
            if (packet[1] == FifineHT6::CMD_STATE && answer_state) {
                pending.push_back(state_reply);
            } else if (packet[1] == FifineHT6::CMD_LED_STATE && answer_led) {
                pending.push_back(led_reply);
            }
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

class TestableFifine final : public FifineHT6 {
public:
    explicit TestableFifine(FifineMockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    FifineMockHID& hid_;
};

void testFifineRegistered()
{
    auto* device = DeviceRegistry::instance().getDevice(0x3142, 0x00c3);
    FIFINE_ASSERT(device != nullptr, "FIFINE HT6 should be registered");
    FIFINE_ASSERT(device->getDeviceName() == "FIFINE HT6 Wireless", "device name");

    constexpr int expected = B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_LIGHTS) | B(CAP_LIGHT_COLOR);
    FIFINE_ASSERT(device->getCapabilities() == expected, "capabilities");

    auto detail = device->getCapabilityDetail(CAP_BATTERY_STATUS);
    FIFINE_ASSERT(detail.usagepage == 0xff90 && detail.usageid == 0x01,
        "should select the vendor collection");

    FIFINE_ASSERT(DeviceRegistry::instance().getDevice(0x3142, 0x00c2) == nullptr, "wrong product id");
    FIFINE_ASSERT(DeviceRegistry::instance().getDevice(0x3141, 0x00c3) == nullptr, "wrong vendor id");
}

void testFifineBattery()
{
    FifineMockHID hid;
    TestableFifine device(hid);

    auto battery = device.getBattery(FIFINE_TEST_HANDLE);
    FIFINE_ASSERT(battery.hasValue(), "battery read should succeed");
    FIFINE_ASSERT(battery->level_percent == 80, "50 hex is 80%");
    FIFINE_ASSERT(battery->status == BATTERY_AVAILABLE, "status available");
    FIFINE_ASSERT(battery->raw_data.has_value(), "raw packet is exposed");

    FIFINE_ASSERT(hid.writes.size() == 1, "one request");
    const Bytes& request = hid.writes.front();
    FIFINE_ASSERT(request.size() == FifineHT6::REPORT_LENGTH, "64-byte request");
    FIFINE_ASSERT(request[0] == 0x55 && request[1] == 0x65 && request[2] == 0x01 && request[3] == 0x00,
        "request is 55 65 01 00 ...");
}

void testFifineBatteryCharging()
{
    FifineMockHID hid;
    hid.state_reply[3] = 0x03;
    TestableFifine device(hid);

    auto battery = device.getBattery(FIFINE_TEST_HANDLE);
    FIFINE_ASSERT(battery.hasValue() && battery->status == BATTERY_CHARGING, "status 3 is charging");
}

void testFifineHeadsetOff()
{
    FifineMockHID hid;
    hid.state_reply[1] = FifineHT6::STATE_HEADPHONES_OFF;
    TestableFifine device(hid);

    auto battery = device.getBattery(FIFINE_TEST_HANDLE);
    FIFINE_ASSERT(battery.hasError(), "headset off is an error, not 0%");
    FIFINE_ASSERT(battery.error().code == DeviceError::Code::DeviceOffline, "reported as offline, not as a timeout");
}

void testFifineNoAnswer()
{
    FifineMockHID hid;
    hid.answer_state = false;
    TestableFifine device(hid);

    FIFINE_ASSERT(!device.getBattery(FIFINE_TEST_HANDLE).hasValue(), "silence is an error");
}

void testFifineInactiveTime()
{
    FifineMockHID hid;
    TestableFifine device(hid);

    auto enabled = device.setInactiveTime(FIFINE_TEST_HANDLE, 30);
    FIFINE_ASSERT(enabled.hasValue() && enabled->minutes == 30, "30 minutes accepted");
    FIFINE_ASSERT(enabled->max_minutes == 90, "CLI range");

    const Bytes& packet = hid.writes.back();
    FIFINE_ASSERT(packet[0] == 0x55 && packet[1] == 0x23 && packet[2] == 0x01 && packet[3] == 30,
        "55 23 01 1e enables 30 minutes");

    auto disabled = device.setInactiveTime(FIFINE_TEST_HANDLE, 0);
    FIFINE_ASSERT(disabled.hasValue() && disabled->minutes == 0, "0 disables");
    FIFINE_ASSERT(hid.writes.back()[3] == 0xff, "0xff disables the timer");
}

void testFifineSetLightColorKeepsMode()
{
    FifineMockHID hid;
    // Real device in rhythm mode with pink: 55 06 03 ff 69 b4 0a 00
    TestableFifine device(hid);

    auto result = device.setLightColor(FIFINE_TEST_HANDLE, { .r = 0x00, .g = 0xff, .b = 0x00 });
    FIFINE_ASSERT(result.hasValue(), "colour write succeeds");
    FIFINE_ASSERT(result->color.r == 0x00 && result->color.g == 0xff && result->color.b == 0x00,
        "returned colour");

    // LED query, enable, stop, mode, 32 table packets, save, start.
    FIFINE_ASSERT(hid.writes.size() == 1 + 2 + 1 + 32 + 2, "query + engine sequence");

    const Bytes& query = hid.writes[0];
    FIFINE_ASSERT(query[1] == 0x06, "reads the current mode first");

    const Bytes& enable = hid.writes[1];
    FIFINE_ASSERT(enable[1] == 0x0c && enable[2] == 0x01, "RGB is enabled");

    const Bytes& stop = hid.writes[2];
    FIFINE_ASSERT(stop[1] == 0x02, "LED engine is stopped");

    const Bytes& mode = hid.writes[3];
    FIFINE_ASSERT(mode[1] == 0x03 && mode[2] == 0x03, "rhythm mode is kept");
    FIFINE_ASSERT(mode[3] == 0x80, "128-entry table");

    const Bytes& first_table = hid.writes[4];
    FIFINE_ASSERT(first_table[1] == 0x04 && first_table[2] == 0x00, "table starts at index 0");
    FIFINE_ASSERT(first_table[4] == 0x00 && first_table[5] == 0xff && first_table[6] == 0x00,
        "table starts at the base colour");

    const Bytes& save = hid.writes[36];
    FIFINE_ASSERT(save[1] == 0x05, "saved to flash");

    const Bytes& start = hid.writes[37];
    FIFINE_ASSERT(start[1] == 0x01, "LED engine is started");
}

void testFifineLightsOff()
{
    FifineMockHID hid;
    TestableFifine device(hid);

    auto result = device.setLights(FIFINE_TEST_HANDLE, false);
    FIFINE_ASSERT(result.hasValue() && !result->enabled, "lights off");
    FIFINE_ASSERT(result->mode.has_value() && *result->mode == "off", "reports off");

    FIFINE_ASSERT(hid.writes.size() == 1, "one command, no colour writes");
    const Bytes& off = hid.writes.front();
    FIFINE_ASSERT(off[0] == 0x55 && off[1] == 0x0c && off[2] == 0x00,
        "55 0c 00 disables the RGB engine");
}

void testFifineLightsOnKeepsMode()
{
    FifineMockHID hid;
    TestableFifine device(hid);

    auto result = device.setLights(FIFINE_TEST_HANDLE, true);
    FIFINE_ASSERT(result.hasValue() && result->enabled, "lights on");
    FIFINE_ASSERT(result->mode.has_value() && *result->mode == "rhythm", "mode is kept");

    FIFINE_ASSERT(hid.writes.size() == 2, "query + enable, no colour rewrite");
    const Bytes& enable = hid.writes[1];
    FIFINE_ASSERT(enable[1] == 0x0c && enable[2] == 0x01, "55 0c 01 enables the RGB engine");
}

void testFifineLightsOnAfterOffUsesWhite()
{
    FifineMockHID hid;
    // Off: mode 4, retained colour is black.
    hid.led_reply = fifineReport({ 0x55, 0x06, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x00 });
    TestableFifine device(hid);

    auto result = device.setLights(FIFINE_TEST_HANDLE, true);
    FIFINE_ASSERT(result.hasValue() && result->enabled, "lights on");

    // Query, enable, stop, mode, 2 table packets for 8 entries, save, start.
    FIFINE_ASSERT(hid.writes.size() == 8, "full engine sequence for 8 colours");

    const Bytes& mode = hid.writes[3];
    FIFINE_ASSERT(mode[2] == FifineHT6::LED_MODE_STATIC && mode[3] == FifineHT6::LED_COUNT_STATIC,
        "static mode with 8 colours");

    const Bytes& table = hid.writes[4];
    FIFINE_ASSERT(table[4] == 0xff && table[5] == 0xff && table[6] == 0xff,
        "black falls back to white instead of staying dark");
}

void runAllFifineTests()
{
    std::cout << "\n=== FIFINE Tests ===" << std::endl;
    testFifineRegistered();
    testFifineBattery();
    testFifineBatteryCharging();
    testFifineHeadsetOff();
    testFifineNoAnswer();
    testFifineInactiveTime();
    testFifineSetLightColorKeepsMode();
    testFifineLightsOff();
    testFifineLightsOnKeepsMode();
    testFifineLightsOnAfterOffUsesWhite();
    std::cout << "  FIFINE tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

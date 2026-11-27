#include "device.hpp"
#include "device_registry.hpp"
#include "devices/moondrop_gm01_pro.hpp"
#include "headsetcontrol_c.h"
#include "hid_interface.hpp"
#include "result_types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {
class MdrpGM01ProTestFailure : public std::runtime_error {

public:
    explicit MdrpGM01ProTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define MDRP_GM01_PRO_ASSERT(condition, message)                                         \
    do {                                                                                 \
        if (!(condition))                                                                \
            throw MdrpGM01ProTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

using Message = std::vector<uint8_t>;

auto* const MDRP_GM01_PRO_TEST_HANDLE = reinterpret_cast<hid_device*>(0x1);

class MdrpGM01ProMockHID final : public HIDInterface {
public:
    bool answer_battery        = true;
    bool answer_link           = true;
    bool answer_with_unrelated = false;
    bool wrong_data_size       = false;
    bool short_reply           = false;
    /*
     * CMD_LINK is called before almost every known command
     * needs its own flags in order to properly test
     * wrong frame for other commands
     */
    bool wrong_link_data_size = false;
    bool short_link_reply     = false;
    Message battery_reply     = { 0x4B, 0x80, 0x27, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x32 };
    Message link_reply        = { 0x4B, 0x80, 0x31, 0x01, 0x01 };
    Message led_reply         = { 0x4B, 0x80, 0x28, 0x01, 0x01 };
    std::vector<Message> writes;
    std::deque<Message> pending;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        if (data.size() >= 2 && data[0] == MoondropGM01Pro::REPORT_ID) {
            if (data[1] == MoondropGM01Pro::MSG_TYPE_READ) {
                if (data[2] == MoondropGM01Pro::CMD_BATTERY && answer_battery) {
                    Message final_reply = battery_reply;
                    if (wrong_data_size) {
                        final_reply = resizeReplyToWrongDataSize(final_reply, INVALID_BATTERY_DATA_SIZE_MSG_SIZE, INVALID_BATTERY_DATA_SIZE);
                    }
                    pending.push_back(resizeReplyToMsgSize(final_reply, short_reply));
                } else if (data[2] == MoondropGM01Pro::CMD_LINK && answer_link) {
                    Message final_reply = link_reply;
                    if (wrong_link_data_size) {
                        final_reply = resizeReplyToWrongDataSize(final_reply, INVALID_LINK_DATA_SIZE_MSG_SIZE, INVALID_LINK_DATA_SIZE);
                    }
                    pending.push_back(resizeReplyToMsgSize(final_reply, short_link_reply));
                }
            }
            if (answer_with_unrelated) {
                /*
                 * Simulate device responding with unrelated packet before target response
                 * link_reply cannot be used as dummy filler reply
                 * because it is called before almost every request
                 */
                if (data[1] == MoondropGM01Pro::CMD_LED) {
                    pending.push_back(resizeReplyToMsgSize(battery_reply));
                } else {
                    pending.push_back(resizeReplyToMsgSize(led_reply));
                }
            }
        }

        return {};
    }

    Result<void> write(hid_device* device_handle, std::span<const uint8_t> data, size_t size) override
    {
        return write(device_handle, data.first(size));
    }

    Result<size_t> readTimeout(hid_device*, std::span<uint8_t> data, int) override
    {
        if (pending.empty())
            return DeviceError::timeout("Simulated silence");
        const Message next = pending.front();
        pending.pop_front();
        const size_t count = std::min(next.size(), data.size());
        std::fill(data.begin(), data.end(), 0);
        std::copy_n(next.begin(), count, data.begin());

        return count;
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>) override
    {
        return DeviceError::notSupported("unused");
    }
    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>, size_t) override
    {
        return DeviceError::notSupported("unused");
    }
    Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("unused");
    }
    Result<size_t> getInputReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("unused");
    }

private:
    const uint8_t INVALID_BATTERY_DATA_SIZE = 1;
    const uint8_t INVALID_LINK_DATA_SIZE    = 2;

    const size_t INVALID_BATTERY_DATA_SIZE_MSG_SIZE = 5;
    const size_t INVALID_LINK_DATA_SIZE_MSG_SIZE    = 6;

    Message resizeReplyToMsgSize(Message reply, bool short_reply = false)
    {
        if (!short_reply) {
            reply.resize(MoondropGM01Pro::MSG_SIZE, 0);
        }

        return reply;
    }

    Message resizeReplyToWrongDataSize(Message reply, size_t new_msg_size, uint8_t new_data_size)
    {
        reply[3] = new_data_size;
        reply.resize(new_msg_size, 0);
        return reply;
    }
};

class TestableMdrpGM01Pro final : public MoondropGM01Pro {
public:
    explicit TestableMdrpGM01Pro(MdrpGM01ProMockHID& hid)
        : hid_(hid)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    MdrpGM01ProMockHID& hid_;
};

void testMdrpGM01ProRegistered()
{
    auto* device = DeviceRegistry::instance().getDevice(0x35D8, 0x0133);
    MDRP_GM01_PRO_ASSERT(device != nullptr, "Moondrop GM01 Pro should be registered");
    MDRP_GM01_PRO_ASSERT(device->getDeviceName() == "Moondrop GM01 Pro", "device name");

    constexpr int expected = B(CAP_BATTERY_STATUS);
    MDRP_GM01_PRO_ASSERT(device->getCapabilities() == expected, "capabilities");

    auto detail = device->getCapabilityDetail(CAP_BATTERY_STATUS);
    MDRP_GM01_PRO_ASSERT(detail.usagepage == 0x000C && detail.usageid == 0x0001 && detail.interface_id == 3,
        "should select the consumer collection");

    MDRP_GM01_PRO_ASSERT(DeviceRegistry::instance().getDevice(0x35D8, 0x0123) == nullptr, "wrong product id");
    MDRP_GM01_PRO_ASSERT(DeviceRegistry::instance().getDevice(0x35D7, 0x0133) == nullptr, "wrong vendor id");
}

void testMdrpGM01ProBattery()
{
    MdrpGM01ProMockHID hid;
    TestableMdrpGM01Pro device(hid);

    auto battery = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);

    MDRP_GM01_PRO_ASSERT(battery.hasValue(), "read success");
    MDRP_GM01_PRO_ASSERT(battery->level_percent == 50, "0x32 is 50%");
    MDRP_GM01_PRO_ASSERT(battery->status == BATTERY_AVAILABLE, "status available");
    MDRP_GM01_PRO_ASSERT(battery->raw_data.has_value(), "raw packet is exposed");

    MDRP_GM01_PRO_ASSERT(hid.writes.size() == 2, "two requests (CMD_LINK and CMD_BATTERY)");
    const Message& link_request = hid.writes[0];
    MDRP_GM01_PRO_ASSERT(link_request.size() == MoondropGM01Pro::MSG_SIZE, "64-byte link request");
    MDRP_GM01_PRO_ASSERT(link_request[0] == 0x4B && link_request[1] == 0x80 && link_request[2] == 0x31 && link_request[3] == 0x00,
        "request is 4B 80 31 00 (LINK CHECK)...");
    const Message& battery_request = hid.writes[1];
    MDRP_GM01_PRO_ASSERT(battery_request.size() == MoondropGM01Pro::MSG_SIZE, "64-byte battery status request");
    MDRP_GM01_PRO_ASSERT(battery_request[0] == 0x4B && battery_request[1] == 0x80 && battery_request[2] == 0x27 && battery_request[3] == 0x00,
        "request is 4B 80 27 00 (BATTERY STATUS CHECK)...");
}

void testMdrpGM01ProHeadsetOff()
{
    MdrpGM01ProMockHID hid;
    hid.link_reply[4] = 0x00;
    TestableMdrpGM01Pro device(hid);

    auto battery = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(!battery, "headset is not connected is an error, not 0%");
    MDRP_GM01_PRO_ASSERT(battery.error().code == DeviceError::Code::DeviceOffline,
        "reported as offline, not as timeout");
}

void testMdrpGM01ProNoAnswer()
{
    MdrpGM01ProMockHID hid;
    hid.answer_battery = false;
    TestableMdrpGM01Pro device(hid);

    auto battery_response = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(battery_response.hasError(), "(battery no answer) silence is an error");
    MDRP_GM01_PRO_ASSERT(battery_response.error().code == DeviceError::Code::Timeout, "(battery no answer) is a timeout error");

    hid.answer_battery = true;
    hid.answer_link    = false;
    auto link_response = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(link_response.hasError(), "(link no answer) silence is an error");
    MDRP_GM01_PRO_ASSERT(link_response.error().code == DeviceError::Code::Timeout, "(link no answer) is a timeout error");
}

void testMdrpGM01ProUnrelatedResponse()
{
    MdrpGM01ProMockHID hid;
    TestableMdrpGM01Pro device(hid);

    hid.answer_with_unrelated = true;
    auto response_raw         = *device.getBattery(MDRP_GM01_PRO_TEST_HANDLE).value().raw_data;
    uint8_t response_cmd      = response_raw[2];

    MDRP_GM01_PRO_ASSERT(response_cmd == 0x27, "unrelated packet ignored");
}

void testMdrpGM01ProWrongDataSize()
{
    MdrpGM01ProMockHID hid;
    TestableMdrpGM01Pro device(hid);

    hid.wrong_data_size          = true;
    auto wrong_data_size_battery = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(wrong_data_size_battery.hasError(), "(battery wrong size) is an error");
    MDRP_GM01_PRO_ASSERT(wrong_data_size_battery.error().code == DeviceError::Code::ProtocolError,
        "(battery wrong size) error is a ProtocolError");

    hid.wrong_data_size       = false;
    hid.wrong_link_data_size  = true;
    auto wrong_data_size_link = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(wrong_data_size_link.hasError(), "(link wrong size) is an error");
    MDRP_GM01_PRO_ASSERT(wrong_data_size_link.error().code == DeviceError::Code::ProtocolError,
        "(link wrong size) error is a ProtocolError");
}

void testMdrpGM01ProShortRead()
{
    MdrpGM01ProMockHID hid;
    TestableMdrpGM01Pro device(hid);

    hid.short_reply          = true;
    auto short_reply_battery = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(short_reply_battery.hasError(), "(battery short reply) is an error");
    MDRP_GM01_PRO_ASSERT(short_reply_battery.error().code == DeviceError::Code::Timeout,
        "(battery short reply) error is a Timeout");

    hid.short_reply       = false;
    hid.short_link_reply  = true;
    auto short_reply_link = device.getBattery(MDRP_GM01_PRO_TEST_HANDLE);
    MDRP_GM01_PRO_ASSERT(short_reply_link.hasError(), "(link short reply) is an error");
    MDRP_GM01_PRO_ASSERT(short_reply_link.error().code == DeviceError::Code::Timeout,
        "(link short reply) error is a Timeout");
}

void runAllMdrpGM01ProTests()
{
    std::cout << "\n=== Moondrop GM01 Pro Tests ===" << std::endl;
    testMdrpGM01ProRegistered();
    testMdrpGM01ProBattery();
    testMdrpGM01ProHeadsetOff();
    testMdrpGM01ProNoAnswer();
    testMdrpGM01ProUnrelatedResponse();
    testMdrpGM01ProWrongDataSize();
    testMdrpGM01ProShortRead();
}

} // namespace headsetcontrol::testing

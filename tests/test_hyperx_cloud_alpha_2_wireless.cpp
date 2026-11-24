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

// Audio settings reply captured from the hardware with sidetone off, level 10
std::vector<uint8_t> audioSettings(uint8_t enabled, uint8_t level)
{
    return { 0x61, 0x02, 0x5a, 0x00, 0x01, 0x14, 0x00, 0x00, enabled, level, 0x00, 0x00, 0x81, 0x01, 0x0d, 0x01,
        0x00, 0x00, 0x00, 0x30, 0x00, 0xff, 0xff, 0x03, 0x00, 0x0a };
}

// What the vendor software writes for the same settings: 60 01 followed by the settings block
std::vector<uint8_t> audioSettingsWrite(uint8_t enabled, uint8_t level)
{
    std::vector<uint8_t> r(64, 0);
    const std::vector<uint8_t> block { 0x60, 0x01, 0x01, 0x14, 0x00, 0x00, enabled, level, 0x00, 0x00, 0x81, 0x01, 0x0d, 0x01 };
    std::copy(block.begin(), block.end(), r.begin());
    return r;
}

void testGetSidetone()
{
    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    hid.reads.push_back(ack(0x60, 0x02));
    hid.reads.push_back(audioSettings(0x01, 0x0a));

    auto result = device.getSidetone(nullptr);
    ALPHA2_ASSERT(result.hasValue(), "sidetone should be readable");
    ALPHA2_ASSERT(!result->is_muted, "sidetone is on");
    ALPHA2_ASSERT(result->device_level == 10, "device level is byte 9 of the reply");
    ALPHA2_ASSERT(result->current_level == 64, "device level 10/20 maps to 64/128");
    ALPHA2_ASSERT(hid.writes.size() == 1 && hid.writes[0][0] == 0x60 && hid.writes[0][1] == 0x02, "read uses 60 02");

    Alpha2MockHID hid_off;
    TestableAlpha2 device_off(hid_off);
    hid_off.reads.push_back(audioSettings(0x00, 0x0a));
    auto off = device_off.getSidetone(nullptr);
    ALPHA2_ASSERT(off.hasValue() && off->is_muted && off->current_level == 0, "disabled sidetone reads as 0");
}

void testSetSidetone()
{
    struct Case {
        uint8_t level;
        uint8_t enabled;
        uint8_t device_level;
    };
    // 0 turns sidetone off and leaves the stored level alone; 128 is the device maximum (20)
    for (const auto& c : { Case { 0, 0x00, 0x0a }, Case { 128, 0x01, 0x14 }, Case { 1, 0x01, 0x01 } }) {
        Alpha2MockHID hid;
        TestableAlpha2 device(hid);
        hid.reads.push_back(audioSettings(0x01, 0x0a));
        hid.reads.push_back({ 0x45, 0x02, 0x01 }); // unrelated traffic before the acknowledgement
        hid.reads.push_back(ack(0x60, 0x01));

        auto result = device.setSidetone(nullptr, c.level);
        ALPHA2_ASSERT(result.hasValue(), "setting sidetone should succeed");
        ALPHA2_ASSERT(hid.writes.size() == 2, "read the settings, then write them");
        ALPHA2_ASSERT(hid.writes[1] == audioSettingsWrite(c.enabled, c.device_level),
            "only the sidetone bytes of the settings block may change");
        ALPHA2_ASSERT(result->is_muted == (c.enabled == 0), "muted state");
    }
}

void testSetSidetoneRejected()
{
    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    hid.reads.push_back(audioSettings(0x00, 0x0a));
    auto nack = ack(0x60, 0x01);
    nack[1]   = 0x02;
    hid.reads.push_back(nack);

    auto result = device.setSidetone(nullptr, 64);
    ALPHA2_ASSERT(result.hasError(), "rejected write should fail");
    ALPHA2_ASSERT(result.error().code == DeviceError::Code::ProtocolError, "rejection is a protocol error");
}

std::vector<uint8_t> audioSettingsWith(size_t index, uint8_t value)
{
    auto reply                                                         = audioSettings(0x00, 0x0a);
    reply[HyperXCloudAlpha2Wireless::AUDIO_BLOCK_REPLY_OFFSET + index] = value;
    return reply;
}

void testChatmix()
{
    struct Case {
        uint8_t raw;
        int level;
        int game;
        int chat;
    };
    // Raw values captured while moving NGENUITY's slider: 0x7b all game, 0x80 balanced, 0x85 all chat
    for (const auto& c : { Case { 0x7b, 0, 100, 0 }, Case { 0x80, 64, 100, 100 }, Case { 0x85, 128, 0, 100 }, Case { 0x81, 76, 80, 100 } }) {
        Alpha2MockHID hid;
        TestableAlpha2 device(hid);
        hid.reads.push_back(audioSettingsWith(HyperXCloudAlpha2Wireless::CHATMIX, c.raw));
        auto result = device.getChatmix(nullptr);
        ALPHA2_ASSERT(result.hasValue(), "chat-mix should be readable");
        ALPHA2_ASSERT(result->level == c.level, "chat-mix level");
        ALPHA2_ASSERT(result->game_volume_percent == c.game, "game volume");
        ALPHA2_ASSERT(result->chat_volume_percent == c.chat, "chat volume");
    }

    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    hid.reads.push_back(audioSettingsWith(HyperXCloudAlpha2Wireless::CHATMIX, 0x90));
    ALPHA2_ASSERT(device.getChatmix(nullptr).hasError(), "out-of-range chat-mix should fail");
}

void testEqualizerPreset()
{
    // Preset 0 (HeadsetControl) is preset 1 on the device; as captured, only the last block byte changes
    for (uint8_t preset = 0; preset < 3; ++preset) {
        Alpha2MockHID hid;
        TestableAlpha2 device(hid);
        hid.reads.push_back(audioSettings(0x01, 0x0a));
        hid.reads.push_back(ack(0x60, 0x01));

        auto result = device.setEqualizerPreset(nullptr, preset);
        ALPHA2_ASSERT(result.hasValue(), "setting a preset should succeed");
        auto expected = audioSettingsWrite(0x01, 0x0a);
        expected[13]  = static_cast<uint8_t>(preset + 1);
        ALPHA2_ASSERT(hid.writes.size() == 2 && hid.writes[1] == expected, "only the preset byte may change");
        ALPHA2_ASSERT(result->total_presets == 3, "three presets");
    }

    Alpha2MockHID hid;
    TestableAlpha2 device(hid);
    auto result = device.setEqualizerPreset(nullptr, 3);
    ALPHA2_ASSERT(result.hasError() && result.error().code == DeviceError::Code::InvalidParameter, "preset 3 is out of range");
    ALPHA2_ASSERT(hid.writes.empty(), "nothing is sent for an invalid preset");
}

void runAllHyperXCloudAlpha2WirelessTests()
{
    std::cout << "\n=== HyperX Cloud Alpha 2 Wireless Tests ===" << std::endl;
    testBatteryConnected();
    testBatteryCharging();
    testHeadsetOff();
    testRejectedAndTimeout();
    testGetSidetone();
    testSetSidetone();
    testSetSidetoneRejected();
    testChatmix();
    testEqualizerPreset();
    std::cout << "  HyperX Cloud Alpha 2 Wireless tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

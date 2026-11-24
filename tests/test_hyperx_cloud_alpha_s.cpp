#include "devices/hyperx_cloud_alpha_s.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

namespace {

    class AlphaSTestFailure : public std::runtime_error {
    public:
        explicit AlphaSTestFailure(const std::string& message)
            : std::runtime_error(message)
        {
        }
    };

#define ALPHAS_ASSERT(condition, message)                                           \
    do {                                                                            \
        if (!(condition))                                                           \
            throw AlphaSTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

    class AlphaSMockHID final : public HIDInterface {
    public:
        std::deque<std::vector<uint8_t>> reports;
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

        Result<size_t> readTimeout(hid_device*, std::span<uint8_t>, int) override { return DeviceError::notSupported("Not used"); }
        Result<void> sendFeatureReport(hid_device* handle, std::span<const uint8_t> data) override { return write(handle, data); }
        Result<void> sendFeatureReport(hid_device* handle, std::span<const uint8_t> data, size_t size) override { return write(handle, data, size); }
        Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override { return DeviceError::notSupported("Not used"); }

        Result<size_t> getInputReport(hid_device*, std::span<uint8_t> data) override
        {
            if (reports.empty())
                return DeviceError::hidError("Simulated failure");
            auto report = std::move(reports.front());
            reports.pop_front();
            const size_t size = std::min(report.size(), data.size());
            std::copy_n(report.begin(), size, data.begin());
            return size;
        }
    };

    class TestableAlphaS final : public HyperXCloudAlphaS {
    public:
        explicit TestableAlphaS(AlphaSMockHID& hid)
            : hid_(hid)
        {
        }

    protected:
        HIDInterface& getHIDInterface() const override { return hid_; }

    private:
        AlphaSMockHID& hid_;
    };

    // Status reply as captured from a real headset (firmware 0x4110): all off, both sides at 0 dB
    std::vector<uint8_t> statusReply()
    {
        std::vector<uint8_t> r(62, 0);
        const std::vector<uint8_t> header { 0x06, 0x00, 0x04, 0x00, 0x9b, 0x80, 0x00, 0x68, 0x4a, 0x8e, 0x0a };
        std::copy(header.begin(), header.end(), r.begin());
        return r;
    }

    std::vector<uint8_t> statusQuery()
    {
        std::vector<uint8_t> q(62, 0);
        const std::vector<uint8_t> header { 0x06, 0x00, 0x01, 0x00, 0x9b, 0x00, 0x00, 0x68, 0x4a, 0x8e, 0x09 };
        std::copy(header.begin(), header.end(), q.begin());
        return q;
    }

    void setQ8_8(std::vector<uint8_t>& r, int offset, float db)
    {
        const auto raw = static_cast<int16_t>(db * 256);
        r[offset]      = static_cast<uint8_t>(raw >> 8);
        r[offset + 1]  = static_cast<uint8_t>(raw & 0xff);
    }

    void testSetSidetone()
    {
        struct Case {
            uint8_t level;
            bool currently_on;
            bool toggles;
        };
        // The sidetone command toggles on the device, so it is only sent when the state differs
        for (const auto& c : { Case { 128, false, true }, Case { 1, false, true }, Case { 0, true, true },
                 Case { 128, true, false }, Case { 0, false, false } }) {
            AlphaSMockHID hid;
            TestableAlphaS device(hid);
            auto status = statusReply();
            status[14]  = c.currently_on ? 0x01 : 0x00;
            hid.reports.push_back(status);
            auto ack = statusReply();
            ack[2]   = 0x01;
            ack[10]  = 0x07;
            hid.reports.push_back(ack);

            auto result = device.setSidetone(nullptr, c.level);
            ALPHAS_ASSERT(result.hasValue(), "setting sidetone should succeed");
            ALPHAS_ASSERT(hid.writes.size() == (c.toggles ? 2u : 1u), "status query, plus toggle only when needed");
            ALPHAS_ASSERT(hid.writes[0] == statusQuery(), "state is read first");
            if (c.toggles) {
                auto expected = statusQuery();
                expected[10]  = 0x07;
                ALPHAS_ASSERT(hid.writes[1] == expected, "sidetone toggle command bytes");
            }
            ALPHAS_ASSERT(result->is_muted == (c.level == 0), "on/off only");
        }
    }

    void testGetSidetoneAndMicMute()
    {
        AlphaSMockHID hid;
        TestableAlphaS device(hid);
        hid.reports.push_back(statusReply());
        auto off = device.getSidetone(nullptr);
        ALPHAS_ASSERT(off.hasValue() && off->is_muted && off->current_level == 0, "captured idle state has sidetone off");
        ALPHAS_ASSERT(hid.writes.size() == 1 && hid.writes[0] == statusQuery(), "status query bytes");

        auto on = statusReply();
        on[14]  = 0x01; // captured after enabling sidetone
        hid.reports.push_back(on);
        auto result = device.getSidetone(nullptr);
        ALPHAS_ASSERT(result.hasValue() && !result->is_muted && result->current_level == 128, "bit 0 of byte 14 is sidetone");

        auto muted = statusReply();
        muted[14]  = 0x02;
        hid.reports.push_back(muted);
        auto mic = device.getMicMuteStatus(nullptr);
        ALPHAS_ASSERT(mic.hasValue() && mic->muted, "bit 1 of byte 14 is microphone mute");
    }

    void testChatmix()
    {
        struct Case {
            float game_db;
            float chat_db;
            int level;
            int game;
            int chat;
        };
        for (const auto& c : { Case { 0, 0, 64, 100, 100 }, Case { -50, 0, 128, 0, 100 }, Case { 0, -50, 0, 100, 0 }, Case { 0, -8, 40, 100, 63 } }) {
            AlphaSMockHID hid;
            TestableAlphaS device(hid);
            auto r = statusReply();
            setQ8_8(r, HyperXCloudAlphaS::STATUS_GAME_DB, c.game_db);
            setQ8_8(r, HyperXCloudAlphaS::STATUS_CHAT_DB, c.chat_db);
            hid.reports.push_back(r);

            auto result = device.getChatmix(nullptr);
            ALPHAS_ASSERT(result.hasValue(), "chat-mix should be readable");
            ALPHAS_ASSERT(result->level == c.level, "chat-mix level");
            ALPHAS_ASSERT(result->game_volume_percent == c.game, "game volume");
            ALPHAS_ASSERT(result->chat_volume_percent == c.chat, "chat volume");
        }
    }

    void testBadReply()
    {
        AlphaSMockHID hid;
        TestableAlphaS device(hid);
        auto nack = statusReply();
        nack[5]   = 0x00;
        hid.reports.push_back(nack);
        ALPHAS_ASSERT(device.getSidetone(nullptr).hasError(), "reply without ack bit should fail");

        ALPHAS_ASSERT(device.getChatmix(nullptr).hasError(), "failed input report should propagate");
    }

} // namespace

void runAllHyperXCloudAlphaSTests()
{
    std::cout << "\n=== HyperX Cloud Alpha S Tests ===" << std::endl;
    testSetSidetone();
    testGetSidetoneAndMicMute();
    testChatmix();
    testBadReply();
    std::cout << "  HyperX Cloud Alpha S tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

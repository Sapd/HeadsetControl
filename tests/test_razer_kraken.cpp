#include "devices/razer_kraken_v4.hpp"
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {
namespace {
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    std::vector<uint8_t> packet(std::string_view hex)
    {
        std::vector<uint8_t> result;
        for (size_t i = 0; i < hex.size(); i += 2)
            result.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
        return result;
    }
    const auto BATTERY_QUERY  = packet("0200600000000400008021000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000c700");
    const auto CHARGING_QUERY = packet("020060000000040000802a000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000cc00");
    const auto BATTERY_44     = packet("020260000000050080802101012c0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000006a00");
    const auto BATTERY_45     = packet("020260000000050080802101012d0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000006b00");
    const auto CHARGING_0     = packet("020260000000050080802a0101000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004d00");
    const auto CHARGING_1     = packet("020260000000050080802a0101010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004c00");
    const auto NOTIFICATION   = packet("020a60560000050080802a0201010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001100");

    class KrakenMockHID final : public HIDInterface {
    public:
        std::deque<Result<std::vector<uint8_t>>> reads;
        std::vector<std::vector<uint8_t>> writes;
        bool fail_write = false;

        Result<void> write(hid_device*, std::span<const uint8_t> data) override
        {
            writes.emplace_back(data.begin(), data.end());
            if (fail_write)
                return DeviceError::hidError("Simulated HID write error");
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

            auto result = std::move(reads.front());
            reads.pop_front();
            if (!result)
                return result.error();

            const auto& response = *result;
            const size_t size    = std::min(response.size(), data.size());
            std::copy_n(response.begin(), size, data.begin());
            return size;
        }

        Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>) override
        {
            return DeviceError::notSupported("Kraken queries must use Output reports");
        }

        Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>, size_t) override
        {
            return DeviceError::notSupported("Kraken queries must use Output reports");
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

    class TestableKraken : public RazerKrakenV4 {
    public:
        explicit TestableKraken(KrakenMockHID& hid)
            : hid_(hid)
        {
        }

    protected:
        HIDInterface& getHIDInterface() const override { return hid_; }

    private:
        KrakenMockHID& hid_;
    };
} // namespace

void runAllRazerKrakenTests()
{
    for (bool charging : { false, true }) {
        KrakenMockHID hid;
        TestableKraken device(hid);
        hid.reads.emplace_back(NOTIFICATION);
        hid.reads.emplace_back(charging ? CHARGING_1 : CHARGING_0);
        hid.reads.emplace_back(charging ? BATTERY_45 : BATTERY_44);
        auto result = device.getBattery(nullptr);
        require(result.hasValue(), "Captured Kraken responses should parse");
        require(result->level_percent == (charging ? 45 : 44), "Captured battery should match UI");
        require(result->status == (charging ? BATTERY_CHARGING : BATTERY_AVAILABLE), "Charging should match capture");
        require(hid.writes.size() == 2, "Only two captured queries should be sent");
        require(hid.writes[0] == CHARGING_QUERY && hid.writes[1] == BATTERY_QUERY, "Queries must match captured packets exactly");
    }
    {
        KrakenMockHID hid;
        TestableKraken device(hid);
        auto result = device.getBattery(nullptr);
        require(result.hasError(), "Missing response should fail");
        require(hid.writes.size() == 1, "Timeout should not resend query");
    }
    {
        KrakenMockHID hid;
        TestableKraken device(hid);
        hid.fail_write = true;
        require(device.getBattery(nullptr).hasError(), "Write failure should propagate");
        require(hid.writes.size() == 1, "Write failure should not retry");
    }
    {
        KrakenMockHID hid;
        TestableKraken device(hid);
        auto invalid_charging = CHARGING_0;
        invalid_charging[13]  = 2;
        hid.reads.emplace_back(invalid_charging);
        require(device.getBattery(nullptr).hasError(), "Unknown charging flag should fail");
    }
    for (int invalid : { 101, 255 }) {
        KrakenMockHID hid;
        TestableKraken device(hid);
        auto bad = BATTERY_44;
        bad[13]  = static_cast<uint8_t>(invalid);
        hid.reads.emplace_back(CHARGING_0);
        hid.reads.emplace_back(bad);
        require(device.getBattery(nullptr).hasError(), "Invalid percentage should fail");
    }
    {
        KrakenMockHID hid;
        TestableKraken device(hid);
        auto short_response = CHARGING_0;
        short_response.resize(13);
        hid.reads.emplace_back(short_response);
        require(device.getBattery(nullptr).hasError(), "Truncated matching response should fail");
    }
}
} // namespace headsetcontrol::testing

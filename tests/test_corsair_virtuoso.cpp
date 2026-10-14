#include "devices/corsair_virtuoso_xt.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class VirtuosoTestFailure : public std::runtime_error {
public:
    explicit VirtuosoTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define VIRTUOSO_ASSERT(condition, message)                                           \
    do {                                                                              \
        if (!(condition))                                                             \
            throw VirtuosoTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

/**
 * @brief Mock HID interface that knows when each report arrives
 *
 * A report only becomes readable once a given number of requests have been
 * written, which is what separates a stale reply (already queued before a
 * request goes out) from the reply to that request.
 */
class VirtuosoMockHID final : public HIDInterface {
public:
    struct Report {
        size_t after_write; // readable once this many writes have happened
        std::vector<uint8_t> data;
    };

    std::deque<Report> reads;
    std::vector<std::vector<uint8_t>> writes;
    size_t fail_on_write = 0; // 1-based; 0 means never

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        if (fail_on_write != 0 && writes.size() == fail_on_write)
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
        if (reads.empty() || reads.front().after_write > writes.size())
            return DeviceError::timeout("Simulated timeout");

        const auto report = std::move(reads.front());
        reads.pop_front();
        const size_t size = std::min(report.data.size(), data.size());
        std::copy_n(report.data.begin(), size, data.begin());
        return size;
    }

    Result<void> sendFeatureReport(hid_device* handle, std::span<const uint8_t> data) override
    {
        return write(handle, data);
    }

    Result<void> sendFeatureReport(hid_device* handle, std::span<const uint8_t> data, size_t size) override
    {
        return write(handle, data, size);
    }

    Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this test");
    }

    Result<size_t> getInputReport(hid_device*, std::span<uint8_t>) override
    {
        return DeviceError::notSupported("Not used by this test");
    }

    /// Queue a GET/SET reply: [report 0x01][source][command][status][value LE]
    void reply(size_t after_write, uint8_t source, uint8_t command, uint8_t status, uint32_t value = 0)
    {
        reads.push_back({ after_write,
            { 0x01, source, command, status, static_cast<uint8_t>(value & 0xFF),
                static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
                static_cast<uint8_t>((value >> 24) & 0xFF) } });
    }
};

class TestableVirtuoso final : public CorsairVirtuosoXT {
public:
    TestableVirtuoso(VirtuosoMockHID& hid, uint16_t product_id)
        : hid_(hid)
    {
        setMatchedProductId(product_id);
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    VirtuosoMockHID& hid_;
};

constexpr uint16_t PID_WIRELESS  = 0x0a64;
constexpr uint16_t PID_WIRED     = 0x0a62;
constexpr uint8_t FROM_HEADSET   = 0x01;
constexpr uint8_t FROM_SELF      = 0x00;
constexpr uint8_t GET            = 0x02;
constexpr uint8_t STATUS_OK      = 0x00;
constexpr uint8_t STATUS_NO_PROP = 0x05;
constexpr uint32_t CHARGING      = 1;
constexpr uint32_t DISCHARGING   = 2;

void testVirtuosoBattery()
{
    std::cout << "  Testing battery over both targets..." << std::endl;

    // Wireless: the receiver relays to the headset behind it.
    VirtuosoMockHID wireless;
    TestableVirtuoso wireless_device(wireless, PID_WIRELESS);
    wireless.reply(1, FROM_HEADSET, GET, STATUS_OK, 810);
    wireless.reply(2, FROM_HEADSET, GET, STATUS_OK, CHARGING);
    auto battery = wireless_device.getBattery(nullptr);
    VIRTUOSO_ASSERT(battery.hasValue(), "wireless battery should succeed");
    VIRTUOSO_ASSERT(battery->level_percent == 81, "level is reported in tenths of a percent");
    VIRTUOSO_ASSERT(battery->status == BATTERY_CHARGING, "charge state 1 means charging");
    VIRTUOSO_ASSERT(wireless.writes.size() == 2, "the probed level should not be read a second time");
    VIRTUOSO_ASSERT(wireless.writes[0][1] == 0x09, "wireless requests target the headset");

    // Wired: the headset answers for itself.
    VirtuosoMockHID wired;
    TestableVirtuoso wired_device(wired, PID_WIRED);
    wired.reply(1, FROM_SELF, GET, STATUS_OK, 1000);
    wired.reply(2, FROM_SELF, GET, STATUS_OK, DISCHARGING);
    battery = wired_device.getBattery(nullptr);
    VIRTUOSO_ASSERT(battery.hasValue(), "wired battery should succeed");
    VIRTUOSO_ASSERT(battery->level_percent == 100 && battery->status == BATTERY_AVAILABLE,
        "wired battery should read 100% discharging");
    VIRTUOSO_ASSERT(wired.writes[0][1] == 0x08, "wired requests target the device itself");

    std::cout << "    OK battery over both targets" << std::endl;
}

void testVirtuosoStaleReplyIsDiscarded()
{
    std::cout << "  Testing a late reply is not taken for a new request's..." << std::endl;

    // A charge-state reply left over from an earlier request that timed out is
    // already queued before getBattery() sends anything. Replies carry no
    // property ID, so without discarding it the level probe would read 1 as the
    // battery level.
    VirtuosoMockHID hid;
    TestableVirtuoso device(hid, PID_WIRELESS);
    hid.reply(0, FROM_HEADSET, GET, STATUS_OK, CHARGING);
    hid.reply(1, FROM_HEADSET, GET, STATUS_OK, 810);
    hid.reply(2, FROM_HEADSET, GET, STATUS_OK, DISCHARGING);

    auto battery = device.getBattery(nullptr);
    VIRTUOSO_ASSERT(battery.hasValue(), "battery should succeed");
    VIRTUOSO_ASSERT(battery->level_percent == 81, "the stale reply must not be read as the level");
    VIRTUOSO_ASSERT(battery->status == BATTERY_AVAILABLE, "the charge state must come from its own reply");

    std::cout << "    OK late reply discarded" << std::endl;
}

void testVirtuosoTargetResolutionErrors()
{
    std::cout << "  Testing which probe failures mean \"wrong target\"..." << std::endl;

    // A receiver with its headset off: the headset stays silent, and the
    // receiver answers the battery probe for itself with "no such property".
    // Both mean nothing is there, so the headset is offline.
    VirtuosoMockHID receiver_only;
    TestableVirtuoso receiver_device(receiver_only, PID_WIRELESS);
    receiver_only.reply(2, FROM_SELF, GET, STATUS_NO_PROP);
    auto offline = receiver_device.getBattery(nullptr);
    VIRTUOSO_ASSERT(offline.hasError(), "no headset should fail");
    VIRTUOSO_ASSERT(offline.error().code == DeviceError::Code::DeviceOffline,
        "a silent headset behind a receiver should be reported offline, not unsupported");
    VIRTUOSO_ASSERT(receiver_only.writes.size() == 2, "both targets should have been probed");

    // A HID failure is a real fault and must not be dressed up as offline.
    VirtuosoMockHID broken;
    TestableVirtuoso broken_device(broken, PID_WIRELESS);
    broken.fail_on_write = 1;
    auto failure         = broken_device.getBattery(nullptr);
    VIRTUOSO_ASSERT(failure.hasError(), "a failed write should fail");
    VIRTUOSO_ASSERT(failure.error().code == DeviceError::Code::HIDError,
        "a HID error during target resolution should be propagated");
    VIRTUOSO_ASSERT(broken.writes.size() == 1, "a HID error should not fall through to the other target");

    std::cout << "    OK target resolution errors" << std::endl;
}

void runAllCorsairVirtuosoTests()
{
    std::cout << "\n=== Corsair Virtuoso Tests ===" << std::endl;
    testVirtuosoBattery();
    testVirtuosoStaleReplyIsDiscarded();
    testVirtuosoTargetResolutionErrors();
    std::cout << "  Corsair Virtuoso tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

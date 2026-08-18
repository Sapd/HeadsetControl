#include "devices/steelseries_arctis_nova_7.hpp"

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

class SidetoneTestFailure : public std::runtime_error {
public:
    explicit SidetoneTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define SIDETONE_ASSERT(condition, message)                                           \
    do {                                                                              \
        if (!(condition))                                                             \
            throw SidetoneTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

class SidetoneMockHID final : public HIDInterface {
public:
    std::deque<Result<std::vector<uint8_t>>> reads;
    std::vector<std::vector<uint8_t>> writes;
    bool fail_write      = false;
    size_t fail_on_write = 0;

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        if (fail_write || (fail_on_write != 0 && writes.size() == fail_on_write))
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
};

class TestableNova7 final : public SteelSeriesArctisNova7 {
public:
    explicit TestableNova7(SidetoneMockHID& hid, uint16_t product_id = 0x227e)
        : hid_(hid)
    {
        setMatchedProductId(product_id);
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }

private:
    SidetoneMockHID& hid_;
};

void checkLevel(uint8_t raw, uint8_t normalized, std::string_view name)
{
    SidetoneMockHID hid;
    TestableNova7 device(hid);
    hid.reads.emplace_back(std::vector<uint8_t> { 0x20, 0x07, raw, 0x01 });

    auto result = device.getSidetone(nullptr);
    SIDETONE_ASSERT(result.hasValue(), "valid response should succeed");
    SIDETONE_ASSERT(result->current_level == normalized, "normalized level should match");
    SIDETONE_ASSERT(result->device_level == raw, "native level should match");
    SIDETONE_ASSERT(result->level_name == name, "level name should match");
    SIDETONE_ASSERT(hid.writes.size() == 1, "query should write once");
    SIDETONE_ASSERT(hid.writes[0].size() == 64, "query report should be 64 bytes");
    SIDETONE_ASSERT(hid.writes[0][0] == 0x00 && hid.writes[0][1] == 0x20,
        "query should use the audio settings command");
}

void testSteelSeriesSidetoneLevels()
{
    checkLevel(0, 0, "Off");
    checkLevel(1, 43, "Low");
    checkLevel(2, 85, "Medium");
    checkLevel(3, 128, "High");
}

void testSteelSeriesSidetoneValidation()
{
    {
        SidetoneMockHID hid;
        TestableNova7 device(hid);
        hid.reads.emplace_back(std::vector<uint8_t> { 0x20, 0x07, 0x01 });
        auto result = device.getSidetone(nullptr);
        SIDETONE_ASSERT(result.hasError(), "short response should fail");
        SIDETONE_ASSERT(result.error().code == DeviceError::Code::ProtocolError,
            "short response should be a protocol error");
    }
    {
        SidetoneMockHID hid;
        TestableNova7 device(hid);
        hid.reads.emplace_back(std::vector<uint8_t> { 0x21, 0x07, 0x01, 0x01 });
        auto result = device.getSidetone(nullptr);
        SIDETONE_ASSERT(result.hasError(), "unexpected response type should fail");
        SIDETONE_ASSERT(result.error().code == DeviceError::Code::ProtocolError,
            "unexpected response type should be a protocol error");
    }
    {
        SidetoneMockHID hid;
        TestableNova7 device(hid);
        hid.reads.emplace_back(std::vector<uint8_t> { 0x20, 0x07, 0x04, 0x01 });
        auto result = device.getSidetone(nullptr);
        SIDETONE_ASSERT(result.hasError(), "invalid sidetone value should fail");
        SIDETONE_ASSERT(result.error().code == DeviceError::Code::ProtocolError,
            "invalid sidetone value should be a protocol error");
    }
}

void testSteelSeriesSidetoneHIDErrors()
{
    {
        SidetoneMockHID hid;
        TestableNova7 device(hid);
        hid.fail_write = true;
        auto result    = device.getSidetone(nullptr);
        SIDETONE_ASSERT(result.hasError(), "write error should be propagated");
        SIDETONE_ASSERT(result.error().code == DeviceError::Code::HIDError,
            "write failure should remain a HID error");
    }
    {
        SidetoneMockHID hid;
        TestableNova7 device(hid);
        hid.reads.emplace_back(DeviceError::hidError("Simulated HID read error"));
        auto result = device.getSidetone(nullptr);
        SIDETONE_ASSERT(result.hasError(), "read error should be propagated");
        SIDETONE_ASSERT(result.error().code == DeviceError::Code::HIDError,
            "read failure should remain a HID error");
    }
    {
        SidetoneMockHID hid;
        TestableNova7 device(hid);
        hid.reads.emplace_back(DeviceError::timeout("Simulated timeout"));
        auto result = device.getSidetone(nullptr);
        SIDETONE_ASSERT(result.hasError(), "timeout should be propagated");
        SIDETONE_ASSERT(result.error().code == DeviceError::Code::Timeout,
            "timeout should remain a timeout");
    }
}

void testSteelSeriesSidetoneAsyncStatusAndSave()
{
    SidetoneMockHID hid;
    TestableNova7 device(hid);
    hid.reads.emplace_back(std::vector<uint8_t> { 0xb0, 0x00, 0x64, 0x01 });
    hid.reads.emplace_back(std::vector<uint8_t> { 0x20, 0x07, 0x01, 0x01 });

    auto read_result = device.getSidetone(nullptr);
    SIDETONE_ASSERT(read_result.hasValue() && read_result->current_level == 43,
        "asynchronous status report should be skipped");

    hid.writes.clear();
    auto set_result = device.setSidetone(nullptr, 43);
    SIDETONE_ASSERT(set_result.hasValue(), "setting sidetone should succeed");
    SIDETONE_ASSERT(hid.writes.size() == 2, "Gen 2 setting should be followed by save");
    SIDETONE_ASSERT(hid.writes[0][0] == 0x00 && hid.writes[0][1] == 0x39
            && hid.writes[0][2] == 0x01,
        "set command should contain the discrete level");
    SIDETONE_ASSERT(hid.writes[1][0] == 0x00 && hid.writes[1][1] == 0x09,
        "Gen 2 should use the confirmed save command");

    SidetoneMockHID legacy_hid;
    TestableNova7 legacy_device(legacy_hid, 0x2202);
    auto legacy_result = legacy_device.setSidetone(nullptr, 43);
    SIDETONE_ASSERT(legacy_result.hasValue(), "legacy Nova 7 setting should succeed");
    SIDETONE_ASSERT(legacy_hid.writes.size() == 1,
        "other Nova 7 product IDs must not receive the Gen 2 save command");

    SIDETONE_ASSERT((device.getCapabilities() & B(CAP_SIDETONE_STATUS)) != 0,
        "Gen 2 should advertise sidetone status");
    SIDETONE_ASSERT((legacy_device.getCapabilities() & B(CAP_SIDETONE_STATUS)) == 0,
        "other Nova 7 product IDs must not advertise sidetone status");

    auto unsupported_result = legacy_device.getSidetone(nullptr);
    SIDETONE_ASSERT(unsupported_result.hasError(),
        "sidetone reading should be rejected for unverified product IDs");
    SIDETONE_ASSERT(unsupported_result.error().code == DeviceError::Code::NotSupported,
        "unverified product IDs should return not supported");

    SidetoneMockHID save_failure_hid;
    TestableNova7 save_failure_device(save_failure_hid);
    save_failure_hid.fail_on_write = 2;
    auto save_failure_result       = save_failure_device.setSidetone(nullptr, 43);
    SIDETONE_ASSERT(save_failure_result.hasError(), "save write failure should be propagated");
    SIDETONE_ASSERT(save_failure_result.error().code == DeviceError::Code::HIDError,
        "save write failure should remain a HID error");
}

void testSteelSeriesSidetoneShortAsyncStatus()
{
    // A status report is still one to skip even if it comes in short - the
    // 0xb0 check has to run before the length check that guards the real
    // settings response.
    SidetoneMockHID hid;
    TestableNova7 device(hid);
    hid.reads.emplace_back(std::vector<uint8_t> { 0xb0 });
    hid.reads.emplace_back(std::vector<uint8_t> { 0x20, 0x07, 0x02, 0x01 });

    auto result = device.getSidetone(nullptr);
    SIDETONE_ASSERT(result.hasValue(), "a short status report should be skipped, not fail the query");
    SIDETONE_ASSERT(result->current_level == 85, "the real response after it should still be read");
}

void runAllSteelSeriesSidetoneTests()
{
    std::cout << "\n=== SteelSeries Sidetone Tests ===" << std::endl;
    testSteelSeriesSidetoneLevels();
    testSteelSeriesSidetoneValidation();
    testSteelSeriesSidetoneHIDErrors();
    testSteelSeriesSidetoneAsyncStatusAndSave();
    testSteelSeriesSidetoneShortAsyncStatus();
    std::cout << "  SteelSeries sidetone tests passed" << std::endl;
}

} // namespace headsetcontrol::testing

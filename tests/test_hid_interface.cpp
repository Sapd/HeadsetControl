/**
 * @file test_hid_interface.cpp
 * @brief Tests for the HID interface abstraction and mock implementation
 *
 * Tests for:
 * - Mock HID interface for testing devices without hardware
 * - HID communication patterns
 * - Error handling in HID operations
 */

#include "devices/hid_interface.hpp"
#include "result_types.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

// ============================================================================
// Test Utilities
// ============================================================================

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& msg)
        : std::runtime_error(msg)
    {
    }
};

#define ASSERT_TRUE(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            throw TestFailure(std::string("ASSERT_TRUE failed: ") + (msg)); \
        }                                                                   \
    } while (0)

#define ASSERT_FALSE(cond, msg)                                              \
    do {                                                                     \
        if ((cond)) {                                                        \
            throw TestFailure(std::string("ASSERT_FALSE failed: ") + (msg)); \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(expected, actual, msg)                        \
    do {                                                        \
        if ((expected) != (actual)) {                           \
            std::ostringstream oss;                             \
            oss << "ASSERT_EQ failed: " << (msg) << "\n"        \
                << "  Expected: " << static_cast<int>(expected) \
                << "\n"                                         \
                << "  Actual:   " << static_cast<int>(actual);  \
            throw TestFailure(oss.str());                       \
        }                                                       \
    } while (0)

// ============================================================================
// Mock HID Interface Implementation
// ============================================================================

/**
 * @brief Mock HID interface for testing
 *
 * This mock records all operations and can be configured to return
 * specific responses or simulate errors.
 */
class MockHIDInterface : public HIDInterface {
public:
    // Configuration
    bool simulate_write_error = false;
    bool simulate_read_error  = false;
    bool simulate_timeout     = false;
    std::vector<uint8_t> read_response;
    size_t feature_report_response_size = 0;

    // Recording
    std::vector<std::vector<uint8_t>> written_data;
    int write_count = 0;
    int read_count  = 0;

    void reset()
    {
        simulate_write_error = false;
        simulate_read_error  = false;
        simulate_timeout     = false;
        read_response.clear();
        feature_report_response_size = 0;
        written_data.clear();
        write_count = 0;
        read_count  = 0;
    }

    [[nodiscard]] auto write(hid_device* /*device_handle*/, std::span<const uint8_t> data)
        -> Result<void> override
    {
        write_count++;
        written_data.push_back(std::vector<uint8_t>(data.begin(), data.end()));

        if (simulate_write_error) {
            return DeviceError::hidError("Simulated write error");
        }
        return {};
    }

    [[nodiscard]] auto write(hid_device* /*device_handle*/, std::span<const uint8_t> data, size_t size)
        -> Result<void> override
    {
        write_count++;
        std::vector<uint8_t> padded(size, 0);
        std::copy_n(data.begin(), std::min(data.size(), size), padded.begin());
        written_data.push_back(std::move(padded));

        if (simulate_write_error) {
            return DeviceError::hidError("Simulated write error");
        }
        return {};
    }

    [[nodiscard]] auto readTimeout(hid_device* /*device_handle*/, std::span<uint8_t> data, int /*timeout_ms*/)
        -> Result<size_t> override
    {
        read_count++;

        if (simulate_read_error) {
            return DeviceError::hidError("Simulated read error");
        }

        if (simulate_timeout) {
            return DeviceError::timeout("Simulated timeout");
        }

        if (read_response.empty()) {
            return DeviceError::timeout("No response configured");
        }

        size_t copy_size = std::min(read_response.size(), data.size());
        std::copy_n(read_response.begin(), copy_size, data.begin());

        return copy_size;
    }

    [[nodiscard]] auto sendFeatureReport(hid_device* /*device_handle*/, std::span<const uint8_t> data)
        -> Result<void> override
    {
        write_count++;
        written_data.push_back(std::vector<uint8_t>(data.begin(), data.end()));

        if (simulate_write_error) {
            return DeviceError::hidError("Simulated feature report error");
        }
        return {};
    }

    [[nodiscard]] auto sendFeatureReport(hid_device* /*device_handle*/, std::span<const uint8_t> data, size_t size)
        -> Result<void> override
    {
        write_count++;
        std::vector<uint8_t> padded(size, 0);
        std::copy_n(data.begin(), std::min(data.size(), size), padded.begin());
        written_data.push_back(std::move(padded));

        if (simulate_write_error) {
            return DeviceError::hidError("Simulated feature report error");
        }
        return {};
    }

    [[nodiscard]] auto getFeatureReport(hid_device* /*device_handle*/, std::span<uint8_t> data)
        -> Result<size_t> override
    {
        read_count++;

        if (simulate_read_error) {
            return DeviceError::hidError("Simulated get feature report error");
        }

        if (read_response.empty()) {
            return feature_report_response_size > 0 ? feature_report_response_size : data.size();
        }

        size_t copy_size = std::min(read_response.size(), data.size());
        std::copy_n(read_response.begin(), copy_size, data.begin());

        return copy_size;
    }

    [[nodiscard]] auto getInputReport(hid_device* /*device_handle*/, std::span<uint8_t> data)
        -> Result<size_t> override
    {
        read_count++;

        if (simulate_read_error) {
            return DeviceError::hidError("Simulated get input report error");
        }

        if (read_response.empty()) {
            return data.size();
        }

        size_t copy_size = std::min(read_response.size(), data.size());
        std::copy_n(read_response.begin(), copy_size, data.begin());

        return copy_size;
    }
};

// ============================================================================
// Mock Interface Tests
// ============================================================================

void testMockWriteBasic()
{
    std::cout << "  Testing mock write basic..." << std::endl;

    MockHIDInterface mock;

    std::array<uint8_t, 4> data { 0x01, 0x02, 0x03, 0x04 };
    auto result = mock.write(nullptr, data);

    ASSERT_TRUE(result.hasValue(), "Write should succeed");
    ASSERT_EQ(1, mock.write_count, "Should record one write");
    ASSERT_EQ(4u, mock.written_data[0].size(), "Should record correct size");
    ASSERT_EQ(0x01, mock.written_data[0][0], "Should record correct data[0]");
    ASSERT_EQ(0x04, mock.written_data[0][3], "Should record correct data[3]");

    std::cout << "    OK mock write basic" << std::endl;
}

void testMockWriteWithSize()
{
    std::cout << "  Testing mock write with explicit size..." << std::endl;

    MockHIDInterface mock;

    std::array<uint8_t, 2> data { 0xAA, 0xBB };
    auto result = mock.write(nullptr, data, 8); // Pad to 8 bytes

    ASSERT_TRUE(result.hasValue(), "Write should succeed");
    ASSERT_EQ(8u, mock.written_data[0].size(), "Should pad to requested size");
    ASSERT_EQ(0xAA, mock.written_data[0][0], "First byte preserved");
    ASSERT_EQ(0xBB, mock.written_data[0][1], "Second byte preserved");
    ASSERT_EQ(0x00, mock.written_data[0][2], "Padding bytes are zero");

    std::cout << "    OK mock write with size" << std::endl;
}

void testMockWriteError()
{
    std::cout << "  Testing mock write error..." << std::endl;

    MockHIDInterface mock;
    mock.simulate_write_error = true;

    std::array<uint8_t, 2> data { 0x01, 0x02 };
    auto result = mock.write(nullptr, data);

    ASSERT_FALSE(result.hasValue(), "Write should fail");
    ASSERT_TRUE(result.hasError(), "Should have error");
    ASSERT_TRUE(result.error().code == DeviceError::Code::HIDError, "Should be HID error");

    std::cout << "    OK mock write error" << std::endl;
}

void testMockReadWithResponse()
{
    std::cout << "  Testing mock read with response..." << std::endl;

    MockHIDInterface mock;
    mock.read_response = { 0x11, 0x22, 0x33, 0x44 };

    std::array<uint8_t, 8> buffer {};
    auto result = mock.readTimeout(nullptr, buffer, 1000);

    ASSERT_TRUE(result.hasValue(), "Read should succeed");
    ASSERT_EQ(4u, result.value(), "Should return response size");
    ASSERT_EQ(0x11, buffer[0], "First byte matches");
    ASSERT_EQ(0x44, buffer[3], "Fourth byte matches");

    std::cout << "    OK mock read with response" << std::endl;
}

void testMockReadTimeout()
{
    std::cout << "  Testing mock read timeout..." << std::endl;

    MockHIDInterface mock;
    mock.simulate_timeout = true;

    std::array<uint8_t, 8> buffer {};
    auto result = mock.readTimeout(nullptr, buffer, 1000);

    ASSERT_FALSE(result.hasValue(), "Read should fail");
    ASSERT_TRUE(result.error().code == DeviceError::Code::Timeout, "Should be timeout error");

    std::cout << "    OK mock read timeout" << std::endl;
}

void testMockReadError()
{
    std::cout << "  Testing mock read error..." << std::endl;

    MockHIDInterface mock;
    mock.simulate_read_error = true;

    std::array<uint8_t, 8> buffer {};
    auto result = mock.readTimeout(nullptr, buffer, 1000);

    ASSERT_FALSE(result.hasValue(), "Read should fail");
    ASSERT_TRUE(result.error().code == DeviceError::Code::HIDError, "Should be HID error");

    std::cout << "    OK mock read error" << std::endl;
}

void testMockFeatureReport()
{
    std::cout << "  Testing mock feature report..." << std::endl;

    MockHIDInterface mock;
    mock.read_response = { 0x00, 0xAA, 0xBB }; // Report ID + data

    // Send feature report
    std::array<uint8_t, 3> send_data { 0x00, 0x01, 0x02 };
    auto send_result = mock.sendFeatureReport(nullptr, send_data);
    ASSERT_TRUE(send_result.hasValue(), "Send should succeed");

    // Get feature report
    std::array<uint8_t, 8> recv_buffer { 0x00 }; // Report ID in first byte
    auto recv_result = mock.getFeatureReport(nullptr, recv_buffer);
    ASSERT_TRUE(recv_result.hasValue(), "Get should succeed");
    ASSERT_EQ(3u, recv_result.value(), "Should return response size");
    ASSERT_EQ(0xAA, recv_buffer[1], "Data matches");

    std::cout << "    OK mock feature report" << std::endl;
}

void testMockInputReport()
{
    std::cout << "  Testing mock input report..." << std::endl;

    MockHIDInterface mock;
    // Use push_back for MinGW compatibility (initializer list assignment may fail)
    mock.read_response.push_back(0x01);
    mock.read_response.push_back(0xFF);
    mock.read_response.push_back(0xEE);

    // Verify response was set correctly
    ASSERT_EQ(3u, mock.read_response.size(), "Response should have 3 bytes");

    // Use different initial value to verify copy actually happened
    std::array<uint8_t, 8> buffer {};
    buffer[0]   = 0x01; // Report ID
    auto result = mock.getInputReport(nullptr, buffer);

    ASSERT_TRUE(result.hasValue(), "Get should succeed");
    ASSERT_EQ(3u, result.value(), "Should return 3 bytes copied");
    ASSERT_EQ(0x01, buffer[0], "Report ID preserved");
    ASSERT_EQ(0xFF, buffer[1], "Data byte 1");
    ASSERT_EQ(0xEE, buffer[2], "Data byte 2");

    std::cout << "    OK mock input report" << std::endl;
}

void testMockReset()
{
    std::cout << "  Testing mock reset..." << std::endl;

    MockHIDInterface mock;
    mock.simulate_write_error = true;
    mock.read_response        = { 0x01, 0x02 };
    mock.written_data.push_back({ 0xAA });
    mock.write_count = 5;
    mock.read_count  = 3;

    mock.reset();

    ASSERT_FALSE(mock.simulate_write_error, "Flags should reset");
    ASSERT_TRUE(mock.read_response.empty(), "Response should clear");
    ASSERT_TRUE(mock.written_data.empty(), "Written data should clear");
    ASSERT_EQ(0, mock.write_count, "Write count should reset");
    ASSERT_EQ(0, mock.read_count, "Read count should reset");

    std::cout << "    OK mock reset" << std::endl;
}

void testMockMultipleWrites()
{
    std::cout << "  Testing mock multiple writes..." << std::endl;

    MockHIDInterface mock;

    std::array<uint8_t, 2> data1 { 0x01, 0x02 };
    std::array<uint8_t, 3> data2 { 0x03, 0x04, 0x05 };
    std::array<uint8_t, 1> data3 { 0x06 };

    (void)mock.write(nullptr, data1);
    (void)mock.write(nullptr, data2);
    (void)mock.write(nullptr, data3);

    ASSERT_EQ(3, mock.write_count, "Should record 3 writes");
    ASSERT_EQ(2u, mock.written_data[0].size(), "First write size");
    ASSERT_EQ(3u, mock.written_data[1].size(), "Second write size");
    ASSERT_EQ(1u, mock.written_data[2].size(), "Third write size");

    std::cout << "    OK mock multiple writes" << std::endl;
}

// ============================================================================
// HID Communication Pattern Tests
// ============================================================================

void testTypicalBatteryQueryPattern()
{
    std::cout << "  Testing typical battery query pattern..." << std::endl;

    MockHIDInterface mock;

    // Configure response (simulating a battery response)
    mock.read_response = {
        0x00, 0xB0, // Header
        0x00, 0x00, 0x00, 0x00, // Padding
        0x50, // Battery level (80 in 0-100)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // More padding
        0x08 // Status byte (online)
    };

    // Send battery request
    std::array<uint8_t, 2> request { 0x00, 0xB0 };
    auto write_result = mock.write(nullptr, request);
    ASSERT_TRUE(write_result.hasValue(), "Request should succeed");

    // Read response
    std::array<uint8_t, 16> response {};
    auto read_result = mock.readTimeout(nullptr, response, 5000);
    ASSERT_TRUE(read_result.hasValue(), "Response should succeed");

    // Verify data
    ASSERT_EQ(0x50, response[6], "Battery level should be at correct offset");
    ASSERT_EQ(0x08, response[15], "Status byte at correct offset");

    std::cout << "    OK typical battery query pattern" << std::endl;
}

void testTypicalSetCommandPattern()
{
    std::cout << "  Testing typical set command pattern..." << std::endl;

    MockHIDInterface mock;

    // Set sidetone command pattern
    std::array<uint8_t, 3> sidetone_cmd { 0x06, 0x39, 0x02 }; // Report ID, cmd, level

    auto result = mock.write(nullptr, sidetone_cmd);
    ASSERT_TRUE(result.hasValue(), "Set command should succeed");

    // Verify command was recorded
    ASSERT_EQ(0x06, mock.written_data[0][0], "Report ID");
    ASSERT_EQ(0x39, mock.written_data[0][1], "Command byte");
    ASSERT_EQ(0x02, mock.written_data[0][2], "Level value");

    std::cout << "    OK typical set command pattern" << std::endl;
}

void testErrorRecoveryPattern()
{
    std::cout << "  Testing error recovery pattern..." << std::endl;

    MockHIDInterface mock;

    // First attempt fails
    mock.simulate_write_error = true;

    std::array<uint8_t, 2> cmd { 0x00, 0x01 };
    auto result1 = mock.write(nullptr, cmd);
    ASSERT_FALSE(result1.hasValue(), "First attempt should fail");

    // Reset and retry succeeds
    mock.simulate_write_error = false;
    auto result2              = mock.write(nullptr, cmd);
    ASSERT_TRUE(result2.hasValue(), "Retry should succeed");

    ASSERT_EQ(2, mock.write_count, "Should record both attempts");

    std::cout << "    OK error recovery pattern" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllHIDInterfaceTests()
{
    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                    HID Interface Tests                           " << std::endl;
    std::cout << "===================================================================\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    FAILED: " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "    ERROR: " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== Mock Interface Basic Tests ===" << std::endl;
    runTest("Write Basic", testMockWriteBasic);
    runTest("Write With Size", testMockWriteWithSize);
    runTest("Write Error", testMockWriteError);
    runTest("Read With Response", testMockReadWithResponse);
    runTest("Read Timeout", testMockReadTimeout);
    runTest("Read Error", testMockReadError);
    runTest("Feature Report", testMockFeatureReport);
    runTest("Input Report", testMockInputReport);
    runTest("Reset", testMockReset);
    runTest("Multiple Writes", testMockMultipleWrites);

    std::cout << "\n=== HID Communication Patterns ===" << std::endl;
    runTest("Battery Query Pattern", testTypicalBatteryQueryPattern);
    runTest("Set Command Pattern", testTypicalSetCommandPattern);
    runTest("Error Recovery Pattern", testErrorRecoveryPattern);

    std::cout << "\n-------------------------------------------------------------------" << std::endl;
    std::cout << "HID Interface Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some HID interface tests failed");
    }
}

} // namespace headsetcontrol::testing

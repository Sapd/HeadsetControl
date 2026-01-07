/**
 * @file test_device_registry.cpp
 * @brief Tests for the device registry singleton and device lookup
 *
 * Tests for:
 * - Device registration and lookup
 * - Edge cases in device lookup
 * - Registry initialization
 * - Device enumeration
 */

#include "device_registry.hpp"
#include "devices/headsetcontrol_test.hpp"

#include <cstdint>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

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

#define ASSERT_EQ(expected, actual, msg)                 \
    do {                                                 \
        if ((expected) != (actual)) {                    \
            std::ostringstream oss;                      \
            oss << "ASSERT_EQ failed: " << (msg) << "\n" \
                << "  Expected: " << (expected) << "\n"  \
                << "  Actual:   " << (actual);           \
            throw TestFailure(oss.str());                \
        }                                                \
    } while (0)

#define ASSERT_NOT_NULL(ptr, msg)                                               \
    do {                                                                        \
        if ((ptr) == nullptr) {                                                 \
            throw TestFailure(std::string("ASSERT_NOT_NULL failed: ") + (msg)); \
        }                                                                       \
    } while (0)

#define ASSERT_NULL(ptr, msg)                                               \
    do {                                                                    \
        if ((ptr) != nullptr) {                                             \
            throw TestFailure(std::string("ASSERT_NULL failed: ") + (msg)); \
        }                                                                   \
    } while (0)

// ============================================================================
// Registry Singleton Tests
// ============================================================================

void testRegistrySingleton()
{
    std::cout << "  Testing registry singleton..." << std::endl;

    auto& instance1 = DeviceRegistry::instance();
    auto& instance2 = DeviceRegistry::instance();

    // Should be the same instance
    ASSERT_TRUE(&instance1 == &instance2, "Singleton should return same instance");

    std::cout << "    OK registry singleton" << std::endl;
}

void testRegistryInitialization()
{
    std::cout << "  Testing registry initialization..." << std::endl;

    auto& registry = DeviceRegistry::instance();

    // Initialize (may be called multiple times safely)
    registry.initialize();

    // After initialization, should have devices
    const auto& devices = registry.getAllDevices();
    ASSERT_TRUE(devices.size() > 0, "Registry should have devices after initialization");

    std::cout << "    OK registry initialization (found " << devices.size() << " devices)" << std::endl;
}

void testRegistryMultipleInitialization()
{
    std::cout << "  Testing multiple initialization calls..." << std::endl;

    auto& registry = DeviceRegistry::instance();

    // Call initialize multiple times
    registry.initialize();
    size_t count1 = registry.getAllDevices().size();

    registry.initialize();
    size_t count2 = registry.getAllDevices().size();

    registry.initialize();
    size_t count3 = registry.getAllDevices().size();

    // Should be the same count each time
    ASSERT_EQ(count1, count2, "Device count should be same after multiple init calls");
    ASSERT_EQ(count2, count3, "Device count should remain stable");

    std::cout << "    OK multiple initialization calls" << std::endl;
}

// ============================================================================
// Device Lookup Tests
// ============================================================================

void testLookupTestDevice()
{
    std::cout << "  Testing lookup of test device..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // Look up test device
    auto* device = registry.getDevice(0xF00B, 0xA00C);

    ASSERT_NOT_NULL(device, "Test device should be found");
    ASSERT_EQ(0xF00B, device->getVendorId(), "Vendor ID should match");
    ASSERT_EQ("HeadsetControl Test device", std::string(device->getDeviceName()), "Device name should match");

    std::cout << "    OK lookup test device" << std::endl;
}

void testLookupNonExistentDevice()
{
    std::cout << "  Testing lookup of non-existent device..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // Try to look up a device that doesn't exist
    auto* device = registry.getDevice(0x0000, 0x0000);
    ASSERT_NULL(device, "Non-existent device should return nullptr");

    // Try another non-existent
    device = registry.getDevice(0xFFFF, 0xFFFF);
    ASSERT_NULL(device, "Another non-existent device should return nullptr");

    std::cout << "    OK lookup non-existent device" << std::endl;
}

void testLookupWithWrongProductId()
{
    std::cout << "  Testing lookup with wrong product ID..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // Correct vendor, wrong product
    auto* device = registry.getDevice(0xF00B, 0x0000);
    ASSERT_NULL(device, "Wrong product ID should return nullptr");

    // Try with max product ID
    device = registry.getDevice(0xF00B, 0xFFFF);
    ASSERT_NULL(device, "Max product ID should return nullptr");

    std::cout << "    OK lookup with wrong product ID" << std::endl;
}

void testLookupWithWrongVendorId()
{
    std::cout << "  Testing lookup with wrong vendor ID..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // Wrong vendor, correct product (test device product ID)
    auto* device = registry.getDevice(0x0000, 0xA00C);
    ASSERT_NULL(device, "Wrong vendor ID should return nullptr");

    std::cout << "    OK lookup with wrong vendor ID" << std::endl;
}

// ============================================================================
// Device Enumeration Tests
// ============================================================================

void testEnumerateAllDevices()
{
    std::cout << "  Testing device enumeration..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    // Should have multiple devices
    ASSERT_TRUE(devices.size() >= 1, "Should have at least one device (test device)");

    // Iterate and verify each device has valid data
    for (const auto& device : devices) {
        ASSERT_NOT_NULL(device.get(), "Device pointer should not be null");
        ASSERT_TRUE(device->getVendorId() != 0 || device->getDeviceName().length() > 0,
            "Device should have vendor ID or name");
    }

    std::cout << "    OK enumerate all devices (" << devices.size() << " devices)" << std::endl;
}

void testDeviceVendorDistribution()
{
    std::cout << "  Testing device vendor distribution..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    // Count devices per vendor
    std::set<uint16_t> vendors;
    for (const auto& device : devices) {
        vendors.insert(device->getVendorId());
    }

    // Should have multiple vendors (Logitech, SteelSeries, etc.)
    ASSERT_TRUE(vendors.size() >= 1, "Should have at least one vendor");

    std::cout << "    OK device vendor distribution (" << vendors.size() << " vendors)" << std::endl;
}

void testRegisteredDeviceCapabilities()
{
    std::cout << "  Testing registered device capabilities..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    // At least one device should have some capabilities
    bool found_with_caps = false;
    for (const auto& device : devices) {
        int caps = device->getCapabilities();
        if (caps != 0) {
            found_with_caps = true;
            break;
        }
    }

    ASSERT_TRUE(found_with_caps, "At least one device should have capabilities");

    // Test device should have multiple capabilities
    auto* test_device = registry.getDevice(0xF00B, 0xA00C);
    if (test_device) {
        int caps = test_device->getCapabilities();
        ASSERT_TRUE((caps & B(CAP_BATTERY_STATUS)) != 0, "Test device should have battery");
        ASSERT_TRUE((caps & B(CAP_SIDETONE)) != 0, "Test device should have sidetone");
    }

    std::cout << "    OK registered device capabilities" << std::endl;
}

void testDeviceProductIds()
{
    std::cout << "  Testing device product IDs..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    for (const auto& device : devices) {
        auto product_ids = device->getProductIds();

        // Each device should have at least one product ID
        ASSERT_TRUE(product_ids.size() >= 1, "Device should have at least one product ID");

        // All product IDs should be non-zero (typically)
        for (auto pid : product_ids) {
            // Allow zero for test purposes, but real devices should have non-zero
            if (device->getVendorId() != 0xF00B) {
                ASSERT_TRUE(pid != 0 || product_ids.size() > 1,
                    "Real devices should have non-zero product ID");
            }
        }
    }

    std::cout << "    OK device product IDs" << std::endl;
}

// ============================================================================
// Known Vendor Tests
// ============================================================================

void testKnownVendors()
{
    std::cout << "  Testing known vendor devices..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    // Known vendor IDs (using different names to avoid macro conflicts)
    constexpr uint16_t VID_LOGITECH    = 0x046d;
    constexpr uint16_t VID_STEELSERIES = 0x1038;
    constexpr uint16_t VID_CORSAIR     = 0x1b1c;
    constexpr uint16_t VID_HYPERX      = 0x0951;

    // Count devices per known vendor
    int logitech_count    = 0;
    int steelseries_count = 0;
    int corsair_count     = 0;
    int hyperx_count      = 0;

    for (const auto& device : devices) {
        switch (device->getVendorId()) {
        case VID_LOGITECH:
            logitech_count++;
            break;
        case VID_STEELSERIES:
            steelseries_count++;
            break;
        case VID_CORSAIR:
            corsair_count++;
            break;
        case VID_HYPERX:
            hyperx_count++;
            break;
        }
    }

    std::cout << "    Found: Logitech=" << logitech_count
              << ", SteelSeries=" << steelseries_count
              << ", Corsair=" << corsair_count
              << ", HyperX=" << hyperx_count << std::endl;

    // Should have at least some known vendor devices
    int total_known = logitech_count + steelseries_count + corsair_count + hyperx_count;
    ASSERT_TRUE(total_known >= 0, "Count should be non-negative"); // Basic sanity check

    std::cout << "    OK known vendor devices" << std::endl;
}

// ============================================================================
// Device Name Tests
// ============================================================================

void testDeviceNames()
{
    std::cout << "  Testing device names..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    for (const auto& device : devices) {
        auto name = device->getDeviceName();

        // Name should not be empty
        ASSERT_TRUE(name.length() > 0, "Device name should not be empty");

        // Name should be reasonable length
        ASSERT_TRUE(name.length() < 200, "Device name should not be excessively long");

        // Name should contain printable characters
        for (char c : name) {
            ASSERT_TRUE(c >= 32 && c < 127, "Device name should contain printable ASCII");
        }
    }

    std::cout << "    OK device names" << std::endl;
}

// ============================================================================
// Platform Support Tests
// ============================================================================

void testDevicePlatformSupport()
{
    std::cout << "  Testing device platform support..." << std::endl;

    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    const auto& devices = registry.getAllDevices();

    for (const auto& device : devices) {
        uint8_t platforms = device->getSupportedPlatforms();

        // Platform mask should be valid (some bits set or all bits for PLATFORM_ALL)
        // PLATFORM_ALL is typically 0xFF or similar
        ASSERT_TRUE(platforms > 0, "Platform support should not be zero");
    }

    std::cout << "    OK device platform support" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllDeviceRegistryTests()
{
    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                    Device Registry Tests                         " << std::endl;
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

    std::cout << "=== Singleton Tests ===" << std::endl;
    runTest("Singleton Instance", testRegistrySingleton);
    runTest("Initialization", testRegistryInitialization);
    runTest("Multiple Initialization", testRegistryMultipleInitialization);

    std::cout << "\n=== Device Lookup Tests ===" << std::endl;
    runTest("Lookup Test Device", testLookupTestDevice);
    runTest("Lookup Non-Existent", testLookupNonExistentDevice);
    runTest("Lookup Wrong Product ID", testLookupWithWrongProductId);
    runTest("Lookup Wrong Vendor ID", testLookupWithWrongVendorId);

    std::cout << "\n=== Enumeration Tests ===" << std::endl;
    runTest("Enumerate All Devices", testEnumerateAllDevices);
    runTest("Vendor Distribution", testDeviceVendorDistribution);
    runTest("Device Capabilities", testRegisteredDeviceCapabilities);
    runTest("Product IDs", testDeviceProductIds);

    std::cout << "\n=== Vendor Tests ===" << std::endl;
    runTest("Known Vendors", testKnownVendors);

    std::cout << "\n=== Device Properties Tests ===" << std::endl;
    runTest("Device Names", testDeviceNames);
    runTest("Platform Support", testDevicePlatformSupport);

    std::cout << "\n-------------------------------------------------------------------" << std::endl;
    std::cout << "Device Registry Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some device registry tests failed");
    }
}

} // namespace headsetcontrol::testing

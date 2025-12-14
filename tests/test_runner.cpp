/**
 * @file test_runner.cpp
 * @brief Simple test runner for HeadsetControl unit tests
 *
 * This is a minimal test runner that demonstrates the mock HID interface.
 * For production use, consider integrating Google Test or Catch2.
 *
 * Build with: cmake -DBUILD_UNIT_TESTS=ON ..
 * Run with: make test
 */

#include "device_registry.hpp"
#include "headsetcontrol.hpp"
#include <cstdlib>
#include <iostream>

// Forward declarations for test suites
namespace headsetcontrol::testing {
void runAllOutputFormatTests();
void runAllCliOutputTests();
void runAllLibraryLinkageTests();
void runAllUtilityTests();
void runAllEdgeCaseTests();
void runAllHIDInterfaceTests();
void runAllDeviceRegistryTests();
void runAllStringEscapingTests();
void runAllLibraryApiTests();
void runAllProtocolTests();
}

int main()
{
    std::cout << "HeadsetControl Unit Test Runner" << std::endl;
    std::cout << "================================" << std::endl;

    // Initialize device registry for library linkage tests
    headsetcontrol::DeviceRegistry::instance().initialize();

    try {
        // Run output format tests
        headsetcontrol::testing::runAllOutputFormatTests();

        // Run CLI output integration tests
        headsetcontrol::testing::runAllCliOutputTests();

        // Run library linkage tests
        headsetcontrol::testing::runAllLibraryLinkageTests();

        // Run utility function tests
        headsetcontrol::testing::runAllUtilityTests();

        // Run edge case tests
        headsetcontrol::testing::runAllEdgeCaseTests();

        // Run HID interface tests
        headsetcontrol::testing::runAllHIDInterfaceTests();

        // Run device registry tests
        headsetcontrol::testing::runAllDeviceRegistryTests();

        // Run string escaping tests
        headsetcontrol::testing::runAllStringEscapingTests();

        // Run library API tests
        headsetcontrol::testing::runAllLibraryApiTests();

        // Run protocol tests
        headsetcontrol::testing::runAllProtocolTests();

        std::cout << "\n====================================================================" << std::endl;
        std::cout << "                    All tests passed successfully!                  " << std::endl;
        std::cout << "====================================================================" << std::endl;

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cerr << "❌ Test failed with exception:" << std::endl;
        std::cerr << "   " << e.what() << std::endl;
        std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

        return EXIT_FAILURE;

    } catch (...) {
        std::cerr << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

        return EXIT_FAILURE;
    }
}

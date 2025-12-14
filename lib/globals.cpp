/**
 * @file globals.cpp
 * @brief Global state management for HeadsetControl library
 *
 * Provides internal storage and accessor functions for library-wide settings.
 * The CLI and other consumers should use the public API functions to access these.
 */

namespace headsetcontrol {
namespace detail {

    // Default timeout for HID operations (milliseconds)
    static int g_device_timeout = 5000;

    // Test profile selector for HeadsetControlTest device
    // 0 = normal operation
    // 1 = error conditions
    // 2 = charging state
    // 3 = basic battery info only
    // 5 = timeout simulation
    // 10 = minimal capabilities
    static int g_test_profile = 0;

    int getDeviceTimeout()
    {
        return g_device_timeout;
    }

    void setDeviceTimeout(int timeout_ms)
    {
        g_device_timeout = timeout_ms;
    }

    int getTestProfile()
    {
        return g_test_profile;
    }

    void setTestProfile(int profile)
    {
        g_test_profile = profile;
    }

} // namespace detail
} // namespace headsetcontrol

// Legacy C-compatible global variables for backward compatibility within the library
// These are used by device implementations that reference hsc_device_timeout and test_profile
int hsc_device_timeout = 5000;
int test_profile       = 0;

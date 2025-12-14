#include "hid_device.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace headsetcontrol {

// C function wrappers that delegate to C++ methods
static int c_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // Get the HIDDevice* from the device struct
    // This will be stored in a custom field we'll add
    return HSC_ERROR; // Placeholder
}

struct device* HIDDevice::toCDevice()
{
    if (c_device_cache) {
        return c_device_cache.get();
    }

    // Allocate new C device struct
    c_device_cache     = std::make_unique<struct device>();
    struct device* dev = c_device_cache.get();

    // Zero-initialize
    std::memset(dev, 0, sizeof(struct device));

    // Fill in basic info
    dev->idVendor = getVendorId();

    // Convert product IDs vector to static array
    // Note: This requires product IDs to be stored somewhere persistent
    // For now, we'll need to keep them as class members
    auto product_ids = getProductIds();
    // This is a limitation - the C API expects a pointer to a static array
    // We'll handle this in derived classes

    dev->numIdProducts = product_ids.size();

    // Copy device name safely using modern C++
    std::string_view name = getDeviceName();
    size_t copy_len       = std::min(name.size(), sizeof(dev->device_name) - 1);
    std::copy_n(name.data(), copy_len, dev->device_name);
    dev->device_name[copy_len] = '\0';

    // Set platform support
    dev->supported_platforms = getSupportedPlatforms();

    // Set capabilities
    dev->capabilities = getCapabilities();

    // Set capability details
    for (int i = 0; i < NUM_CAPABILITIES; i++) {
        dev->capability_details[i] = getCapabilityDetail(static_cast<enum capabilities>(i));
    }

    // Set function pointers
    // Note: These need to capture 'this' pointer somehow
    // This is the tricky part - we need to store the C++ object pointer
    // and retrieve it in the C callbacks

    // For now, we'll use a simpler approach:
    // each derived class will create its own C device struct
    // with proper function pointers

    return dev;
}

} // namespace headsetcontrol

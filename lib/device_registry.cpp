#include "device_registry.hpp"

// Corsair devices
#include "devices/corsair_void_rich.hpp"
#include "devices/corsair_void_v2w.hpp"

// Logitech devices with HIDPPDevice protocol template
#include "devices/logitech_g432.hpp"
#include "devices/logitech_g533.hpp"
#include "devices/logitech_g535.hpp"
#include "devices/logitech_g633_g933_935.hpp"
#include "devices/logitech_g930.hpp"
#include "devices/logitech_gpro.hpp"
#include "devices/logitech_zone_wired.hpp"

// SteelSeries devices with SteelSeries protocol templates
#include "devices/steelseries_arctis_1.hpp"
#include "devices/steelseries_arctis_7.hpp"
#include "devices/steelseries_arctis_7_plus.hpp"
#include "devices/steelseries_arctis_9.hpp"
#include "devices/steelseries_arctis_nova_3.hpp"
#include "devices/steelseries_arctis_nova_3p_wireless.hpp"
#include "devices/steelseries_arctis_nova_5.hpp"
#include "devices/steelseries_arctis_nova_7.hpp"
#include "devices/steelseries_arctis_nova_7p.hpp"
#include "devices/steelseries_arctis_nova_pro_wireless.hpp"
#include "devices/steelseries_arctis_pro_wireless.hpp"

// HyperX devices
#include "devices/hyperx_cloud_2_wireless.hpp"
#include "devices/hyperx_cloud_3.hpp"
#include "devices/hyperx_cloud_alpha_wireless.hpp"
#include "devices/hyperx_cloud_flight.hpp"

// Roccat devices
#include "devices/roccat_elo_7_1_air.hpp"
#include "devices/roccat_elo_7_1_usb.hpp"

// Audeze devices
#include "devices/audeze_maxwell.hpp"

// Test device
#include "devices/headsetcontrol_test.hpp"

namespace headsetcontrol {

DeviceRegistry& DeviceRegistry::instance()
{
    static DeviceRegistry registry;
    return registry;
}

// extern "C" bridge functions for C code compatibility
extern "C" {

/**
 * @brief Bridge function to get C++ device from C code
 *
 * This allows the C registry to delegate to the C++ registry
 */
HIDDevice* get_cpp_device(uint16_t vendor_id, uint16_t product_id)
{
    return DeviceRegistry::instance().getDevice(vendor_id, product_id);
}

/**
 * @brief Initialize the C++ device registry
 */
void init_cpp_devices()
{
    DeviceRegistry::instance().initialize();
}

} // extern "C"

void DeviceRegistry::initialize()
{
    // Use call_once to ensure thread-safe initialization
    std::call_once(init_flag_, [this]() {
        // Register all device implementations
        // Each device is managed by a unique_ptr for automatic cleanup

        // Logitech devices (using HIDPPDevice protocol template)
        registerDevice(std::make_unique<LogitechG533>());
        registerDevice(std::make_unique<LogitechG535>());
        registerDevice(std::make_unique<LogitechG633Family>());
        registerDevice(std::make_unique<LogitechG432>());
        registerDevice(std::make_unique<LogitechG930>());
        registerDevice(std::make_unique<LogitechGPro>());
        registerDevice(std::make_unique<LogitechZoneWired>());

        // Corsair devices
        registerDevice(std::make_unique<CorsairVoidRich>());
        registerDevice(std::make_unique<CorsairVoidV2W>());

        // SteelSeries devices (using SteelSeries protocol templates)
        registerDevice(std::make_unique<SteelSeriesArctis1>());
        registerDevice(std::make_unique<SteelSeriesArctis7>());
        registerDevice(std::make_unique<SteelSeriesArctis9>());
        registerDevice(std::make_unique<SteelSeriesArctisProWireless>());
        registerDevice(std::make_unique<SteelSeriesArctisNova3>());
        registerDevice(std::make_unique<SteelSeriesArctisNova5>());
        registerDevice(std::make_unique<SteelSeriesArctisNova7>());
        registerDevice(std::make_unique<SteelSeriesArctisNova7P>());
        registerDevice(std::make_unique<SteelSeriesArctis7Plus>());
        registerDevice(std::make_unique<SteelSeriesArctisNovaProWireless>());
        registerDevice(std::make_unique<SteelSeriesArctisNova3PWireless>());

        // HyperX devices
        registerDevice(std::make_unique<HyperXCloudAlphaWireless>());
        registerDevice(std::make_unique<HyperXCloudFlight>());
        registerDevice(std::make_unique<HyperXCloud2Wireless>());
        registerDevice(std::make_unique<HyperXCloud3>());

        // Roccat devices
        registerDevice(std::make_unique<RoccatElo71Air>());
        registerDevice(std::make_unique<RoccatElo71USB>());

        // Audeze devices
        registerDevice(std::make_unique<AudezeMaxwell>());

        // Test device
        registerDevice(std::make_unique<HeadsetControlTest>());
    });
}

HIDDevice* DeviceRegistry::getDevice(uint16_t vendor_id, uint16_t product_id)
{
    for (auto& device : devices_) {
        // Check if vendor ID matches
        if (device->getVendorId() != vendor_id) {
            continue;
        }

        // Check if any of the device's product IDs match
        auto product_ids = device->getProductIds();
        for (uint16_t pid : product_ids) {
            if (pid == product_id) {
                return device.get();
            }
        }
    }

    return nullptr; // Device not found
}

const std::vector<std::unique_ptr<HIDDevice>>& DeviceRegistry::getAllDevices() const
{
    return devices_;
}

void DeviceRegistry::registerDevice(std::unique_ptr<HIDDevice> device)
{
    devices_.push_back(std::move(device));
}

} // namespace headsetcontrol

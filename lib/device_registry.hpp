#pragma once

#include "devices/hid_device.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace headsetcontrol {

/**
 * @brief Central registry for all supported headset devices
 *
 * This class manages the collection of all device implementations
 * and provides device lookup by USB vendor/product ID.
 *
 * @note Thread Safety:
 * - instance(): Thread-safe (Meyer's singleton)
 * - initialize(): Thread-safe (uses std::call_once internally)
 * - getDevice(): Thread-safe after initialization (read-only)
 * - getAllDevices(): Thread-safe after initialization (read-only)
 * - registerDevice(): NOT thread-safe, should only be called during initialize()
 */
class DeviceRegistry {
public:
    /**
     * @brief Get the singleton instance
     * @note Thread-safe
     */
    static DeviceRegistry& instance();

    /**
     * @brief Initialize the registry with all device implementations
     *
     * This should be called once at startup to register all devices.
     * Multiple calls are safe - initialization only happens once.
     *
     * @note Thread-safe (uses std::call_once internally)
     */
    void initialize();

    /**
     * @brief Find a device by USB vendor and product ID
     *
     * @param vendor_id USB vendor ID
     * @param product_id USB product ID
     * @return Pointer to device implementation, or nullptr if not found
     *
     * @note Thread-safe after initialize() has completed
     */
    HIDDevice* getDevice(uint16_t vendor_id, uint16_t product_id);

    /**
     * @brief Get all registered devices
     *
     * @return Vector of all device implementations
     *
     * @note Thread-safe after initialize() has completed
     */
    const std::vector<std::unique_ptr<HIDDevice>>& getAllDevices() const;

    /**
     * @brief Register a device implementation
     *
     * @param device Device implementation to register
     *
     * @warning NOT thread-safe. Only call during initialization.
     */
    void registerDevice(std::unique_ptr<HIDDevice> device);

private:
    DeviceRegistry()  = default;
    ~DeviceRegistry() = default;

    // Prevent copying
    DeviceRegistry(const DeviceRegistry&)            = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

    std::vector<std::unique_ptr<HIDDevice>> devices_;
    std::once_flag init_flag_;
};

} // namespace headsetcontrol

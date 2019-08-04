#pragma once

#include <hidapi.h>
#include <stdint.h>

#define VENDOR_CORSAIR  0x1b1c
#define VENDOR_LOGITECH 0x046d
#define VENDOR_STEELSERIES 0x1038

/** @brief A list of all features settable/queryable
 *         for headsets
 */
enum capabilities {
    CAP_SIDETONE            = 1,
    CAP_BATTERY_STATUS      = 2,
    CAP_NOTIFICATION_SOUND  = 4,
    CAP_LIGHTS              = 8,
};

/** @brief Flags for battery status
 */
enum battery_status {
    BATTERY_LOADING         = 65535
};

/** @brief Defines the basic data of a device
 *
 *  Also defines function pointers for using supported features
 */
struct device
{
    /// USB Vendor id
    uint16_t idVendor;
    /// USB Product id
    uint16_t idProduct;
    /// Interface ID - zero means first enumerated interface!
    int idInterface;
 
    /// Name of device, used as information for the user
    char device_name[32];
 
    /// Bitmask of currently supported features the software can currently handle
    int capabilities;
    
    /** @brief Function pointer for setting headset sidetone
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param device_handle The libUSB handle. Must be the same
     *                       device as defined here (same ids)
     *  @param num           Level of the sidetone, between 0 - 128
     */
    int (*send_sidetone)(hid_device *hid_device, uint8_t num);
    
    int (*request_battery)(hid_device *hid_device);
    
    int (*notifcation_sound)(hid_device *hid_device, uint8_t soundid);

    int (*switch_lights)(hid_device *hid_device, uint8_t on);
};


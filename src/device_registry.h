#pragma once

#include "device.h"

/** @brief Inits the data of all supported devices
 */
void init_devices();

/** @brief Provides data to a struct device when a supported device is found
 *
 *  Only matches when idVendor and idProduct correspond to a supported device.
 *
 *  @param device_found When a device is found, the struct pointed to by device_found
 *                      is filled with it's device data
 *  @param idVendor     USB Vendor id to search for
 *  @param idProduct    USB Product id to search for
 *  @return 0 when a device was found, 1 otherwise
 */
int get_device(struct device* device_found, uint16_t idVendor, uint16_t idProduct);

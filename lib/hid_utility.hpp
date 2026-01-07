#pragma once

#include <hidapi.h>

#include <cstdint>
#include <optional>
#include <string>

namespace headsetcontrol {

/**
 *  @brief Helper fetching a HID path for a given device description.
 *
 *  This is a convenience function iterating over connected USB devices and
 *  returning the HID path belonging to the device described in the parameters.
 *
 *  @param vid The device vendor ID.
 *  @param pid The device product ID.
 *  @param iid The device interface ID. A value of zero means to take the
 *             first enumerated (sub-) device.
 *  @param usagepageid The device usage page id, see usageid
 *  @param usageid      The device usage id in context to usagepageid.
 *                      Only used on Windows currently, and when not 0;
 *                      ignores iid when set
 *
 *  @return HID path string, or nullopt on failure
 */
std::optional<std::string> get_hid_path(uint16_t vid, uint16_t pid, int iid, uint16_t usagepageid, uint16_t usageid);

/**
 *  @brief Helper closing a HID device handle.
 */
void close_hid_device(hid_device* handle);

} // namespace headsetcontrol

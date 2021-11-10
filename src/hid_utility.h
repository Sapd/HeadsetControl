#pragma once

#include <hidapi.h>

#include <inttypes.h>
#include <stdlib.h>

/**
 *  @brief Helper fetching a copied HID path for a given device description.
 *
 *  This is a convenience function iterating over connected USB devices and
 *  returning a copy of the HID path belonging to the device described in the
 *  parameters.
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
 *  @return copy of the HID path or NULL on failure
 */
char* get_hid_path(uint16_t vid, uint16_t pid, int iid, uint16_t usagepageid, uint16_t usageid);

/**
 *  Helper freeing HID data and terminating HID usage.
 */
/* This function is explicitly called terminate_hid to avoid HIDAPI clashes. */
void terminate_hid(hid_device** handle, char** path);
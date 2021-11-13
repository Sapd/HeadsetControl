#include "hid_utility.h"

#include <stdio.h>
#include <string.h>

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
 *  @return copy of the HID path or NULL on failure (copy must be freed)
 */
char* get_hid_path(uint16_t vid, uint16_t pid, int iid, uint16_t usagepageid, uint16_t usageid)
{
    char* ret = NULL;

    struct hid_device_info* devs = hid_enumerate(vid, pid);

    if (!devs) {
        fprintf(stderr, "HID enumeration failure.\n");
        return ret;
    }

    // usageid is more specific to interface id, so we try it first
    // It is a good idea, to do it on all platforms, however googling shows
    //      that older versions of hidapi have a bug where the value is not correctly
    //      set on non-Windows.
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    if (usageid && usagepageid) // ignore when one of them 0
    {
        struct hid_device_info* cur_dev = devs;
        while (cur_dev) {
            if (cur_dev->usage_page == usagepageid && cur_dev->usage == usageid) {
                ret = strdup(cur_dev->path);

                if (!ret) {
                    fprintf(stderr, "Unable to copy HID path for usageid.\n");
                    hid_free_enumeration(devs);
                    devs = NULL;
                    return ret;
                }

                break;
            }

            cur_dev = cur_dev->next;
        }
    }
#else
    // ignore unused parameter warning
    (void)(usageid);
    (void)(usagepageid);
#endif

    if (ret == NULL) // only when we didn't yet found something
    {
        struct hid_device_info* cur_dev = devs;
        while (cur_dev) {
            if (!iid || cur_dev->interface_number == iid) {
                ret = strdup(cur_dev->path);

                if (!ret) {
                    fprintf(stderr, "Unable to copy HID path.\n");
                    hid_free_enumeration(devs);
                    devs = NULL;
                    return ret;
                }

                break;
            }

            cur_dev = cur_dev->next;
        }
    }

    hid_free_enumeration(devs);
    devs = NULL;

    return ret;
}

/**
 *  Helper freeing HID data and terminating HID usage.
 */
/* This function is explicitly called terminate_hid to avoid HIDAPI clashes. */
void terminate_hid(hid_device** handle, char** path)
{
    if (handle) {
        if (*handle) {
            hid_close(*handle);
        }

        *handle = NULL;
    }

    if (path) {
        free(*path);
    }

    hid_exit();
}
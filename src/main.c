/***
    Copyright (C) 2016-2018 Denis Arnst (Sapd) <https://github.com/Sapd>

    This file is part of HeadsetControl.

    HeadsetControl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    HeadsetControl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with HeadsetControl.  If not, see <http://www.gnu.org/licenses/>.
***/

#include "device_registry.h"
#include "utility.h"

#include <hidapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// describes the device, when a headset was found
static struct device device_found;

// 0=false; 1=true
static int short_output = 0;

#define PRINT(...) { if (!short_output) \
    {printf(__VA_ARGS__);}}

/**
 *  This function iterates through all HID devices.
 *
 *  @return 0 when a supported device is found
 */
int find_device()
{
    struct hid_device_info *devs, *cur_dev;
    int found;
    devs = hid_enumerate(0x0, 0x0);
    cur_dev = devs;
    while (cur_dev) {
        found = get_device(&device_found, cur_dev->vendor_id, cur_dev->product_id);

        if (found == 0)
        {
            PRINT("Found %s!\n", device_found.device_name);
            break;
        }

        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);


    return found;
}

/**
 *  Helper freeing HID data and terminating HID usage.
 */
/* This function is explicitly called terminate_hid to avoid HIDAPI clashes. */
static void terminate_hid(hid_device **handle, char **path)
{
    if (handle)
    {
        if (*handle)
        {
            hid_close(*handle);
        }

        *handle = NULL;
    }

    if (path)
    {
        SAFE_FREE(*path);
    }

    hid_exit();
}

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
 *
 *  @return copy of the HID path or NULL on failure
 */
static char* get_hid_path(uint16_t vid, uint16_t pid, int iid)
{
    char *ret = NULL;

    struct hid_device_info *devs = hid_enumerate(vid, pid);

    if (!devs)
    {
        PRINT("HID enumeration failure.\n");
        return ret;
    }

    struct hid_device_info *cur_dev = devs;
    while (cur_dev)
    {
        if (!iid || cur_dev->interface_number == iid)
        {
            ret = strdup(cur_dev->path);

            if (!ret)
            {
                PRINT("Unable to copy HID path.\n");
                hid_free_enumeration(devs);
                devs = NULL;
                return ret;
            }

            break;
        }

        cur_dev = cur_dev->next;
    }

    hid_free_enumeration(devs);
    devs = NULL;

    return ret;
}

int main(int argc, char *argv[])
{
    {
        /* Avoid compiler warning. Pretty stupid. */
        (void) map(0, 0, 1, 0, 0);
    }

    int c;

    int sidetone_loudness = -1;
    int request_battery = 0;
    int notification_sound = -1;
    int lights = -1;


    while ((c = getopt(argc, argv, "bchs:n:l:")) != -1)
    {
        switch(c) {
            case 'b':
                request_battery = 1;
                break;
            case 's':
                sidetone_loudness = strtol(optarg, NULL, 10);

                if (sidetone_loudness > 128 || sidetone_loudness < 0)
                {
                    printf("Usage: %s -s 0-128\n", argv[0]);
                    return 1;
                }
                break;
            case 'n':
                notification_sound = strtol(optarg, NULL, 10);

                if (notification_sound < 0 || notification_sound > 1)
                {
                    printf("Usage: %s -n 0|1\n", argv[0]);
                    return 1;
                }
                break;
            case 'c':
                short_output = 1;
                break;
            case 'l':
                lights = strtol(optarg, NULL, 10);
                if (lights < 0 || lights > 1)
                {
                    printf("Usage: %s -l 0|1\n", argv[0]);
                    return 1;
                }
                break;
            case 'h':
                printf("Headsetcontrol written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd\n\n");
                printf("Parameters\n");
                printf("  -s level\tSets sidetone, level must be between 0 and 128\n");
                printf("  -b\t\tChecks the battery level\n");
                printf("  -n soundid\tMakes the headset play a notifiation\n");
                printf("  -l 0|1\tSwitch lights (0 = off, 1 = on)\n");
                printf("  -c\t\tCut unnecessary output \n");

                printf("\n");
                return 0;
            default:
                printf("Invalid argument %c (Programming error)\n", c);
                return 1;
        }

    }

    if (!short_output)
    {
        for (int index = optind; index < argc; index++)
            printf("Non-option argument %s\n", argv[index]);
    }
    // Init all information of supported devices
    init_devices();


    // Look for a supported device
    int headset_available = find_device();
    if (headset_available != 0)
    {
        PRINT("No supported headset found\n");
        return 1;
    }
    PRINT("\n");

    hid_device *device_handle = NULL;
    char *hid_path = get_hid_path(device_found.idVendor, device_found.idProduct, device_found.idInterface);

    if (!hid_path)
    {
        PRINT("Requested/supported HID device not found or system error.\n");
        terminate_hid(&device_handle, &hid_path);
        return 1;
    }

    device_handle = hid_open_path(hid_path);

    // Open libusb device
    if (device_handle == NULL)
    {
        PRINT("Couldn't open device.\n");
        terminate_hid(&device_handle, &hid_path);
        return 1;
    }

    int ret = 0;
    // Set all features the user wants us to set
    if (sidetone_loudness != -1)
    {
        if ((device_found.capabilities & CAP_SIDETONE) == 0)
        {
            PRINT("Error: This headset doesn't support sidetone\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.send_sidetone(device_handle, sidetone_loudness);

        if (ret < 0)
        {
            PRINT("Failed to set sidetone. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        else
        {
            PRINT("Success!\n");
        }
    }

    if (lights != -1)
    {
        if ((device_found.capabilities & CAP_LIGHTS) == 0)
        {
            PRINT("Error: This headset doesn't support light switching\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        ret = device_found.switch_lights(device_handle, lights);

        if (ret < 0)
        {
            PRINT("Failed to switch lights. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        else
        {
            PRINT("Success!\n");
        }
    }

    if (notification_sound != -1)
    {
        if ((device_found.capabilities & CAP_NOTIFICATION_SOUND) == 0)
        {
            PRINT("Error: This headset doesn't support notification sound\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.notifcation_sound(device_handle, notification_sound);

        if (ret < 0)
        {
            PRINT("Failed to send notification sound. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        else
        {
            PRINT("Success!\n");
        }
    }

    if (request_battery == 1)
    {
        if ((device_found.capabilities & CAP_BATTERY_STATUS) == 0)
        {
            PRINT("Error: This headset doesn't support battery status\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.request_battery(device_handle);

        if (ret < 0)
        {
            PRINT("Failed to request battery. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        if (ret == BATTERY_LOADING)
        {
            if (!short_output)
                printf("Battery: Loading\n");
            else
                printf("-1");
        }
        else
        {
            if (!short_output)
                printf("Battery: %d%%\n", ret);
            else
                printf("%d", ret);
        }
    }

    if (argc <= 1)
    {
        printf("You didn't set any arguments, so nothing happend.\nType %s -h for help.\n", argv[0]);
    }

    terminate_hid(&device_handle, &hid_path);

    return 0;
}

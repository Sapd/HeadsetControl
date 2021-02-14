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
#include <string.h>
#include <unistd.h>

// describes the device, when a headset was found
static struct device device_found;

// 0=false; 1=true
static int short_output = 0;

#define PRINT_INFO(...)          \
    {                            \
        if (!short_output) {     \
            printf(__VA_ARGS__); \
        }                        \
    }

/**
 *  This function iterates through all HID devices.
 *
 *  @return 0 when a supported device is found
 */
int find_device()
{
    struct hid_device_info* devs;
    struct hid_device_info* cur_dev;
    int found = -1;
    devs = hid_enumerate(0x0, 0x0);
    cur_dev = devs;
    while (cur_dev) {
        found = get_device(&device_found, cur_dev->vendor_id, cur_dev->product_id);

        if (found == 0) {
            PRINT_INFO("Found %s!\n", device_found.device_name);
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
static void terminate_hid(hid_device** handle, char** path)
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
static char* get_hid_path(uint16_t vid, uint16_t pid, int iid, uint16_t usagepageid, uint16_t usageid)
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

int main(int argc, char* argv[])
{
    int c;

    long sidetone_loudness = -1;
    long request_battery = 0;
    long notification_sound = -1;
    long lights = -1;
    long inactive_time = -1;
    long request_chatmix = 0;
    long voice_prompts = -1;
    long rotate_to_mute = -1;

    while ((c = getopt(argc, argv, "bchs:n:l:i:mv:r:")) != -1) {
        switch (c) {
        case 'b':
            request_battery = 1;
            break;
        case 'm':
            request_chatmix = 1;
            break;
        case 's':
            sidetone_loudness = strtol(optarg, NULL, 10);

            if (sidetone_loudness > 128 || sidetone_loudness < 0) {
                printf("Usage: %s -s 0-128\n", argv[0]);
                return 1;
            }
            break;
        case 'n':
            notification_sound = strtol(optarg, NULL, 10);

            if (notification_sound < 0 || notification_sound > 1) {
                printf("Usage: %s -n 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'c':
            short_output = 1;
            break;
        case 'l':
            lights = strtol(optarg, NULL, 10);
            if (lights < 0 || lights > 1) {
                printf("Usage: %s -l 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'i':
            inactive_time = strtol(optarg, NULL, 10);

            if (inactive_time > 90 || inactive_time < 0) {
                printf("Usage: %s -s 0-90, 0 is off\n", argv[0]);
                return 1;
            }
            break;
        case 'v':
            voice_prompts = strtol(optarg, NULL, 10);
            if (voice_prompts < 0 || voice_prompts > 1) {
                printf("Usage: %s -v 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'r':
            rotate_to_mute = strtol(optarg, NULL, 10);
            if (rotate_to_mute < 0 || rotate_to_mute > 1) {
                printf("Usage: %s -r 0|1\n", argv[0]);
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
            printf("  -i time\tSets inactive time in minutes, time must be between 0 and 90, 0 disables the feature.\n");
            printf("  -m\t\tRetrieves the current chat-mix-dial level setting\n");
            printf("  -v 0|1\tTurn voice prompts on or off (0 = off, 1 = on)\n");
            printf("  -r 0|1\tTurn rotate to mute feature on or off (0 = off, 1 = on)\n");

            printf("\n");
            return 0;
        default:
            printf("Invalid argument %c\n", c);
            return 1;
        }
    }

    if (!short_output) {
        for (int index = optind; index < argc; index++)
            printf("Non-option argument %s\n", argv[index]);
    }

    // Init all information of supported devices
    init_devices();

    // Look for a supported device
    int headset_available = find_device();
    if (headset_available != 0) {
        fprintf(stderr, "No supported headset found\n");
        return 1;
    }
    PRINT_INFO("\n");

    hid_device* device_handle = NULL;
    char* hid_path = get_hid_path(device_found.idVendor, device_found.idProduct, device_found.idInterface, device_found.idUsagePage, device_found.idUsage);

    if (!hid_path) {
        fprintf(stderr, "Requested/supported HID device not found or system error.\n");
        terminate_hid(&device_handle, &hid_path);
        return 1;
    }

    device_handle = hid_open_path(hid_path);

    // Open libusb device
    if (device_handle == NULL) {
        fprintf(stderr, "Couldn't open device.\n");
        terminate_hid(&device_handle, &hid_path);
        return 1;
    }

    int ret = 0;
    // Set all features the user wants us to set
    if (sidetone_loudness != -1) {
        if ((device_found.capabilities & CAP_SIDETONE) == 0) {
            fprintf(stderr, "Error: This headset doesn't support sidetone\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.send_sidetone(device_handle, sidetone_loudness);

        if (ret < 0) {
            fprintf(stderr, "Failed to set sidetone. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        PRINT_INFO("Success!\n");
    }

    if (lights != -1) {
        if ((device_found.capabilities & CAP_LIGHTS) == 0) {
            fprintf(stderr, "Error: This headset doesn't support light switching\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        ret = device_found.switch_lights(device_handle, lights);

        if (ret < 0) {
            fprintf(stderr, "Failed to switch lights. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        PRINT_INFO("Success!\n");
    }

    if (notification_sound != -1) {
        if ((device_found.capabilities & CAP_NOTIFICATION_SOUND) == 0) {
            fprintf(stderr, "Error: This headset doesn't support notification sound\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.notifcation_sound(device_handle, notification_sound);

        if (ret < 0) {
            fprintf(stderr, "Failed to send notification sound. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        PRINT_INFO("Success!\n");
    }

    if (request_battery == 1) {
        if ((device_found.capabilities & CAP_BATTERY_STATUS) == 0) {
            fprintf(stderr, "Error: This headset doesn't support battery status\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.request_battery(device_handle);

        if (ret < 0) {
            fprintf(stderr, "Failed to request battery. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        if (ret == BATTERY_CHARGING) {
            if (!short_output)
                printf("Battery: Charging\n");
            else
                printf("-1");
        } else if (ret == BATTERY_UNAVAILABLE) {
            if (!short_output)
                printf("Battery: Unavailable\n");
            else
                printf("-2");
        } else {
            if (!short_output)
                printf("Battery: %d%%\n", ret);
            else
                printf("%d", ret);
        }
    }

    if (inactive_time != -1) {
        if ((device_found.capabilities & CAP_INACTIVE_TIME) == 0) {
            fprintf(stderr, "Error: This headset doesn't support setting inactive time\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.send_inactive_time(device_handle, inactive_time);

        if (ret < 0) {
            fprintf(stderr, "Failed to set inactive time. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        PRINT_INFO("Successfully set inactive time to %ld minutes!\n", inactive_time);
    }

    if (request_chatmix == 1) {
        if ((device_found.capabilities & CAP_CHATMIX_STATUS) == 0) {
            fprintf(stderr, "Error: This headset doesn't support chat-mix level\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        ret = device_found.request_chatmix(device_handle);

        if (ret < 0) {
            fprintf(stderr, "Failed to request chat-mix. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        if (!short_output)
            printf("Chat-Mix: %d\n", ret);
        else
            printf("%d", ret);
    }

    if (voice_prompts != -1) {
        if ((device_found.capabilities & CAP_VOICE_PROMPTS) == 0) {
            fprintf(stderr, "Error: This headset doesn't support voice prompt switching\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        ret = device_found.switch_voice_prompts(device_handle, voice_prompts);

        if (ret < 0) {
            fprintf(stderr, "Failed to switch voice prompt. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        PRINT_INFO("Success!\n");
    }

    if (rotate_to_mute != -1) {
        if ((device_found.capabilities & CAP_ROTATE_TO_MUTE) == 0) {
            fprintf(stderr, "Error: This headset doesn't support rotate to mute switching\n");
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }
        ret = device_found.switch_rotate_to_mute(device_handle, rotate_to_mute);

        if (ret < 0) {
            fprintf(stderr, "Failed to switch rotate to mute. Error: %d: %ls\n", ret, hid_error(device_handle));
            terminate_hid(&device_handle, &hid_path);
            return 1;
        }

        PRINT_INFO("Success!\n");
    }

    if (argc <= 1) {
        printf("You didn't set any arguments, so nothing happened.\nType %s -h for help.\n", argv[0]);
    }

    terminate_hid(&device_handle, &hid_path);

    return 0;
}

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

#include "dev.h"
#include "device_registry.h"
#include "hid_utility.h"
#include "utility.h"

#include <hidapi.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
int find_device(struct device* device_found)
{
    struct hid_device_info* devs;
    struct hid_device_info* cur_dev;
    int found = -1;
    devs = hid_enumerate(0x0, 0x0);
    cur_dev = devs;
    while (cur_dev) {
        found = get_device(device_found, cur_dev->vendor_id, cur_dev->product_id);

        if (found == 0) {
            PRINT_INFO("Found %s!\n", device_found->device_name);
            break;
        }

        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);

    return found;
}

static void print_capability(int capabilities, enum capabilities cap, char shortName, const char* longName)
{
    if ((capabilities & cap) == cap) {
        if (short_output) {
            printf("%c", shortName);
        } else {
            printf("* %s\n", longName);
        }
    }
}

static void print_udevrules()
{
    int i = 0;
    struct device* device_found;

    printf("ACTION!=\"add|change\", GOTO=\"headset_end\"\n");
    printf("\n");

    while (iterate_devices(i++, &device_found) == 0) {
        printf("# %s\n", device_found->device_name);

        for (int i = 0; i < device_found->numIdProducts; i++)
            printf("KERNEL==\"hidraw*\", SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\", TAG+=\"uaccess\"\n",
                (unsigned int)device_found->idVendor, (unsigned int)device_found->idProductsSupported[i]);

        printf("\n");
    }

    printf("LABEL=\"headset_end\"\n");
}

static int check_capability(struct device* device_found, enum capabilities cap)
{
    if ((device_found->capabilities & cap) == 0) {
        fprintf(stderr, "Error: This headset doesn't support %s\n", capabilities_str[cap]);
        return 1;
    }

    return 0;
}

static int handle_featurereturn(int ret, hid_device* device_handle, enum capabilities cap)
{
    if (ret < 0) {
        fprintf(stderr, "Failed to set %s. Error: %d: %ls\n", capabilities_str[cap], ret, hid_error(device_handle));
        return 1;
    }

    PRINT_INFO("Success!\n");
    return ret;
}

int main(int argc, char* argv[])
{
    int c;

    int sidetone_loudness = -1;
    int request_battery = 0;
    int request_chatmix = 0;
    int notification_sound = -1;
    int lights = -1;
    int inactive_time = -1;
    int voice_prompts = -1;
    int rotate_to_mute = -1;
    int print_capabilities = -1;
    int dev_mode = 0;

    struct option opts[] = {
        { "battery", no_argument, NULL, 'b' },
        { "capabilities", no_argument, NULL, '?' },
        { "chatmix", no_argument, NULL, 'm' },
        { "dev", no_argument, NULL, 0 },
        { "help", no_argument, NULL, 'h' },
        { "inactive-time", required_argument, NULL, 'i' },
        { "light", required_argument, NULL, 'l' },
        { "notificate", required_argument, NULL, 'n' },
        { "rotate-to-mute", required_argument, NULL, 'r' },
        { "sidetone", required_argument, NULL, 's' },
        { "short-output", no_argument, NULL, 'c' },
        { "voice-prompt", required_argument, NULL, 'v' },
        { 0, 0, 0, 0 }
    };

    int option_index = 0;

    // Init all information of supported devices
    init_devices();

    while ((c = getopt_long(argc, argv, "bchi:l:mn:r:s:uv:?", opts, &option_index)) != -1) {
        switch (c) {
        case 'b':
            request_battery = 1;
            break;
        case 'c':
            short_output = 1;
            break;
        case 'i':
            inactive_time = strtol(optarg, NULL, 10);

            if (inactive_time > 90 || inactive_time < 0) {
                printf("Usage: %s -s 0-90, 0 is off\n", argv[0]);
                return 1;
            }
            break;
        case 'l':
            lights = strtol(optarg, NULL, 10);
            if (lights < 0 || lights > 1) {
                printf("Usage: %s -l 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'm':
            request_chatmix = 1;
            break;
        case 'n': // todo
            notification_sound = strtol(optarg, NULL, 10);

            if (notification_sound < 0 || notification_sound > 1) {
                printf("Usage: %s -n 0|1\n", argv[0]);
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
        case 's':
            sidetone_loudness = strtol(optarg, NULL, 10);

            if (sidetone_loudness > 128 || sidetone_loudness < 0) {
                printf("Usage: %s -s 0-128\n", argv[0]);
                return 1;
            }
            break;
        case 'u':
            fprintf(stderr, "Outputting udev rules to stdout/console...\n\n");
            print_udevrules();
            return 0;
        case 'v':
            voice_prompts = strtol(optarg, NULL, 10);
            if (voice_prompts < 0 || voice_prompts > 1) {
                printf("Usage: %s -v 0|1\n", argv[0]);
                return 1;
            }
            break;
        case '?':
            print_capabilities = 1;
            break;
        case 'h':
            printf("Headsetcontrol written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd\n\n");
            printf("Parameters\n");
            printf("  -s, --sidetone level\t\tSets sidetone, level must be between 0 and 128\n");
            printf("  -b, --battery\t\t\tChecks the battery level\n");
            printf("  -n, --notificate soundid\tMakes the headset play a notifiation\n");
            printf("  -l, --light 0|1\t\tSwitch lights (0 = off, 1 = on)\n");
            printf("  -c, --short-output\t\t\tUse more machine-friendly output \n");
            printf("  -i, --inactive-time time\tSets inactive time in minutes, time must be between 0 and 90, 0 disables the feature.\n");
            printf("  -m, --chatmix\t\t\tRetrieves the current chat-mix-dial level setting between 0 and 128. Below 64 is the game side and above is the chat side.\n");
            printf("  -v, --voice-prompt 0|1\tTurn voice prompts on or off (0 = off, 1 = on)\n");
            printf("  -r, --rotate-to-mute 0|1\tTurn rotate to mute feature on or off (0 = off, 1 = on)\n");
            printf("\n");
            printf("  -u\tOutputs udev rules to stdout/console\n");

            printf("\n");
            return 0;
        case 0:
            if (strcmp(opts[option_index].name, "dev") == 0) {
                dev_mode = 1;
                break;
            }
        default:
            printf("Invalid argument %c\n", c);
            return 1;
        }
    }

    if (dev_mode) {
        // use +1 to make sure the first parameter is some previous argument (which normally would be the name of the program)
        return dev_main(argc - optind + 1, &argv[optind - 1]);
    } else {
        for (int index = optind; index < argc; index++)
            fprintf(stderr, "Non-option argument %s\n", argv[index]);
    }

    // describes the device, when a headset was found
    static struct device device_found;

    // Look for a supported device
    int headset_available = find_device(&device_found);
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
    if (device_handle == NULL) {
        fprintf(stderr, "Couldn't open device.\n");
        terminate_hid(&device_handle, &hid_path);
        return 1;
    }

    int ret = 0;
    // Set all features the user wants us to set

    if (sidetone_loudness != -1) {
        if (check_capability(&device_found, CAP_SIDETONE))
            goto error;

        ret = device_found.send_sidetone(device_handle, sidetone_loudness);

        if (handle_featurereturn(ret, device_handle, CAP_SIDETONE))
            goto error;
    }

    if (lights != -1) {
        if (check_capability(&device_found, CAP_LIGHTS))
            goto error;

        ret = device_found.switch_lights(device_handle, lights);

        if (handle_featurereturn(ret, device_handle, CAP_LIGHTS))
            goto error;
    }

    if (notification_sound != -1) {
        if (check_capability(&device_found, CAP_NOTIFICATION_SOUND))
            goto error;

        ret = device_found.notifcation_sound(device_handle, notification_sound);

        if (handle_featurereturn(ret, device_handle, CAP_NOTIFICATION_SOUND))
            goto error;
    }

    if (request_battery == 1) {
        if (check_capability(&device_found, CAP_BATTERY_STATUS))
            goto error;

        ret = device_found.request_battery(device_handle);

        if (ret < 0) {
            fprintf(stderr, "Failed to read battery. Error: %d: %ls\n", ret, hid_error(device_handle));
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
        if (check_capability(&device_found, CAP_INACTIVE_TIME))
            goto error;

        ret = device_found.send_inactive_time(device_handle, inactive_time);

        if (ret < 0) {
            fprintf(stderr, "Failed to set inactive time. Error: %d: %ls\n", ret, hid_error(device_handle));
            goto error;
        }

        PRINT_INFO("Successfully set inactive time to %d minutes!\n", inactive_time);
    }

    if (request_chatmix == 1) {
        if (check_capability(&device_found, CAP_CHATMIX_STATUS))
            goto error;

        ret = device_found.request_chatmix(device_handle);

        if (ret < 0) {
            fprintf(stderr, "Failed to request chat-mix. Error: %d: %ls\n", ret, hid_error(device_handle));
            goto error;
        }

        if (!short_output)
            printf("Chat-Mix: %d\n", ret);
        else
            printf("%d", ret);
    }

    if (voice_prompts != -1) {
        if (check_capability(&device_found, CAP_VOICE_PROMPTS))
            goto error;
        ret = device_found.switch_voice_prompts(device_handle, voice_prompts);

        if (handle_featurereturn(ret, device_handle, CAP_VOICE_PROMPTS))
            goto error;
    }

    if (rotate_to_mute != -1) {
        if (check_capability(&device_found, CAP_ROTATE_TO_MUTE))
            goto error;

        ret = device_found.switch_rotate_to_mute(device_handle, rotate_to_mute);

        if (handle_featurereturn(ret, device_handle, CAP_ROTATE_TO_MUTE))
            goto error;
    }

    if (print_capabilities != -1) {
        PRINT_INFO("Supported capabilities:\n\n");

        print_capability(device_found.capabilities, CAP_SIDETONE, 's', "set sidetone");
        print_capability(device_found.capabilities, CAP_BATTERY_STATUS, 'b', "read battery level");
        print_capability(device_found.capabilities, CAP_NOTIFICATION_SOUND, 'n', "play notification sound");
        print_capability(device_found.capabilities, CAP_LIGHTS, 'l', "switch lights");
        print_capability(device_found.capabilities, CAP_INACTIVE_TIME, 'i', "set inactive time");
        print_capability(device_found.capabilities, CAP_CHATMIX_STATUS, 'm', "read chat-mix");
        print_capability(device_found.capabilities, CAP_VOICE_PROMPTS, 'v', "set voice prompts");
        print_capability(device_found.capabilities, CAP_ROTATE_TO_MUTE, 'r', "set rotate to mute");
    }

    if (argc <= 1) {
        printf("You didn't set any arguments, so nothing happened.\nType %s -h for help.\n", argv[0]);
    }

    terminate_hid(&device_handle, &hid_path);

    return 0;

error:
    terminate_hid(&device_handle, &hid_path);
    return 1;
}

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
#include "device.h"
#include "device_registry.h"
#include "hid_utility.h"
#include "utility.h"

#include <hidapi.h>

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int hsc_device_timeout = 5000;

// 0=false; 1=true
static int short_output = 0;

/// printf only when short output not specified
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
static int find_device(struct device* device_found)
{
    struct hid_device_info* devs;
    struct hid_device_info* cur_dev;
    int found = -1;
    devs      = hid_enumerate(0x0, 0x0);
    cur_dev   = devs;
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

/**
 * @brief Generates udev rules, and prints them to STDOUT
 *
 * Goes through the implementation of all devices, and generates udev rules for use by Linux systems
 */
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

/**
 * @brief Checks if an existing connection exists, and either uses it, or closes it and creates a new one
 *
 * A device - depending on the feature - needs differend Endpoints/Connections
 * Instead of opening and keeping track of multiple connections, we close and open connections no demand
 *
 *
 *
 * @param existing_hid_path an existing connection path or NULL if none yet
 * @param device_handle an existing device handle or NULL if none yet
 * @param device headsetcontrol struct, containing vendor and productid
 * @param cap which capability to use, to determine interfaceid and usageids
 * @return 0 if successfull, or hid error code
 */
static hid_device* dynamic_connect(char** existing_hid_path, hid_device* device_handle,
    struct device* device, enum capabilities cap)
{
    // Generate the path which is needed
    char* hid_path = get_hid_path(device->idVendor, device->idProduct,
        device->capability_details[cap].interface, device->capability_details[cap].usagepage, device->capability_details[cap].usageid);

    if (!hid_path) {
        fprintf(stderr, "Requested/supported HID device not found or system error.\n");
        fprintf(stderr, " HID Error: %S\n", hid_error(NULL));
        return NULL;
    }

    // A connection already exists
    if (device_handle != NULL) {
        // The connection which exists is the same we need, so simply return it
        if (strcmp(*existing_hid_path, hid_path) == 0) {
            return device_handle;
        } else { // if its not the same, and already one connection is open, close it so we can make a new one
            hid_close(device_handle);
        }
    }

    free(*existing_hid_path);

    device_handle = hid_open_path(hid_path);
    if (device_handle == NULL) {
        fprintf(stderr, "Failed to open requested device.\n");
        fprintf(stderr, " HID Error: %S\n", hid_error(NULL));

        *existing_hid_path = NULL;
        return NULL;
    }

    *existing_hid_path = hid_path;
    return device_handle;
}

/**
 * @brief Handle a requested feature
 *
 * @param device_found the headset to use
 * @param device_handle points to an already open device_handle (if connection already exists) or points to null
 * @param hid_path points to an already used path used to connect, or points to null
 * @param cap requested feature
 * @param param first parameter of the feature
 * @return int 0 on success, some other number otherwise
 */
static int handle_feature(struct device* device_found, hid_device** device_handle, char** hid_path, enum capabilities cap, void* param)
{
    // Check if the headset implements the requested feature
    if ((device_found->capabilities & B(cap)) == 0) {
        fprintf(stderr, "Error: This headset doesn't support %s\n", capabilities_str[cap]);
        return 1;
    }

    *device_handle = dynamic_connect(hid_path, *device_handle,
        device_found, cap);

    if (!device_handle | !(*device_handle))
        return 1;

    int ret;

    switch (cap) {
    case CAP_SIDETONE:
        ret = device_found->send_sidetone(*device_handle, *(int*)param);
        break;

    case CAP_BATTERY_STATUS:
        ret = device_found->request_battery(*device_handle);

        if (ret < 0)
            break;
        else if (ret == BATTERY_CHARGING)
            short_output ? printf("-1") : printf("Battery: Charging\n");
        else if (ret == BATTERY_UNAVAILABLE)
            short_output ? printf("-2") : printf("Battery: Unavailable\n");
        else
            short_output ? printf("%d", ret) : printf("Battery: %d%%\n", ret);

        break;

    case CAP_NOTIFICATION_SOUND:
        ret = device_found->notifcation_sound(*device_handle, *(int*)param);
        break;

    case CAP_LIGHTS:
        ret = device_found->switch_lights(*device_handle, *(int*)param);
        break;

    case CAP_INACTIVE_TIME:
        ret = device_found->send_inactive_time(*device_handle, *(int*)param);

        if (ret < 0)
            break;

        PRINT_INFO("Successfully set inactive time to %d minutes!\n", *(int*)param);
        break;

    case CAP_CHATMIX_STATUS:
        ret = device_found->request_chatmix(*device_handle);

        if (ret < 0)
            break;

        short_output ? printf("%d", ret) : printf("Chat-Mix: %d\n", ret);
        break;

    case CAP_VOICE_PROMPTS:
        ret = device_found->switch_voice_prompts(*device_handle, *(int*)param);
        break;

    case CAP_ROTATE_TO_MUTE:
        ret = device_found->switch_rotate_to_mute(*device_handle, *(int*)param);
        break;

    case CAP_EQUALIZER_PRESET:
        ret = device_found->send_equalizer_preset(*device_handle, *(int*)param);

        if (ret < 0)
            break;

        PRINT_INFO("Successfully set equalizer preset to %d!\n", *(int*)param);
        break;

    case CAP_EQUALIZER:
        ret = device_found->send_equalizer(*device_handle, (struct equalizer_settings*)param);

        if (ret < 0)
            break;

        PRINT_INFO("Successfully set equalizer!\n");
        break;

    case CAP_MICROPHONE_MUTE_LED_BRIGHTNESS:
        ret = device_found->send_microphone_mute_led_brightness(*device_handle, *(int*)param);
        break;

    case CAP_MICROPHONE_VOLUME:
        ret = device_found->send_microphone_volume(*device_handle, *(int*)param);
        break;

    case NUM_CAPABILITIES:
        ret = -99; // silence warning

        assert(0);
        break;
    }

    if (ret == HSC_READ_TIMEOUT) {
        fprintf(stderr, "Failed to set/request %s, because of timeout, try again.\n", capabilities_str[cap]);
        return HSC_READ_TIMEOUT;
    } else if (ret == HSC_ERROR) {
        fprintf(stderr, "Failed to set/request %s. HeadsetControl Error. Error: %d: %ls\n", capabilities_str[cap], ret, hid_error(*device_handle));
        return HSC_ERROR;
    } else if (ret == HSC_OUT_OF_BOUNDS) {
        fprintf(stderr, "Failed to set/request %s. Provided parameter out of boundaries. Error: %d: %ls\n", capabilities_str[cap], ret, hid_error(*device_handle));
        return HSC_ERROR;
    } else if (ret < 0) {
        fprintf(stderr, "Failed to set/request %s. Error: %d: %ls\n", capabilities_str[cap], ret, hid_error(*device_handle));
        return 1;
    }

    PRINT_INFO("Success!\n");
    return 0;
}

// Makes parsing of optiona arguments easier
// Credits to https://cfengine.com/blog/2021/optional-arguments-with-getopt-long/
#define OPTIONAL_ARGUMENT_IS_PRESENT                             \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
            ? (bool)(optarg = argv[optind++])                    \
            : (optarg != NULL))

int main(int argc, char* argv[])
{
    int c;

    int sidetone_loudness                = -1;
    int request_battery                  = 0;
    int request_chatmix                  = 0;
    int notification_sound               = -1;
    int lights                           = -1;
    int inactive_time                    = -1;
    int voice_prompts                    = -1;
    int rotate_to_mute                   = -1;
    int print_capabilities               = -1;
    int equalizer_preset                 = -1;
    int microphone_mute_led_brightness   = -1;
    int microphone_volume                = -1;
    int dev_mode                         = 0;
    int follow                           = 0;
    unsigned follow_sec                  = 2;
    struct equalizer_settings* equalizer = NULL;

#define BUFFERLENGTH 1024
    char* read_buffer = calloc(BUFFERLENGTH, sizeof(char));

    struct option opts[] = {
        { "battery", no_argument, NULL, 'b' },
        { "capabilities", no_argument, NULL, '?' },
        { "chatmix", no_argument, NULL, 'm' },
        { "dev", no_argument, NULL, 0 },
        { "help", no_argument, NULL, 'h' },
        { "equalizer", required_argument, NULL, 'e' },
        { "equalizer-preset", required_argument, NULL, 'p' },
        { "inactive-time", required_argument, NULL, 'i' },
        { "light", required_argument, NULL, 'l' },
        { "follow", optional_argument, NULL, 'f' },
        { "notificate", required_argument, NULL, 'n' },
        { "rotate-to-mute", required_argument, NULL, 'r' },
        { "sidetone", required_argument, NULL, 's' },
        { "short-output", no_argument, NULL, 'c' },
        { "timeout", required_argument, NULL, 0 },
        { "voice-prompt", required_argument, NULL, 'v' },
        { 0, 0, 0, 0 }
    };

    int option_index = 0;

    // Init all information of supported devices
    init_devices();

    while ((c = getopt_long(argc, argv, "bchi:l:f::mn:r:s:uv:p:t:o:e:?", opts, &option_index)) != -1) {
        char* endptr = NULL; // for strtol

        switch (c) {
        case 'b':
            request_battery = 1;
            break;
        case 'c':
            short_output = 1;
            break;
        case 'e': {
            int size = get_data_from_parameter(optarg, read_buffer, BUFFERLENGTH);

            if (size < 0) {
                fprintf(stderr, "Equalizer bands values size larger than supported %d\n", BUFFERLENGTH);
                return 1;
            }

            if (size == 0) {
                fprintf(stderr, "No bands values specified to --equalizer\n");
                return 1;
            }

            equalizer       = malloc(sizeof(struct equalizer_settings) + size * sizeof(char));
            equalizer->size = size;
            for (int i = 0; i < size; i++) {
                equalizer->bands_values[i] = read_buffer[i];
            }
            break;
        }
        case 'f':
            follow = 1;
            if (OPTIONAL_ARGUMENT_IS_PRESENT) {
                follow_sec = strtol(optarg, &endptr, 10);
                if (follow_sec == 0) {
                    printf("Usage: %s -f [secs timeout]\n", argv[0]);
                    return 1;
                }
            }
            break;
        case 'i':
            inactive_time = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || inactive_time > 90 || inactive_time < 0) {
                printf("Usage: %s -i 0-90, 0 is off\n", argv[0]);
                return 1;
            }
            break;
        case 'l':
            lights = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || endptr == optarg || lights < 0 || lights > 1) {
                printf("Usage: %s -l 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'm':
            request_chatmix = 1;
            break;
        case 'n': // todo
            notification_sound = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || notification_sound < 0 || notification_sound > 1) {
                printf("Usage: %s -n 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'p':
            equalizer_preset = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || equalizer_preset < 0 || equalizer_preset > 3) {
                printf("Usage: %s -p 0-3, 0 is default\n", argv[0]);
                return 1;
            }
            break;
        case 't':
            microphone_mute_led_brightness = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || microphone_mute_led_brightness < 0 || microphone_mute_led_brightness > 3) {
                printf("Usage: %s -t 0-3\n", argv[0]);
                return 1;
            }
            break;
        case 'o':
            microphone_volume = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || microphone_volume < 0 || microphone_volume > 128) {
                printf("Usage: %s -o 0-128\n", argv[0]);
                return 1;
            }
            break;
        case 'r':
            rotate_to_mute = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || endptr == optarg || rotate_to_mute < 0 || rotate_to_mute > 1) {
                printf("Usage: %s -r 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 's':
            sidetone_loudness = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || sidetone_loudness > 128 || sidetone_loudness < 0) {
                printf("Usage: %s -s 0-128\n", argv[0]);
                return 1;
            }
            break;
        case 'u':
            fprintf(stderr, "Generating udev rules..\n\n");
            print_udevrules();
            return 0;
        case 'v':
            voice_prompts = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || endptr == optarg || voice_prompts < 0 || voice_prompts > 1) {
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
            printf("  -c, --short-output\t\tUse more machine-friendly output \n");
            printf("  -i, --inactive-time time\tSets inactive time in minutes, time must be between 0 and 90, 0 disables the feature.\n");
            printf("  -m, --chatmix\t\t\tRetrieves the current chat-mix-dial level setting between 0 and 128. Below 64 is the game side and above is the chat side.\n");
            printf("  -v, --voice-prompt 0|1\tTurn voice prompts on or off (0 = off, 1 = on)\n");
            printf("  -r, --rotate-to-mute 0|1\tTurn rotate to mute feature on or off (0 = off, 1 = on)\n");
            printf("  -e, --equalizer string\tSets equalizer to specified curve, string must contain band values specific to the device (hex or decimal) delimited by spaces, or commas, or new-lines e.g \"0x18, 0x18, 0x18, 0x18, 0x18\".\n");
            printf("  -p, --equalizer-preset number\tSets equalizer preset, number must be between 0 and 3, 0 sets the default\n");
            printf("  -t, --microphone-mute-led-brightness number\tSets microphone mute LED brightness, number must be between 0 and 3\n");
            printf("  -o, --microphone-volume number\tSets microphone volume, number must be between 0 and 128\n");
            printf("  -f, --follow [secs timeout]\tRe-run the commands after the specified seconds timeout or 2 by default\n");
            printf("\n");
            printf("      --timeout 0-100000\t\tSpecifies the timeout in ms for reading data from device (default 5000)\n");
            printf("  -?, --capabilities\t\tPrint every feature headsetcontrol supports of the connected headset\n");
            printf("\n");
            printf("  -u\tOutputs udev rules to stdout/console\n");

            printf("\n");
            return 0;
        case 0:
            if (strcmp(opts[option_index].name, "dev") == 0) {
                dev_mode = 1;
                break;
            } else if (strcmp(opts[option_index].name, "timeout") == 0) {
                hsc_device_timeout = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || hsc_device_timeout < 0 || hsc_device_timeout > 100000) {
                    printf("Usage: %s --timeout 0-100000\n", argv[0]);
                    return 1;
                }
                break;
            }
            // fall through
        default:
            printf("Invalid argument %c\n", c);
            return 1;
        }
    }

    free(read_buffer);

    if (dev_mode) {
        // use +1 to make sure the first parameter is some previous argument (which normally would be the name of the program)
        return dev_main(argc - optind + 1, &argv[optind - 1]);
    } else {
        for (int index = optind; index < argc; index++)
            fprintf(stderr, "Non-option argument %s\n", argv[index]);
    }

    // describes the headsetcontrol device, when a headset was found
    static struct device device_found;

    // Look for a supported device
    int headset_available = find_device(&device_found);
    if (headset_available != 0) {
        fprintf(stderr, "No supported headset found\n");
        return 1;
    }

    // We open connection to HID devices on demand
    hid_device* device_handle = NULL;
    char* hid_path            = NULL;

    // Set all features the user wants us to set
    int error = 0;

loop_start:
    if (sidetone_loudness != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_SIDETONE, &sidetone_loudness)) != 0)
            goto error;
    }

    if (lights != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_LIGHTS, &lights)) != 0)
            goto error;
    }

    if (notification_sound != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_NOTIFICATION_SOUND, &notification_sound)) != 0)
            goto error;
    }

    if (request_battery == 1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_BATTERY_STATUS, &request_battery)) != 0)
            goto error;
    }

    if (inactive_time != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_INACTIVE_TIME, &inactive_time)) != 0)
            goto error;
    }

    if (request_chatmix == 1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_CHATMIX_STATUS, &request_chatmix)) != 0)
            goto error;
    }

    if (voice_prompts != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_VOICE_PROMPTS, &voice_prompts)) != 0)
            goto error;
    }

    if (rotate_to_mute != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_ROTATE_TO_MUTE, &rotate_to_mute)) != 0)
            goto error;
    }

    if (equalizer_preset != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_EQUALIZER_PRESET, &equalizer_preset)) != 0)
            goto error;
    }

    if (microphone_mute_led_brightness != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_MICROPHONE_MUTE_LED_BRIGHTNESS, &microphone_mute_led_brightness)) != 0)
            goto error;
    }

    if (microphone_volume != -1) {
        if ((error = handle_feature(&device_found, &device_handle, &hid_path, CAP_MICROPHONE_VOLUME, &microphone_volume)) != 0)
            goto error;
    }

    if (equalizer != NULL) {
        error = handle_feature(&device_found, &device_handle, &hid_path, CAP_EQUALIZER, equalizer);
        free(equalizer);

        if ((error) != 0)
            goto error;
    }

    if (print_capabilities != -1) {
        PRINT_INFO("Supported capabilities:\n\n");

        // go through all enum capabilities
        for (int i = 0; i < NUM_CAPABILITIES; i++) {
            // When the capability i is included in .capabilities
            if ((device_found.capabilities & B(i)) == B(i)) {
                if (short_output) {
                    printf("%c", capabilities_str_short[i]);
                } else {
                    printf("* %s\n", capabilities_str[i]);
                }
            }
        }
    }

    if (argc <= 1) {
        printf("You didn't set any arguments, so nothing happened.\nType %s -h for help.\n", argv[0]);
    }

    if (follow) {
        sleep(follow_sec);
        goto loop_start;
    }

    terminate_hid(&device_handle, &hid_path);

    return 0;

error:
    terminate_hid(&device_handle, &hid_path);
    return error;
}

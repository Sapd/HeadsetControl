/***
    Copyright (C) 2016-2024 Denis Arnst (Sapd) <https://github.com/Sapd>

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
#include "output.h"
#include "utility.h"
#include "version.h"

#include <hidapi.h>

#include <assert.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int test_profile = 0;

int hsc_device_timeout = 5000;

/**
 *  This function iterates through all HID devices.
 *
 *  @return 0 when a supported device is found
 */
static int find_device(struct device* device_found, int test_device)
{
    if (test_device)
        return get_device(device_found, VENDOR_TESTDEVICE, PRODUCT_TESTDEVICE);

    struct hid_device_info* devs;
    struct hid_device_info* cur_dev;
    int found = -1;
    devs      = hid_enumerate(0x0, 0x0);
    cur_dev   = devs;
    while (cur_dev) {
        found = get_device(device_found, cur_dev->vendor_id, cur_dev->product_id);

        if (found == 0) {
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

static void print_readmetable()
{
    int i = 0;
    struct device* device_found;

    printf("| Device |");
    for (int j = 0; j < NUM_CAPABILITIES; j++) {
        printf(" %s |", capabilities_str[j]);
    }
    printf("\n");

    printf("| --- |");
    for (int j = 0; j < NUM_CAPABILITIES; j++) {
        printf(" --- |");
    }
    printf("\n");

    while (iterate_devices(i++, &device_found) == 0) {
        printf("| %s |", device_found->device_name);

        for (int j = 0; j < NUM_CAPABILITIES; j++) {
            if (has_capability(device_found->capabilities, j)) {
                printf(" x |");
            } else {
                printf("   |");
            }
        }
        printf("\n");
    }
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
 * @return hid_device pointer if successfull, or NULL (error in hid_error)
 */
static hid_device* dynamic_connect(char** existing_hid_path, hid_device* device_handle,
    struct device* device, enum capabilities cap)
{
    // Generate the path which is needed
    char* hid_path = get_hid_path(device->idVendor, device->idProduct,
        device->capability_details[cap].interface, device->capability_details[cap].usagepage, device->capability_details[cap].usageid);

    if (!hid_path) {
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
        *existing_hid_path = NULL;
        return NULL;
    }

    hid_get_manufacturer_string(device_handle, device->device_hid_vendorname, sizeof(device->device_hid_vendorname) / sizeof(device->device_hid_vendorname[0]));
    hid_get_product_string(device_handle, device->device_hid_productname, sizeof(device->device_hid_productname) / sizeof(device->device_hid_productname[0]));

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
 * @return FeatureResult which saves the result or failure of the requested feature
 */
static FeatureResult handle_feature(struct device* device_found, hid_device** device_handle, char** hid_path, enum capabilities cap, void* param)
{
    FeatureResult result;

    // Check if the headset implements the requested feature
    if ((device_found->capabilities & B(cap)) == 0) {
        result.status = FEATURE_ERROR;
        result.value  = -1;
        _asprintf(&result.message, "This headset doesn't support %s", capabilities_str[cap]);
        return result;
    }

    if (device_found->idProduct != PRODUCT_TESTDEVICE) {
        *device_handle = dynamic_connect(hid_path, *device_handle,
            device_found, cap);

        if (!device_handle | !(*device_handle)) {
            result.status = FEATURE_DEVICE_FAILED_OPEN;
            result.value  = 0;
            _asprintf(&result.message, "Could not open device. Error: %ls", hid_error(*device_handle));
            return result;
        }
    } else {
        *device_handle = NULL;
    }

    int ret;

    switch (cap) {
    case CAP_SIDETONE:
        ret = device_found->send_sidetone(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_BATTERY_STATUS: {
        BatteryInfo battery = device_found->request_battery(*device_handle);

        result.status2 = battery.status;
        if (battery.status == BATTERY_AVAILABLE) {
            result.status = FEATURE_SUCCESS;
            result.value  = battery.level;
            _asprintf(&result.message, "Battery: %d%%", battery.level);
        } else if (battery.status == BATTERY_CHARGING) {
            result.status  = FEATURE_INFO;
            result.value   = battery.level;
            result.message = strdup("Charging");
        } else if (battery.status == BATTERY_UNAVAILABLE) {
            result.status  = FEATURE_INFO;
            result.value   = BATTERY_UNAVAILABLE;
            result.message = strdup("Battery status unavailable");
        } else if (battery.status == BATTERY_TIMEOUT) {
            result.status  = FEATURE_ERROR;
            result.value   = BATTERY_TIMEOUT;
            result.message = strdup("Battery status request timed out");
        } else { // Handle errors
            result.status = FEATURE_ERROR;
            result.value  = (int)battery.status;

            if (device_found->idProduct != PRODUCT_TESTDEVICE)
                _asprintf(&result.message, "Error retrieving battery status. Error: %ls", hid_error(*device_handle));
            else // dont call hid_error on test device
                _asprintf(&result.message, "Error retrieving battery status");
        }
        return result;
    }

    case CAP_NOTIFICATION_SOUND:
        ret = device_found->notifcation_sound(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_LIGHTS:
        ret = device_found->switch_lights(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_INACTIVE_TIME:
        ret = device_found->send_inactive_time(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_CHATMIX_STATUS:
        ret = device_found->request_chatmix(*device_handle);

        if (ret >= 0) {
            result.status = FEATURE_SUCCESS;
            result.value  = ret;
            _asprintf(&result.message, "Chat-Mix: %d", ret);
        } else {
            result.status  = FEATURE_ERROR;
            result.value   = ret;
            result.message = strdup("Error retrieving chatmix status");
        }

        return result;

    case CAP_VOICE_PROMPTS:
        ret = device_found->switch_voice_prompts(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_ROTATE_TO_MUTE:
        ret = device_found->switch_rotate_to_mute(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_EQUALIZER_PRESET:
        ret = device_found->send_equalizer_preset(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_EQUALIZER:
        ret = device_found->send_equalizer(*device_handle, (struct equalizer_settings*)param);
        break;

    case CAP_PARAMETRIC_EQUALIZER:
        ret = device_found->send_parametric_equalizer(*device_handle, (struct parametric_equalizer_settings*)param);
        break;

    case CAP_MICROPHONE_MUTE_LED_BRIGHTNESS:
        ret = device_found->send_microphone_mute_led_brightness(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_MICROPHONE_VOLUME:
        ret = device_found->send_microphone_volume(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_VOLUME_LIMITER:
        ret = device_found->send_volume_limiter(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_BT_WHEN_POWERED_ON:
        ret = device_found->send_bluetooth_when_powered_on(*device_handle, (uint8_t) * (int*)param);
        break;

    case CAP_BT_CALL_VOLUME:
        ret = device_found->send_bluetooth_call_volume(*device_handle, (uint8_t) * (int*)param);
        break;

    case NUM_CAPABILITIES:
    default:
        ret = -99; // silence warning
        UNUSED(ret);

        assert(0);
        break;
    }

    // Handle success
    if (ret >= 0) {
        result.status  = FEATURE_SUCCESS;
        result.value   = ret;
        result.message = NULL;
        return result;
    }

    result.status = FEATURE_ERROR;
    result.value  = ret;

    switch (ret) {
    case HSC_READ_TIMEOUT:
        _asprintf(&result.message, "Failed to set/request %s, because of timeout", capabilities_str[cap]);
        break;
    case HSC_ERROR:
        _asprintf(&result.message, "Failed to set/request %s. HeadsetControl Error", capabilities_str[cap]);
        break;
    case HSC_OUT_OF_BOUNDS:
        _asprintf(&result.message, "Failed to set/request %s. Provided parameter out of boundaries", capabilities_str[cap]);
        break;
    case HSC_INVALID_ARG:
        _asprintf(&result.message, "Failed to set/request %s. Provided parameter invalid", capabilities_str[cap]);
        break;
    default: // Must be a HID error
        if (device_found->idProduct != PRODUCT_TESTDEVICE)
            _asprintf(&result.message, "Failed to set/request %s. Error: %d: %ls", capabilities_str[cap], ret, hid_error(*device_handle));
        else // dont call hid_error on test device, it will confuse users/devs because it will show success
            _asprintf(&result.message, "Failed to set/request %s. Error: %d", capabilities_str[cap], ret);

        break;
    }

    return result;
}

void print_help(char* programname, struct device* device_found, bool _show_all)
{
    bool show_all = !device_found || _show_all;

    printf("HeadsetControl by Sapd (Denis Arnst)\n\thttps://github.com/Sapd/HeadsetControl\n\n");
    printf("Version: %s\n\n", VERSION);
    // printf("Usage: %s [options]\n", programname);
    // printf("Options:\n");

    if (show_all || has_capability(device_found->capabilities, CAP_SIDETONE)) {
        printf("Sidetone:\n");
        printf("  -s, --sidetone LEVEL\t\tSet sidetone level (0-128)\n");
        printf("\n");
    }

    if (show_all || has_capability(device_found->capabilities, CAP_BATTERY_STATUS)) {
        printf("Battery:\n");
        printf("  -b, --battery\t\t\tCheck battery level\n");
        printf("\n");
    }

    // ------ Category: lights and notifications
    bool show_lights        = show_all || has_capability(device_found->capabilities, CAP_LIGHTS);
    bool show_voice_prompts = show_all || has_capability(device_found->capabilities, CAP_VOICE_PROMPTS);

    if (show_lights || show_voice_prompts) {
        printf("%s:\n", (show_lights && show_voice_prompts) ? "Lights and Voice Prompts" : (show_lights ? "Lights" : "Voice Prompts"));
        if (show_lights) {
            printf("  -l, --light [0|1]\t\tTurn lights off (0) or on (1)\n");
        }
        if (show_voice_prompts) {
            printf("  -v, --voice-prompt [0|1]\tTurn voice prompts off (0) or on (1)\n");
        }
        printf("\n");
    }
    // ------

    // ------ Category: Features
    bool show_inactive_time      = show_all || has_capability(device_found->capabilities, CAP_INACTIVE_TIME);
    bool show_chatmix_status     = show_all || has_capability(device_found->capabilities, CAP_CHATMIX_STATUS);
    bool show_notification_sound = show_all || has_capability(device_found->capabilities, CAP_NOTIFICATION_SOUND);
    bool show_volume_limiter     = show_all || has_capability(device_found->capabilities, CAP_VOLUME_LIMITER);

    if (show_inactive_time || show_chatmix_status || show_notification_sound) {
        printf("Features:\n");
        if (show_inactive_time) {
            printf("  -i, --inactive-time MINUTES\tSet inactive time (0-90 minutes, 0 disables)\n");
        }
        if (show_chatmix_status) {
            printf("  -m, --chatmix\t\t\tGet chat-mix-dial level (0-128, <64 for game, >64 for chat)\n");
        }
        if (show_notification_sound) {
            printf("  -n, --notificate SOUNDID\tPlay notification sound (SOUNDID depends on device)\n");
        }
        if (show_volume_limiter) {
            printf("  --volume-limiter [0|1]\tTurn Volume limiter off (0) or on (1)\n");
        }
        printf("\n");
    }
    // ------

    // ------ Category: Equalizer
    bool show_equalizer            = show_all || has_capability(device_found->capabilities, CAP_EQUALIZER);
    bool show_equalizer_preset     = show_all || has_capability(device_found->capabilities, CAP_EQUALIZER_PRESET);
    bool show_parametric_equalizer = show_all || has_capability(device_found->capabilities, CAP_PARAMETRIC_EQUALIZER);

    if (show_equalizer || show_equalizer_preset) {
        printf("Equalizer:\n");
        if (show_equalizer) {
            printf("  -e, --equalizer STRING\tSet equalizer curve (values separated by spaces, commas, or new-lines)\n");
        }
        if (show_equalizer_preset) {
            printf("  -p, --equalizer-preset NUMBER\tSet equalizer preset (0-3, 0 for default)\n");
        }
        printf("\n");
    }

    if (show_parametric_equalizer) {
        // TODO parametric equalizer
        printf("Parametric Equalizer:\n");
        printf("  --parametric-equalizer STRING\t\tSet equalizer bands (bands separated by semicolon)\n");
        printf("      Band format:\t\t\tFREQUENCY,GAIN,Q_FACTOR,FILTER_TYPE\n");
        printf("      Availabe filter types:\t\t");
        for (int i = 0; i < NUM_EQ_FILTER_TYPES; i++) {
            if (show_all || has_capability(device_found->parametric_equalizer->filter_types, i)) {
                printf("%s, ", equalizer_filter_type_str[i]);
            }
        }
        printf("\n\n");
        printf("      Examples:\t--parametric-equalizer\t'300,3.5,1.5,peaking;14000,-2,1.414,highshelf'\n");
        printf("      \t\t\t\t\tSets a 300Hz +3.5dB Q1.5 peaking filter\n\t\t\t\t\tand a 14kHz -2dB Q1.414 highshelf filter\n");
        printf("\n");
        printf("      \t\t--parametric-equalizer\treset\n");
        printf("      \t\t\t\t\tResets/disables all bands");
        printf("\n");
    }
    // ------

    // ------ Category: Microphone
    bool show_rotate_to_mute                 = show_all || has_capability(device_found->capabilities, CAP_ROTATE_TO_MUTE);
    bool show_microphone_mute_led_brightness = show_all || has_capability(device_found->capabilities, CAP_MICROPHONE_MUTE_LED_BRIGHTNESS);
    bool show_microphone_volume              = show_all || has_capability(device_found->capabilities, CAP_MICROPHONE_VOLUME);

    if (show_rotate_to_mute || show_microphone_mute_led_brightness || show_microphone_volume) {
        printf("Microphone:\n");
        if (show_rotate_to_mute) {
            printf("  -r, --rotate-to-mute [0|1]\t\t\tToggle rotate to mute (0 = off, 1 = on)\n");
        }
        if (show_microphone_mute_led_brightness) {
            printf("  --microphone-mute-led-brightness NUMBER\tSet mic mute LED brightness (0-3)\n");
        }
        if (show_microphone_volume) {
            printf("  --microphone-volume NUMBER\t\t\tSet microphone volume (0-128)\n");
        }
        printf("\n");
    }
    // ------

    // ------ Category: Bluetooth
    bool show_bt_when_powered_on = show_all || has_capability(device_found->capabilities, CAP_BT_WHEN_POWERED_ON);
    bool show_bt_call_volume     = show_all || has_capability(device_found->capabilities, CAP_BT_CALL_VOLUME);

    if (show_bt_when_powered_on || show_bt_call_volume) {
        printf("Bluetooth:\n");
        if (show_bt_when_powered_on) {
            printf("  --bt-when-powered-on [0|1]\tToggle bluetooth turning off (0) or on (1) when turning on headpohnes\n");
        }
        if (show_bt_call_volume) {
            printf("  --bt-call-volume NUMBER\tSet headphones volume during a bluetooth call by lowering pc volume (0-2)\n");
        }
        printf("\n");
    }
    // ------

    if (show_all) {
        printf("Advanced:\n");
        printf("  -f, --follow [SECS]\t\tRe-run commands after SECS seconds (default 2 seconds if not specified)\n");
        printf("  --timeout MS\t\t\tSet timeout for reading data (0-100000 ms, default 5000)\n");
        printf("  -?, --capabilities\t\tList supported features of the connected headset\n\n");

        printf("Miscellaneous:\n");
        printf("  -u\t\t\t\tOutput udev rules to stdout/console\n");
        printf("  --dev\t\t\t\tDevelopment options\n");
        printf("  --readme-helper\t\tOutput table of device features for README.md\n");
        printf("  --test-device [profile]\tUse a built-in test device instead of a real one\n");
        printf("                         \t profile is an optional number for different tests\n");
        printf("  --connected\t\t\tCheck if device connected (for scripting purposes)\n");
        printf("  -o, --output FORMAT\t\tOutput format (JSON, YAML, ENV, STANDARD)\n");
        printf("\n");
    }

    if (show_all || has_capability(device_found->capabilities, CAP_SIDETONE) || has_capability(device_found->capabilities, CAP_BATTERY_STATUS)) {
        printf("Examples:\n");
        if (show_all || has_capability(device_found->capabilities, CAP_BATTERY_STATUS))
            printf("  %s -b\t\tCheck the battery level\n", programname);
        if (show_all || has_capability(device_found->capabilities, CAP_SIDETONE))
            printf("  %s -s 64\tSet sidetone level to 64\n", programname);
        if (show_all || (has_capability(device_found->capabilities, CAP_LIGHTS) && has_capability(device_found->capabilities, CAP_SIDETONE)))
            printf("  %s -l 1 -s 0\tTurn on lights and deactivate sidetone\n", programname);
        printf("\n");
    }

    if (!show_all && device_found)
        printf("\nHint:\tOptions were filtered to your device (%s)\n\tUse --help-all to show all options (including advanced ones)\n", device_found->device_name);
}

// for --follow
volatile sig_atomic_t follow = false;

void interruptHandler(int signal_number)
{
    UNUSED(signal_number);
    follow = false;
}

// Makes parsing of optional arguments easier
// Credits to https://cfengine.com/blog/2021/optional-arguments-with-getopt-long/
#define OPTIONAL_ARGUMENT_IS_PRESENT                             \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
            ? (bool)(optarg = argv[optind++])                    \
            : (optarg != NULL))

int main(int argc, char* argv[])
{
    int c;

    int should_print_help                                      = 0;
    int should_print_help_all                                  = 0;
    int print_udev_rules                                       = 0;
    int sidetone_loudness                                      = -1;
    int request_battery                                        = 0;
    int request_chatmix                                        = 0;
    int request_connected                                      = 0;
    int notification_sound                                     = -1;
    int lights                                                 = -1;
    int inactive_time                                          = -1;
    int voice_prompts                                          = -1;
    int rotate_to_mute                                         = -1;
    int print_capabilities                                     = -1;
    int equalizer_preset                                       = -1;
    int microphone_mute_led_brightness                         = -1;
    int microphone_volume                                      = -1;
    int volume_limiter                                         = -1;
    int bt_when_powered_on                                     = -1;
    int bt_call_volume                                         = -1;
    int dev_mode                                               = 0;
    unsigned follow_sec                                        = 2;
    struct equalizer_settings* equalizer                       = NULL;
    struct parametric_equalizer_settings* parametric_equalizer = NULL;

    OutputType output_format = OUTPUT_STANDARD;
    int test_device          = 0;

#define BUFFERLENGTH 1024
    float* read_buffer = calloc(BUFFERLENGTH, sizeof(float));

    struct option opts[] = {
        { "battery", no_argument, NULL, 'b' },
        { "bt-call-volume", required_argument, NULL, 0 },
        { "bt-when-powered-on", required_argument, NULL, 0 },
        { "capabilities", no_argument, NULL, '?' },
        { "chatmix", no_argument, NULL, 'm' },
        { "connected", no_argument, NULL, 0 },
        { "dev", no_argument, NULL, 0 },
        { "help", no_argument, NULL, 'h' },
        { "help-all", no_argument, NULL, 0 },
        { "equalizer", required_argument, NULL, 'e' },
        { "equalizer-preset", required_argument, NULL, 'p' },
        { "parametric-equalizer", required_argument, NULL, 0 },
        { "microphone-mute-led-brightness", required_argument, NULL, 0 },
        { "microphone-volume", required_argument, NULL, 0 },
        { "inactive-time", required_argument, NULL, 'i' },
        { "light", required_argument, NULL, 'l' },
        { "output", optional_argument, NULL, 'o' },
        { "follow", optional_argument, NULL, 'f' },
        { "notificate", required_argument, NULL, 'n' },
        { "rotate-to-mute", required_argument, NULL, 'r' },
        { "sidetone", required_argument, NULL, 's' },
        { "short-output", no_argument, NULL, 'c' },
        { "timeout", required_argument, NULL, 0 },
        { "voice-prompt", required_argument, NULL, 'v' },
        { "volume-limiter", required_argument, NULL, 0 },
        { "test-device", optional_argument, NULL, 0 },
        { "readme-helper", no_argument, NULL, 0 },
        { 0, 0, 0, 0 }
    };

    int option_index = 0;

    while ((c = getopt_long(argc, argv, "bchi:l:f::mn:o::r:s:uv:p:e:?", opts, &option_index)) != -1) {
        char* endptr = NULL; // for strtol

        switch (c) {
        case 'b':
            request_battery = 1;
            break;
        case 'c':
            output_format = OUTPUT_SHORT;
            break;
        case 'e': {
            int size = get_float_data_from_parameter(optarg, read_buffer, BUFFERLENGTH);

            if (size < 0) {
                fprintf(stderr, "Equalizer bands values size larger than supported %d\n", BUFFERLENGTH);
                return 1;
            }

            if (size == 0) {
                fprintf(stderr, "No bands values specified to --equalizer\n");
                return 1;
            }

            equalizer               = malloc(sizeof(struct equalizer_settings));
            equalizer->size         = size;
            equalizer->bands_values = malloc(sizeof(float) * size);
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
                    fprintf(stderr, "Usage: %s -f [secs timeout]\n", argv[0]);
                    return 1;
                }
            }
            break;
        case 'i':
            inactive_time = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || inactive_time > 90 || inactive_time < 0) {
                fprintf(stderr, "Usage: %s -i 0-90, 0 is off\n", argv[0]);
                return 1;
            }
            break;
        case 'l':
            lights = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || endptr == optarg || lights < 0 || lights > 1) {
                fprintf(stderr, "Usage: %s -l 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'm':
            request_chatmix = 1;
            break;
        case 'n':
            notification_sound = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || notification_sound < 0 || notification_sound > 1) {
                fprintf(stderr, "Usage: %s -n 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 'o': {
            bool output_specified = true;

            if (OPTIONAL_ARGUMENT_IS_PRESENT) {
                if (strcasecmp(optarg, "JSON") == 0)
                    output_format = OUTPUT_JSON;
                else if (strcasecmp(optarg, "YAML") == 0)
                    output_format = OUTPUT_YAML;
                else if (strcasecmp(optarg, "ENV") == 0)
                    output_format = OUTPUT_ENV;
                else if (strcasecmp(optarg, "STANDARD") == 0)
                    output_format = OUTPUT_STANDARD;
                else if (strcasecmp(optarg, "SHORT") == 0)
                    output_format = OUTPUT_SHORT;
                else
                    output_specified = false;
            } else {
                output_specified = false;
            }

            if (output_specified == false) {
                // short not listed because deprecated
                fprintf(stderr, "Usage: %s -o JSON|YAML|ENV|STANDARD\n", argv[0]);
                return 1;
            }

            break;
        }
        case 'p':
            equalizer_preset = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || equalizer_preset < 0 || equalizer_preset > 3) {
                fprintf(stderr, "Usage: %s -p 0-3, 0 is default\n", argv[0]);
                return 1;
            }
            break;
        case 'r':
            rotate_to_mute = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || endptr == optarg || rotate_to_mute < 0 || rotate_to_mute > 1) {
                fprintf(stderr, "Usage: %s -r 0|1\n", argv[0]);
                return 1;
            }
            break;
        case 's':
            sidetone_loudness = strtol(optarg, &endptr, 10);

            if (*endptr != '\0' || endptr == optarg || sidetone_loudness > 128 || sidetone_loudness < 0) {
                fprintf(stderr, "Usage: %s -s 0-128\n", argv[0]);
                return 1;
            }
            break;
        case 'u':
            print_udev_rules = 1;
            break;
        case 'v':
            voice_prompts = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || endptr == optarg || voice_prompts < 0 || voice_prompts > 1) {
                fprintf(stderr, "Usage: %s -v 0|1\n", argv[0]);
                return 1;
            }
            break;
        case '?':
            if (optopt == '?' || optopt == 0) {
                print_capabilities = 1;
            } else {
                // User issued an invalid option (stdlib will make an error message automatically)
                free(read_buffer);
                return 1;
            }
            break;
        case 'h':
            should_print_help = 1;
            break;
        case 0:
            if (strcmp(opts[option_index].name, "dev") == 0) {
                dev_mode = 1;
                break;
            } else if (strcmp(opts[option_index].name, "timeout") == 0) {
                hsc_device_timeout = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || hsc_device_timeout < 0 || hsc_device_timeout > 100000) {
                    fprintf(stderr, "Usage: %s --timeout 0-100000\n", argv[0]);
                    return 1;
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "microphone-mute-led-brightness") == 0) {
                microphone_mute_led_brightness = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || microphone_mute_led_brightness < 0 || microphone_mute_led_brightness > 3) {
                    fprintf(stderr, "Usage: %s --microphone-mute-led-brightness 0-3\n", argv[0]);
                    return 1;
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "microphone-volume") == 0) {
                microphone_volume = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || microphone_volume < 0 || microphone_volume > 128) {
                    fprintf(stderr, "Usage: %s --microphone-volume 0-128\n", argv[0]);
                    return 1;
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "volume-limiter") == 0) {
                volume_limiter = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || volume_limiter < 0 || volume_limiter > 1) {
                    fprintf(stderr, "Usage: %s --volume-limiter 0-1\n", argv[0]);
                    return 1;
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "bt-when-powered-on") == 0) {
                bt_when_powered_on = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || bt_when_powered_on < 0 || bt_when_powered_on > 1) {
                    fprintf(stderr, "Usage: %s --bt-when-powered-on 0-1\n", argv[0]);
                    return 1;
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "bt-call-volume") == 0) {
                bt_call_volume = strtol(optarg, &endptr, 10);

                if (*endptr != '\0' || endptr == optarg || bt_call_volume < 0 || bt_call_volume > 2) {
                    fprintf(stderr, "Usage: %s --bt-call-volume 0-2\n", argv[0]);
                    return 1;
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "connected") == 0) {
                request_connected = 1;
                break;
            } else if (strcmp(opts[option_index].name, "test-device") == 0) {
                test_device = 1;

                if (OPTIONAL_ARGUMENT_IS_PRESENT) {
                    test_profile = strtol(optarg, &endptr, 10);
                    if (test_profile < 0) {
                        fprintf(stderr, "Usage: %s --test-device [testprofile]\n", argv[0]);
                        return 1;
                    }
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "parametric-equalizer") == 0) {
                parametric_equalizer = parse_parametric_equalizer_settings(optarg);

                for (int i = 0; i < parametric_equalizer->size; i++) {
                    if ((int)parametric_equalizer->bands[i].type == HSC_INVALID_ARG) {
                        fprintf(stderr, "Unknown filter type specified to --parametric-equalizer\n");
                        return 1;
                    }
                }
                // fall through
            } else if (strcmp(opts[option_index].name, "readme-helper") == 0) {
                // We need to initialize it at this point
                init_devices();
                print_readmetable();
                return 0;
            } else if (strcmp(opts[option_index].name, "help-all") == 0) {
                should_print_help_all = 1;
            }
            break;
        default:
            fprintf(stderr, "Invalid argument %c\n", c);
            free(read_buffer);
            return 1;
        }
    }

    // Init all information of supported devices
    // Below getopt_long so that the testdevice has a chance to adjust parameters
    init_devices();

    free(read_buffer);

    if (print_udev_rules == 1) {
        fprintf(stderr, "Generating udev rules..\n\n");
        print_udevrules();
        return 0;
    }

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
    int headset_available = find_device(&device_found, test_device);

    if (should_print_help || should_print_help_all) {
        if (headset_available == 0)
            print_help(argv[0], &device_found, should_print_help_all);
        else
            print_help(argv[0], NULL, should_print_help_all);

        return 0;
    } else if (headset_available != 0) {
        output(NULL, false, output_format);
        return 1;
    }

    // We open connection to HID devices on demand
    hid_device* device_handle = NULL;
    char* hid_path            = NULL;

    // Initialize signal handler for CTRL + C
#ifdef _WIN32
    signal(SIGINT, interruptHandler);
#else
    struct sigaction act;
    act.sa_handler = interruptHandler;
    sigaction(SIGINT, &act, NULL);
#endif

    FeatureRequest featureRequests[] = {
        { CAP_SIDETONE, CAPABILITYTYPE_ACTION, &sidetone_loudness, sidetone_loudness != -1, {} },
        { CAP_LIGHTS, CAPABILITYTYPE_ACTION, &lights, lights != -1, {} },
        { CAP_NOTIFICATION_SOUND, CAPABILITYTYPE_ACTION, &notification_sound, notification_sound != -1, {} },
        { CAP_BATTERY_STATUS, CAPABILITYTYPE_INFO, &request_battery, request_battery == 1, {} },
        { CAP_INACTIVE_TIME, CAPABILITYTYPE_ACTION, &inactive_time, inactive_time != -1, {} },
        { CAP_CHATMIX_STATUS, CAPABILITYTYPE_INFO, &request_chatmix, request_chatmix == 1, {} },
        { CAP_VOICE_PROMPTS, CAPABILITYTYPE_ACTION, &voice_prompts, voice_prompts != -1, {} },
        { CAP_ROTATE_TO_MUTE, CAPABILITYTYPE_ACTION, &rotate_to_mute, rotate_to_mute != -1, {} },
        { CAP_EQUALIZER_PRESET, CAPABILITYTYPE_ACTION, &equalizer_preset, equalizer_preset != -1, {} },
        { CAP_MICROPHONE_MUTE_LED_BRIGHTNESS, CAPABILITYTYPE_ACTION, &microphone_mute_led_brightness, microphone_mute_led_brightness != -1, {} },
        { CAP_MICROPHONE_VOLUME, CAPABILITYTYPE_ACTION, &microphone_volume, microphone_volume != -1, {} },
        { CAP_EQUALIZER, CAPABILITYTYPE_ACTION, equalizer, equalizer != NULL, {} },
        { CAP_PARAMETRIC_EQUALIZER, CAPABILITYTYPE_ACTION, parametric_equalizer, parametric_equalizer != NULL, {} },
        { CAP_VOLUME_LIMITER, CAPABILITYTYPE_ACTION, &volume_limiter, volume_limiter != -1, {} },
        { CAP_BT_WHEN_POWERED_ON, CAPABILITYTYPE_ACTION, &bt_when_powered_on, bt_when_powered_on != -1, {} },
        { CAP_BT_CALL_VOLUME, CAPABILITYTYPE_ACTION, &bt_call_volume, bt_call_volume != -1, {} }
    };
    int numFeatures = sizeof(featureRequests) / sizeof(featureRequests[0]);
    assert(numFeatures == NUM_CAPABILITIES);

    // For specific output types, like YAML, we will do all actions - even when not specified - to aggreate all information
    if (output_format == OUTPUT_YAML || output_format == OUTPUT_JSON || output_format == OUTPUT_ENV) {
        for (int i = 0; i < numFeatures; i++) {
            if (featureRequests[i].type == CAPABILITYTYPE_INFO && !featureRequests[i].should_process) {
                if ((device_found.capabilities & B(featureRequests[i].cap)) == B(featureRequests[i].cap)) {
                    featureRequests[i].should_process = true;
                }
            }
        }
    }

    if (request_connected) {
        if (test_device) {
            printf("true\n");
            return 0;
        }

        // Check if battery status can be read
        // If it isn't supported, the device is
        // probably wired meaning it is connected
        int battery_error = 0;

        if ((device_found.capabilities & B(CAP_BATTERY_STATUS)) == B(CAP_BATTERY_STATUS)) {
            device_handle = dynamic_connect(&hid_path, device_handle, &device_found, CAP_BATTERY_STATUS);
            if (!device_handle)
                return 1;

            BatteryInfo info = device_found.request_battery(device_handle);

            if (info.status != BATTERY_AVAILABLE) {
                battery_error = 1;
            }
        }

        terminate_hid(&device_handle, &hid_path);

        if (battery_error != 0) {
            printf("false\n");
            return 1;
        } else {
            printf("true\n");
            return 0;
        }
    }

    do {
        for (int i = 0; i < numFeatures; i++) {
            if (featureRequests[i].should_process) {
                // Assuming handle_feature now returns FeatureResult
                featureRequests[i].result = handle_feature(&device_found, &device_handle, &hid_path, featureRequests[i].cap, featureRequests[i].param);
            } else {
                // Populate with a default "not processed" result
                featureRequests[i].result.status  = FEATURE_NOT_PROCESSED;
                featureRequests[i].result.message = strdup("Not processed");
                featureRequests[i].result.value   = 0;
            }
        }

        DeviceList deviceList;
        deviceList.device          = &device_found;
        deviceList.num_devices     = 1;
        deviceList.featureRequests = featureRequests;
        deviceList.size            = numFeatures;

        output(&deviceList, print_capabilities != -1, output_format);

        if (follow)
            sleep(follow_sec);

    } while (follow);

    // Free memory from features
    for (int i = 0; i < numFeatures; i++) {
        free(featureRequests[i].result.message);
    }

    if (equalizer != NULL) {
        free(equalizer->bands_values);
        free(equalizer);
    }

    if (parametric_equalizer != NULL) {
        free(parametric_equalizer->bands);
        free(parametric_equalizer);
    }

    terminate_hid(&device_handle, &hid_path);
    return 0;
}

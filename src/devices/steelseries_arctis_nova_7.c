#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSG_SIZE 64

static struct device device_arctis;

#define ID_arctis_nova_7     0x2202

#define BATTERY_MAX 0x04
#define BATTERY_MIN 0x00

#define HEADSET_OFFLINE 0x00

static const uint16_t PRODUCT_IDS[] = { ID_arctis_nova_7 };

static int arctis_nova_7_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_nova_7_send_inactive_time(hid_device* device_handle, uint8_t num);
static int arctis_nova_7_send_equalizer_preset(hid_device* device_handle, uint8_t num);
static int arctis_nova_7_request_battery(hid_device* device_handle);

int arctis_nova_7_read_device_status(hid_device* device_handle, unsigned char* data_read);

void arctis_nova_7_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 7", sizeof(device_arctis.device_name));

    device_arctis.capabilities                             = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER_PRESET);
    device_arctis.capability_details[CAP_SIDETONE]         = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]   = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]    = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };

    device_arctis.send_sidetone         = &arctis_nova_7_send_sidetone;
    device_arctis.request_battery       = &arctis_nova_7_request_battery;
    device_arctis.send_inactive_time    = &arctis_nova_7_send_inactive_time;
    device_arctis.send_equalizer_preset = &arctis_nova_7_send_equalizer_preset;

    *device = &device_arctis;
}

static int arctis_nova_7_send_sidetone(hid_device* device_handle, uint8_t num)
{
    if (num < 26) {
        num = 0x0;
    } else if (num < 51) {
        num = 0x1;
    } else if (num < 76) {
        num = 0x2;
    } else {
        num = 0x3;
    }

    uint8_t data[MSG_SIZE] = { 0x00, 0x39, num };

    return hid_write(device_handle, data, MSG_SIZE);
}

static int arctis_nova_7_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    return 0;
    uint8_t data[MSG_SIZE] = { 0x00, 0xa3, num };
    /*
     * During setting this part headphones my lose paring with transmitter, to fix this issue follow instruction in this link
     * https://support.steelseries.com/hc/en-us/articles/115000051472-The-SteelSeries-Engine-says-Reconnect-Headset-but-my-transmitter-is-connected-
     *
     0x00 never
     0x01 1m
     0x05 5m
     0x0a 10m
     0x0f 15m
     0x1e 30m
     0x2d 45m
     */

    return hid_write(device_handle, data, MSG_SIZE);
}

static int arctis_nova_7_request_battery(hid_device* device_handle)
{
    // read device info
    unsigned char data_read[6];
    int r = arctis_nova_7_read_device_status(device_handle, data_read);

//developer corner
    /*char tmp[128];
    size_t rc = hexdump(tmp, sizeof(tmp), data_read, sizeof(data_read));
    printf("rc: %zx, %s\n", rc, tmp);*/

// data_read[1] has state 0x03 when headphones are power on and 0x02 when off
// data_read[5] and data_read[6] offline state [0xff, 0xff] when power on [0x64, 0x64]

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    if (data_read[3] == HEADSET_OFFLINE)
        return BATTERY_UNAVAILABLE;

    if (data_read[3] == 0x01)
        return BATTERY_CHARGING;

    int bat = data_read[2];

    if (bat > BATTERY_MAX)
        return 100;

    return map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
}

static int arctis_nova_7_send_equalizer_preset(hid_device* device_handle, uint8_t num)
{
    // flat (default), bass boost, smiley, focus

    switch (num) {
    case 0: {
        uint8_t flat[MSG_SIZE] = { 0x0, 0x33, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x0 };
        return hid_write(device_handle, flat, MSG_SIZE);
    }
    case 1: {
        uint8_t bass[MSG_SIZE] = { 0x0, 0x33, 0x1b, 0x1f, 0x1c, 0x16, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12, 0x0 };
        return hid_write(device_handle, bass, MSG_SIZE);
    }
    case 2: {
        uint8_t smiley[MSG_SIZE] = { 0x0, 0x33, 0x0a, 0x0d, 0x12, 0x0d, 0x0f, 0x1c, 0x20, 0x1b, 0x0d, 0x14, 0x0 };
        return hid_write(device_handle, smiley, MSG_SIZE);
    }
    case 3: {
        uint8_t focus[MSG_SIZE] = { 0x0, 0x33, 0x1a, 0x1b, 0x17, 0x11, 0x0c, 0x0c, 0x0f, 0x17, 0x1a, 0x1c, 0x0 };
        return hid_write(device_handle, focus, MSG_SIZE);
    }
    default: {
        printf("Device only supports 0-3 range for presets.\n");
        return HSC_OUT_OF_BOUNDS;
    }
    }
}

int arctis_nova_7_read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    unsigned char data_request[2] = { 0x00, 0xb0 };
    int r = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    return hid_read_timeout(device_handle, data_read, 6, hsc_device_timeout);
}

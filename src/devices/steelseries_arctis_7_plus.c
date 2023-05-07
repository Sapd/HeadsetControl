#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSG_SIZE 64

static struct device device_arctis;

#define ID_ARCTIS_7_PLUS         0x220e
#define ID_ARCTIS_7_PLUS_PS5     0x2212
#define ID_ARCTIS_7_PLUS_XBOX    0x2216
#define ID_ARCTIS_7_PLUS_DESTINY 0x2236

#define BATTERY_MAX 0x04
#define BATTERY_MIN 0x00

#define HEADSET_OFFLINE 0x01
#define STATUS_BUF_SIZE 6

#define EQUALIZER_BANDS_SIZE 10

static const uint16_t PRODUCT_IDS[] = { ID_ARCTIS_7_PLUS, ID_ARCTIS_7_PLUS_PS5, ID_ARCTIS_7_PLUS_XBOX, ID_ARCTIS_7_PLUS_DESTINY };

static int arctis_7_plus_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_7_plus_send_inactive_time(hid_device* device_handle, uint8_t num);
static int arctis_7_plus_send_equalizer_preset(hid_device* device_handle, uint8_t num);
static int arctis_7_plus_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings);
static int arctis_7_plus_request_battery(hid_device* device_handle);
static int arctis_7_plus_request_chatmix(hid_device* device_handle);

int arctis_7_plus_read_device_status(hid_device* device_handle, unsigned char* data_read);

void arctis_7_plus_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis 7+", sizeof(device_arctis.device_name));

    device_arctis.capabilities                             = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET);
    device_arctis.capability_details[CAP_SIDETONE]         = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]   = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_CHATMIX_STATUS]   = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]    = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER]        = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };

    device_arctis.send_sidetone         = &arctis_7_plus_send_sidetone;
    device_arctis.request_battery       = &arctis_7_plus_request_battery;
    device_arctis.request_chatmix       = &arctis_7_plus_request_chatmix;
    device_arctis.send_inactive_time    = &arctis_7_plus_send_inactive_time;
    device_arctis.send_equalizer_preset = &arctis_7_plus_send_equalizer_preset;
    device_arctis.send_equalizer        = &arctis_7_plus_send_equalizer;

    *device = &device_arctis;
}

static int arctis_7_plus_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // num, will be from 0 to 128, we need to map it to the correct value
    // the range of the Arctis 7+ is from 0 to 3
    // steps to make it work nicely with headset-charge-indicator

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

static int arctis_7_plus_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // as the value is in minutes, mapping to a different range does not make too much sense here
    // the range of the Arctis 7+ is from 0 to 0x5A (90)

    uint8_t data[MSG_SIZE] = { 0x00, 0xa3, num };

    return hid_write(device_handle, data, MSG_SIZE);
}

static int arctis_7_plus_request_battery(hid_device* device_handle)
{
    // read device info
    unsigned char data_read[STATUS_BUF_SIZE];
    int r = arctis_7_plus_read_device_status(device_handle, data_read);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    if (data_read[1] == HEADSET_OFFLINE)
        return BATTERY_UNAVAILABLE;

    if (data_read[3] == 0x01)
        return BATTERY_CHARGING;

    int bat = data_read[2];

    if (bat > BATTERY_MAX)
        return 100;

    return map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
}

static int arctis_7_plus_request_chatmix(hid_device* device_handle)
{
    // request for setting new mix 0x45
    // max chat 0x00, 0x64
    // max game 0x64, 0x00
    // neutral 0x64, 0x64
    // read device info
    unsigned char data_read[STATUS_BUF_SIZE];
    int r = arctis_7_plus_read_device_status(device_handle, data_read);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    // it's a slider, but setting for game and chat
    // are reported as separate values, we combine
    // them back into one setting of the slider

    // the two values are between 0 and 100,
    // we translate that to a value from 0 to 128
    // with "64" being in the middle

    int game = map(data_read[4], 0, 100, 0, 64);
    int chat = map(data_read[5], 0, 100, 0, -64);

    return 64 - (chat + game);
}

static int arctis_7_plus_send_equalizer_preset(hid_device* device_handle, uint8_t num)
{
    // This headset supports only 4 presets:
    // flat (default), bass boost, smiley, focus

    switch (num) {
    case 0: {

        uint8_t flat[MSG_SIZE] = { 0x0, 0x33, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0 };
        return hid_write(device_handle, flat, MSG_SIZE);
    }
    case 1: {
        uint8_t bass[MSG_SIZE] = { 0x0, 0x33, 0x1f, 0x20, 0x1a, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x23, 0x0 };
        return hid_write(device_handle, bass, MSG_SIZE);
    }
    case 2: {
        uint8_t smiley[MSG_SIZE] = { 0x0, 0x33, 0x1e, 0x1b, 0x15, 0x10, 0x10, 0x13, 0x1b, 0x1e, 0x20, 0x1f, 0x0 };
        return hid_write(device_handle, smiley, MSG_SIZE);
    }
    case 3: {
        uint8_t focus[MSG_SIZE] = { 0x0, 0x33, 0x0e, 0x16, 0x11, 0x13, 0x20, 0x24, 0x1f, 0x11, 0x18, 0x11, 0x0 };
        return hid_write(device_handle, focus, MSG_SIZE);
    }
    default: {
        printf("Device only supports 0-3 range for presets.\n");
        return HSC_OUT_OF_BOUNDS;
    }
    }
}

static int arctis_7_plus_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings)
{
    if (settings->size != EQUALIZER_BANDS_SIZE) {
        printf("Device only supports %d bands.\n", EQUALIZER_BANDS_SIZE);
        return HSC_OUT_OF_BOUNDS;
    }

    uint8_t data[MSG_SIZE] = { 0x0, 0x33 };
    for (int i = 0; i < settings->size; i++) {
        data[i + 2] = (uint8_t)settings->bands_values[i];
    }
    data[settings->size + 3] = 0x0;

    return hid_write(device_handle, data, MSG_SIZE);
}

int arctis_7_plus_read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    unsigned char data_request[2] = { 0x00, 0xb0 };
    int r                         = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    return hid_read_timeout(device_handle, data_read, STATUS_BUF_SIZE, hsc_device_timeout);
}

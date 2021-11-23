#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <stdio.h>

static struct device device_arctis;

#define ID_ARCTIS_9 0x12c2

#define BATTERY_MAX 0x9A
#define BATTERY_MIN 0x64

static const uint16_t PRODUCT_IDS[] = { ID_ARCTIS_9 };

static int arctis_9_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_9_request_battery(hid_device* device_handle);
static int arctis_9_send_inactive_time(hid_device* device_handle, uint8_t num);
static int arctis_9_request_chatmix(hid_device* device_handle);

int arctis_9_save_state(hid_device* device_handle);
int arctis_9_read_device_status(hid_device* device_handle, unsigned char* data_read);

void arctis_9_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis 9", sizeof(device_arctis.device_name));

    device_arctis.capabilities       = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_CHATMIX_STATUS);
    device_arctis.send_sidetone      = &arctis_9_send_sidetone;
    device_arctis.request_battery    = &arctis_9_request_battery;
    device_arctis.send_inactive_time = &arctis_9_send_inactive_time;
    device_arctis.request_chatmix    = &arctis_9_request_chatmix;

    *device = &device_arctis;
}

static int arctis_9_send_sidetone(hid_device* device_handle, uint8_t num)
{
    int ret = -1;

    // transform the log scale sidetone into a more intuitive linear scale
    num = map((int)log2(num) * 100, 0, 700, 0, 128);

    // the range of the Arctis 9 seems to be from 0xc0 (192, also off) to 0xfd (253)
    // and scales exponential
    num = map(num, 0, 128, 0xc0, 0xfd);

    unsigned char* buf = calloc(31, 1);

    if (!buf) {
        return ret;
    }

    const unsigned char data_on[5]  = { 0x06, 0x00, num };
    const unsigned char data_off[3] = { 0x06, 0x00, 0xc0 };

    if (num) {
        memmove(buf, data_on, sizeof(data_on));
    } else {
        memmove(buf, data_off, sizeof(data_off));
    }

    ret = hid_write(device_handle, buf, 31);

    free(buf);

    if (ret >= 0) {
        ret = arctis_9_save_state(device_handle);
    }

    return ret;
}

static int arctis_9_request_battery(hid_device* device_handle)
{
    // read device info
    unsigned char data_read[12];
    int r = arctis_9_read_device_status(device_handle, data_read);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    if (data_read[4] == 0x01)
        return BATTERY_CHARGING;

    int bat = data_read[3];

    if (bat > BATTERY_MAX)
        return 100;

    return map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
}

static int arctis_9_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // the value for the Arctis 9 needs to be in seconds
    uint32_t time    = num * 60;
    uint8_t data[31] = { 0x04, 0x00, (uint8_t)(time >> 8), (uint8_t)(time) };

    int ret = hid_write(device_handle, data, 31);

    if (ret >= 0) {
        ret = arctis_9_save_state(device_handle);
    }

    return ret;
}

static int arctis_9_request_chatmix(hid_device* device_handle)
{
    // read device info
    unsigned char data_read[12];
    int r = arctis_9_read_device_status(device_handle, data_read);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    int game = map(data_read[9], 0, 19, 0, 64);
    int chat = map(data_read[10], 0, 19, 0, -64);

    return 64 - (chat + game);
}

int arctis_9_save_state(hid_device* device_handle)
{
    uint8_t data[31] = { 0x90, 0x00 };

    return hid_write(device_handle, data, 31);
}

int arctis_9_read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    int r = 0;

    unsigned char data_request[2] = { 0x20, 0x00 };
    r                             = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    // read device info
    return hid_read_timeout(device_handle, data_read, 12, hsc_device_timeout);
}

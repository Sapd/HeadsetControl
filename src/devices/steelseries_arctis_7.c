#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdlib.h>
#include <string.h>

static struct device device_arctis;

#define ID_ARCTIS_7        0x1260
#define ID_ARCTIS_7_2019   0x12ad
#define ID_ARCTIS_PRO_2019 0x1252

static const uint16_t PRODUCT_IDS[] = { ID_ARCTIS_7, ID_ARCTIS_7_2019, ID_ARCTIS_PRO_2019 };

static int arctis_7_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_7_request_battery(hid_device* device_handle);
static int arctis_7_send_inactive_time(hid_device* device_handle, uint8_t num);
static int arctis_7_request_chatmix(hid_device* device_handle);
static int arctis_7_switch_lights(hid_device* device_handle, uint8_t on);

static int arctis_7_save_state(hid_device* device_handle);

void arctis_7_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis (7/Pro)", sizeof(device_arctis.device_name));

    device_arctis.capabilities = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME)
        | B(CAP_CHATMIX_STATUS) | B(CAP_LIGHTS);
    device_arctis.capability_details[CAP_SIDETONE]       = (struct capability_detail) { .interface = 0x05 };
    device_arctis.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .interface = 0x05 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]  = (struct capability_detail) { .interface = 0x05 };
    device_arctis.capability_details[CAP_CHATMIX_STATUS] = (struct capability_detail) { .interface = 0x05 };
    device_arctis.capability_details[CAP_LIGHTS]         = (struct capability_detail) { .interface = 0x05 };

    device_arctis.send_sidetone      = &arctis_7_send_sidetone;
    device_arctis.request_battery    = &arctis_7_request_battery;
    device_arctis.send_inactive_time = &arctis_7_send_inactive_time;
    device_arctis.request_chatmix    = &arctis_7_request_chatmix;
    device_arctis.switch_lights      = &arctis_7_switch_lights;

    *device = &device_arctis;
}

static int arctis_7_send_sidetone(hid_device* device_handle, uint8_t num)
{
    int ret = -1;

    // the range of the Arctis 7 seems to be from 0 to 0x12 (18)
    num = map(num, 0, 128, 0x00, 0x12);

    unsigned char* buf = calloc(31, 1);

    if (!buf) {
        return ret;
    }

    const unsigned char data_on[5]  = { 0x06, 0x35, 0x01, 0x00, num };
    const unsigned char data_off[2] = { 0x06, 0x35 };

    if (num) {
        memmove(buf, data_on, sizeof(data_on));
    } else {
        memmove(buf, data_off, sizeof(data_off));
    }

    ret = hid_write(device_handle, buf, 31);

    free(buf);

    if (ret >= 0) {
        ret = arctis_7_save_state(device_handle);
    }

    return ret;
}

static int arctis_7_request_battery(hid_device* device_handle)
{

    int r = 0;

    // request battery status
    unsigned char data_request[2] = { 0x06, 0x18 };

    r = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    // read battery status
    unsigned char data_read[8];

    r = hid_read_timeout(device_handle, data_read, 8, hsc_device_timeout);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    int bat = data_read[2];

    if (bat > 100)
        return 100;

    return bat;
}

static int arctis_7_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // as the value is in minutes, mapping to a different range does not make too much sense here
    // the range of the Arctis 7 seems to be from 0 to 0x5A (90)
    // num = map(num, 0, 128, 0x00, 0x5A);

    uint8_t data[31] = { 0x06, 0x51, num };

    int ret = hid_write(device_handle, data, 31);

    if (ret >= 0) {
        ret = arctis_7_save_state(device_handle);
    }

    return ret;
}

int arctis_7_save_state(hid_device* device_handle)
{
    uint8_t data[31] = { 0x06, 0x09 };

    return hid_write(device_handle, data, 31);
}

static int arctis_7_request_chatmix(hid_device* device_handle)
{
    int r = 0;

    // request chatmix level
    unsigned char data_request[2] = { 0x06, 0x24 };

    r = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    // read chatmix level
    unsigned char data_read[8];

    r = hid_read_timeout(device_handle, data_read, 8, hsc_device_timeout);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    int game = data_read[2];
    int chat = data_read[3];

    // it's a slider, but setting for game and chat
    // are reported as separate values, we combine
    // them back into one setting of the slider

    // the two values are between 255 and 191,
    // we translate that to a value from 0 to 127
    // with "64" being in the middle
    if (game == 0 && chat == 0) {
        return 64;
    }

    if (game == 0) {
        return 64 + 255 - chat;
    }

    return 64 + (-1) * (255 - game);
}

static int arctis_7_switch_lights(hid_device* device_handle, uint8_t on)
{
    unsigned char data[8] = { 0x06, 0x55, 0x01, on ? 0x02 : 0x00 };
    int ret               = hid_write(device_handle, data, 8);

    if (ret >= 0) {
        ret = arctis_7_save_state(device_handle);
    }

    return ret;
}

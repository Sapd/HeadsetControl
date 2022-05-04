#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <stdio.h>

static struct device device_arctis;

#define ID_ARCTIS_PRO_WIRELESS 0x1290

#define BATTERY_MAX 0x04
#define BATTERY_MIN 0x00

#define HEADSET_ONLINE  0x04
#define HEADSET_OFFLINE 0x02

static const uint16_t PRODUCT_IDS[] = { ID_ARCTIS_PRO_WIRELESS };

static int arctis_pro_wireless_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_pro_wireless_request_battery(hid_device* device_handle);
static int arctis_pro_wireless_send_inactive_time(hid_device* device_handle, uint8_t num);

int arctis_pro_wireless_read_device_status(hid_device* device_handle, unsigned char* data_read);
int arctis_pro_wireless_save_state(hid_device* device_handle);

void arctis_pro_wireless_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Pro Wireless", sizeof(device_arctis.device_name));

    device_arctis.capabilities       = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    device_arctis.send_sidetone      = &arctis_pro_wireless_send_sidetone;
    device_arctis.request_battery    = &arctis_pro_wireless_request_battery;
    device_arctis.send_inactive_time = &arctis_pro_wireless_send_inactive_time;

    *device = &device_arctis;
}

static int arctis_pro_wireless_send_sidetone(hid_device* device_handle, uint8_t num)
{
    int ret = -1;

    // the range of the Arctis Pro Wireless seems to be from 0x00 (also off) to 0x09
    num = map(num, 0, 128, 0x00, 0x09);

    const unsigned char data_request[31] = { 0x39, 0xAA, num };
    ret                                  = hid_write(device_handle, data_request, 31);

    if (ret >= 0) {
        ret = arctis_pro_wireless_save_state(device_handle);
    }

    return ret;
}

static int arctis_pro_wireless_request_battery(hid_device* device_handle)
{
    int r = 0;
    unsigned char data_read[2];

    // First check if the headset is connected
    r = arctis_pro_wireless_read_device_status(device_handle, data_read);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    if (data_read[0] == HEADSET_OFFLINE)
        return BATTERY_UNAVAILABLE;

    // If it is, request the battery level
    unsigned char data_request[31] = { 0x40, 0xAA };
    r                              = hid_write(device_handle, data_request, 31);
    if (r < 0)
        return r;

    r = hid_read_timeout(device_handle, data_read, 1, hsc_device_timeout);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    int bat = data_read[0];
    return map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
}

static int arctis_pro_wireless_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    int ret = -1;

    // the value sent over the wire is the number of ten minute increments
    uint32_t time    = num / 10;
    uint8_t data[31] = { 0x3c, 0xAA, (uint8_t)(time) };

    ret = hid_write(device_handle, data, 31);
    if (ret >= 0) {
        ret = arctis_pro_wireless_save_state(device_handle);
    }

    return ret;
}

int arctis_pro_wireless_read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    int r = 0;

    unsigned char data_request[31] = { 0x41, 0xAA };
    r                              = hid_write(device_handle, data_request, 31);

    if (r < 0)
        return r;

    // read device info
    return hid_read_timeout(device_handle, data_read, 2, hsc_device_timeout);
}

int arctis_pro_wireless_save_state(hid_device* device_handle)
{
    uint8_t data[31] = { 0x90, 0xAA };

    return hid_write(device_handle, data, 31);
}

#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <hidapi.h>
#include <string.h>

#define BATTERY_MAX 91
#define BATTERY_MIN 44
#define MSG_SIZE    64

static struct device device_g930;

static const uint16_t PRODUCT_ID = 0x0a1f;

static int g930_send_sidetone(hid_device* device_handle, uint8_t num);
static int g930_request_battery(hid_device* device_handle);

void g930_init(struct device** device)
{
    device_g930.idVendor            = VENDOR_LOGITECH;
    device_g930.idProductsSupported = &PRODUCT_ID;
    device_g930.numIdProducts       = 1;

    strncpy(device_g930.device_name, "Logitech G930", sizeof(device_g930.device_name));

    device_g930.capabilities = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    /// TODO: usagepages and ids may not be correct for all features
    device_g930.capability_details[CAP_SIDETONE]       = (struct capability_detail) { .usagepage = 0xff00, .usageid = 0x1 };
    device_g930.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xff00, .usageid = 0x1 };

    device_g930.send_sidetone   = &g930_send_sidetone;
    device_g930.request_battery = &g930_request_battery;

    *device = &device_g930;
}

static int g930_send_sidetone(hid_device* device_handle, uint8_t num)
{
    uint8_t data[MSG_SIZE] = { 0xFF, 0x0A, 0, 0xFF, 0xF4, 0x10, 0x05, 0xDA, 0x8F, 0xF2, 0x01, num, 0, 0, 0, 0 };

    for (int i = 16; i < MSG_SIZE; i++)
        data[i] = 0;

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

static int g930_request_battery(hid_device* device_handle)
{
    /*
        CREDIT GOES TO https://github.com/Roliga for the project
        https://github.com/Roliga/g930-battery-percentage/blob/master/main.c
        I've simply ported that implementation to this project!
    */

    unsigned char buf[MSG_SIZE] = { 0xFF, 0x09, 0x00, 0xFD, 0xF4, 0x10, 0x05, 0xB1, 0xBF, 0xA0, 0x04 };
    for (int i = 11; i < MSG_SIZE; i++)
        buf[i] = 0;

    int res = hid_send_feature_report(device_handle, buf, MSG_SIZE);
    if (res < 0) {
        return res;
    }

    for (int i = 0; i < 3; i++) {
        res = hid_get_feature_report(device_handle, buf, MSG_SIZE);

        if (res < 0) {
            return res;
        }

        if (i < 2) {
            usleep(100000);
        }
    }
    int batteryPercent = map((int)buf[13], BATTERY_MIN, BATTERY_MAX, 0, 100);
    return batteryPercent;
}

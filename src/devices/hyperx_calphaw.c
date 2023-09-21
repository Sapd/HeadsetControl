// Based on hyperx_cflight.c
// Most requests taken off https://github.com/lehnification/hyperx-cloud-alpha-wireless-tray unless stated otherwise

// This file is part of HeadsetControl.

#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// #define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif

static struct device device_calphaw;

// timeout for hidapi requests in milliseconds
#define TIMEOUT 2000

#define VENDOR_HP  0x03f0 // cloud alpha sold with HP, Inc vendor id
#define ID_CALPHAW 0x098d

static const uint16_t PRODUCT_IDS[] = { ID_CALPHAW };

static int calphaw_request_battery(hid_device* device_handle);
static int calphaw_send_inactive_time(hid_device* device_handle, uint8_t num);
static int calphaw_send_sidetone(hid_device* device_handle, uint8_t num);
static int calphaw_switch_voice_prompts(hid_device* device_handle, uint8_t on);

void calphaw_init(struct device** device)
{
    device_calphaw.idVendor            = VENDOR_HP;
    device_calphaw.idProductsSupported = PRODUCT_IDS;
    device_calphaw.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    strncpy(device_calphaw.device_name, "HyperX Cloud Alpha Wireless", sizeof(device_calphaw.device_name));

    device_calphaw.capabilities         = B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_SIDETONE) | B(CAP_VOICE_PROMPTS);
    device_calphaw.request_battery      = &calphaw_request_battery;
    device_calphaw.send_inactive_time   = &calphaw_send_inactive_time;
    device_calphaw.send_sidetone        = &calphaw_send_sidetone;
    device_calphaw.switch_voice_prompts = &calphaw_switch_voice_prompts;

    *device = &device_calphaw;
}

int calphaw_request_connection(hid_device* device_handle)
{
    int r = 0;
    // request charge info
    uint8_t data_request[31] = { 0x21, 0xbb, 0x03 };

    r = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    if (r < 0)
        return r;

    uint8_t data_read[31];
    r = hid_read_timeout(device_handle, data_read, 20, TIMEOUT);
    if (r < 0)
        return r;
    if (r == 0) // timeout
        return HSC_ERROR;

    if (r == 0xf || r == 0x14) {
        return data_read[3];
    }

    return HSC_ERROR;
}

int calphaw_request_charge(hid_device* device_handle)
{
    int r = 0;
    // request charge info
    uint8_t data_request[31] = { 0x21, 0xbb, 0x0c };

    r = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    if (r < 0)
        return r;

    uint8_t data_read[31];
    r = hid_read_timeout(device_handle, data_read, 20, TIMEOUT);
    if (r < 0)
        return r;
    if (r == 0) // timeout
        return HSC_ERROR;

    if (r == 0xf || r == 0x14) {
        if (data_read[3] == 0x01)
            return 1;

        return 0;
    }

    return HSC_ERROR;
}

static int calphaw_request_battery(hid_device* device_handle)
{
    if (calphaw_request_connection(device_handle) == 1)
        return BATTERY_UNAVAILABLE;

    if (calphaw_request_charge(device_handle) == 1)
        return BATTERY_CHARGING;

    int r = 0;
    // request battery info
    uint8_t data_request[31] = { 0x21, 0xbb, 0x0b }; // Data request reverse engineered by using USBPcap and Wireshark by @InfiniteLoop

    r = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    if (r < 0)
        return r;

    uint8_t data_read[31];
    r = hid_read_timeout(device_handle, data_read, 20, TIMEOUT);
    if (r < 0)
        return r;
    if (r == 0) // timeout
        return HSC_ERROR;

    if (r == 0xf || r == 0x14) {

#ifdef DEBUG
        printf("calphaw_request_battery data_read 3: 0x%02x 4: 0x%02x 5: 0x%02x\n", data_read[3], data_read[4], data_read[5]);
#endif

        uint8_t batteryPercentage = data_read[3];

#ifdef DEBUG
        uint32_t batteryVoltage = (data_read[4] << 8) | data_read[5]; // best assumtion this is voltage in mV
        printf("batteryVoltage: %d mV\n", batteryVoltage);
#endif
        return (int)(batteryPercentage);
    }

    return HSC_ERROR;
}

static int calphaw_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // taken from steelseries_arctis_7.c

    uint8_t data_request[31] = { 0x21, 0xbb, 0x12, num };

    int ret = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));

    return ret;
}

int calphaw_toggle_sidetone(hid_device* device_handle, uint8_t num)
{
    uint8_t data_request[31] = { 0x21, 0xbb, 0x10, num };

    return hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    ;
}

static int calphaw_send_sidetone(hid_device* device_handle, uint8_t num)
{
    if (num == 0) {
        return calphaw_toggle_sidetone(device_handle, 0);
    } else {
        calphaw_toggle_sidetone(device_handle, 1);
        uint8_t data_request[31] = { 0x21, 0xbb, 0x11, num };

        int ret = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));

        // looks to be a binary setting but I'm not sure

        return ret;
    }

    return HSC_ERROR;
}

static int calphaw_switch_voice_prompts(hid_device* device_handle, uint8_t on)
{
    uint8_t data_request[31] = { 0x21, 0xbb, 0x13, on };

    return hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    ;
}

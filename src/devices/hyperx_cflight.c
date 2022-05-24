// initial adaption of HyperX Cloud Flight by hede <github5562@der-he.de>
// derived from some other headset device files
// with Help from Søren Brokær and https://github.com/srn/hyperx-cloud-flight-wireless

// This file is part of HeadsetControl.

#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif

static struct device device_cflight;

// timeout for hidapi requests in milliseconds
#define TIMEOUT 2000

#define VENDOR_HYPERX  0x0951
#define ID_CFLIGHT_OLD 0x16C4
#define ID_CFLIGHT_NEW 0x1723

static const uint16_t PRODUCT_IDS[] = { ID_CFLIGHT_OLD, ID_CFLIGHT_NEW };

static int cflight_request_battery(hid_device* device_handle);

void cflight_init(struct device** device)
{
    device_cflight.idVendor            = VENDOR_HYPERX;
    device_cflight.idProductsSupported = PRODUCT_IDS;
    device_cflight.numIdProducts       = sizeof(PRODUCT_IDS)/sizeof(PRODUCT_IDS[0]);
    strncpy(device_cflight.device_name, "HyperX Cloud Flight Wireless",
            sizeof(device_cflight.device_name));

    device_cflight.capabilities    = B(CAP_BATTERY_STATUS);
    device_cflight.request_battery = &cflight_request_battery;

    *device = &device_cflight;
}

static float estimate_battery_level(uint16_t voltage)
{
    // derived from logitech_g633_g933_935.c

    if (voltage <= 3648)
        return (float)(0.00125 * voltage);
    if (voltage > 3975)
        return (float)100.0;
    return (float)(0.00000002547505 * pow(voltage, 4) - 0.0003900299 * pow(voltage, 3) + 2.238321 * pow(voltage, 2) - 5706.256 * voltage + 5452299);
}

static int cflight_request_battery(hid_device* device_handle)
{
    int r = 0;
    // request battery voltage
    uint8_t data_request[] = {0x21, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    r = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    if (r < 0)
        return r;

    uint8_t data_read[20];
    r = hid_read_timeout(device_handle, data_read, 20, TIMEOUT);
    if (r < 0)
        return r;
    if (r == 0) // timeout
        return HSC_ERROR;

    if (r == 0xf || r == 0x14) {

#ifdef DEBUG
        printf("cflight_request_battery data_read 3: 0x%02x 4: 0x%02x\n", data_read[3], data_read[4]);
#endif

        // battery voltage in millivolts
        uint32_t batteryVoltage = data_read[4] | data_read[3] << 8;

#ifdef DEBUG
        printf("batteryVoltage: %d mV\n", batteryVoltage);
#endif

        if (batteryVoltage > 0x100B)
            return BATTERY_CHARGING;

        return (int)(roundf(estimate_battery_level(batteryVoltage)));
    }

    // we've read other functionality here (other read length)
    // which is currently not supported
    return HSC_ERROR;
}


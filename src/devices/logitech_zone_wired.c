#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <hidapi.h>
#include <string.h>

#define MSG_SIZE    20

static struct device device_zone_wired;

static const uint16_t PRODUCT_ID = 0x0aad;

static int zone_wired_send_sidetone(hid_device* device_handle, uint8_t num);

void zone_wired_init(struct device** device)
{
    device_zone_wired.idVendor = VENDOR_LOGITECH;
    device_zone_wired.idProductsSupported = &PRODUCT_ID;
    device_zone_wired.numIdProducts = 1;
    device_zone_wired.idUsage = 0x0;

    strncpy(device_zone_wired.device_name, "Logitech Zone Wired", sizeof(device_zone_wired.device_name));

    device_zone_wired.capabilities = CAP_SIDETONE;
    device_zone_wired.send_sidetone = &zone_wired_send_sidetone;

    *device = &device_zone_wired;
}

static int zone_wired_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // The sidetone volume of the Zone Wired is configured in steps of 10%, with 0x00 = 0% and 0x0A = 100%
    uint8_t raw_volume = map(num, 0, 128, 0, 10);
    uint8_t data[MSG_SIZE] = { 0x22, 0xF1, 0x04, 0x00, 0x04, 0x3d, raw_volume, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

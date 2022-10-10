#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <math.h>
#include <string.h>

#define MSG_SIZE 20

static struct device device_g535;

static const uint16_t PRODUCT_ID = 0x0ac4;

static int g535_send_sidetone(hid_device* device_handle, uint8_t num);

void g535_init(struct device** device)
{
    device_g535.idVendor            = VENDOR_LOGITECH;
    device_g535.idProductsSupported = &PRODUCT_ID;
    device_g535.numIdProducts       = 1;

    strncpy(device_g535.device_name, "Logitech G535", sizeof(device_g535.device_name));

    device_g535.capabilities                     = B(CAP_SIDETONE);
    device_g535.capability_details[CAP_SIDETONE] = (struct capability_detail) { .usagepage = 0xc, .usageid = 0x1, .interface = 3 };
    device_g535.send_sidetone                    = &g535_send_sidetone;

    *device = &device_g535;
}

static int g535_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t set_sidetone_level[MSG_SIZE] = { 0x11, 0xff, 0x04, 0x1e, num };

    for (int i = 16; i < MSG_SIZE; i++)
        set_sidetone_level[i] = 0;

    return hid_send_feature_report(device_handle, set_sidetone_level, MSG_SIZE);
}

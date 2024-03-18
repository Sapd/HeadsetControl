#include <unistd.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../device.h"
#include "../utility.h"
#include "logitech.h"

static struct device device_gpro_x2;

#define ID_LOGITECH_PRO_X2 0x0af7

static const uint16_t PRODUCT_IDS[] = {
    ID_LOGITECH_PRO_X2,
};

static int gpro_x2_send_sidetone(hid_device* device_handle, uint8_t num);
static int gpro_x2_send_inactive_time(hid_device* device_handle, uint8_t num);

void gpro_x2_init(struct device** device)
{
    device_gpro_x2.idVendor            = VENDOR_LOGITECH;
    device_gpro_x2.idProductsSupported = PRODUCT_IDS;
    device_gpro_x2.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_gpro_x2.device_name, "Logitech G PRO X 2", sizeof(device_gpro_x2.device_name));

    device_gpro_x2.capabilities                          = B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME);
    device_gpro_x2.capability_details[CAP_SIDETONE]      = (struct capability_detail) { .usagepage = 0xffa0, .usageid = 0x1, .interface = 3 };
    device_gpro_x2.capability_details[CAP_INACTIVE_TIME] = (struct capability_detail) { .usagepage = 0xffa0, .usageid = 0x1, .interface = 3 };

    device_gpro_x2.send_sidetone      = &gpro_x2_send_sidetone;
    device_gpro_x2.send_inactive_time = &gpro_x2_send_inactive_time;

    *device = &device_gpro_x2;
}

static int gpro_x2_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[12] = { 0x51, 0x0a, 0x00, 0x03, 0x1b, 0x00, 0x05, 0x00, 0x07, 0x1b, 0x01, num };

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

static int gpro_x2_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    uint8_t inactive_time_data[11] = { 0x51, 0x09, 0x00, 0x03, 0x1c, 0x00, 0x04, 0x00, 0x06, 0x1d, num };

    return hid_write(device_handle, inactive_time_data, sizeof(inactive_time_data) / sizeof(inactive_time_data[0]));
}

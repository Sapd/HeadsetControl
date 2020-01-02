#include "../device.h"

#include <string.h>

static struct device device_g930;

static const uint16_t PRODUCT_ID = 0x0a1f;

static int g930_send_sidetone(hid_device* device_handle, uint8_t num);

void g930_init(struct device** device)
{
    device_g930.idVendor = VENDOR_LOGITECH;
    device_g930.idProductsSupported = &PRODUCT_ID;
    device_g930.numIdProducts = 1;

    strncpy(device_g930.device_name, "Logitech G930", sizeof(device_g930.device_name));

    device_g930.capabilities = CAP_SIDETONE;
    device_g930.send_sidetone = &g930_send_sidetone;

    *device = &device_g930;
}

static int g930_send_sidetone(hid_device* device_handle, uint8_t num)
{
    const size_t message_size = 64;
    uint8_t data[message_size] = { 0xFF, 0x0A, 0, 0xFF, 0xF4, 0x10, 0x05, 0xDA, 0x8F, 0xF2, 0x01, num, 0, 0, 0, 0 };

    for (int i = 16; i < message_size; i++)
        data[i] = 0;

    return hid_send_feature_report(device_handle, data, message_size);
}

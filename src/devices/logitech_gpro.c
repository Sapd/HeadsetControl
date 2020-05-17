#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <string.h>

static struct device device_gpro;

static const uint16_t PRODUCT_ID = 0x0aa7; // PRO X: 0x0aaa

static int gpro_send_sidetone(hid_device* device_handle, uint8_t num);

void gpro_init(struct device** device)
{
    device_gpro.idVendor = VENDOR_LOGITECH;
    device_gpro.idProductsSupported = &PRODUCT_ID;
    device_gpro.numIdProducts = 1;

    strncpy(device_gpro.device_name, "Logitech G PRO", sizeof(device_gpro.device_name));

    device_gpro.capabilities = CAP_SIDETONE;
    device_gpro.send_sidetone = &gpro_send_sidetone;

    *device = &device_gpro;
}

static int gpro_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // num = map(num, 0, 128, 0, 100);
    if (num > 0x64)
        num = 0x64;

    uint8_t sidetone_data[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x1a, num };

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

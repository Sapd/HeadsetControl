#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <string.h>

static struct device device_gprox;

static const uint16_t PRODUCT_ID = 0x0aba;

static int gprox_send_sidetone(hid_device* device_handle, uint8_t num);

void gprox_init(struct device** device)
{
    device_gprox.idVendor = VENDOR_LOGITECH;
    device_gprox.idProductsSupported = &PRODUCT_ID;
    device_gprox.numIdProducts = 1;

    strncpy(device_gprox.device_name, "Logitech G PRO X", sizeof(device_gprox.device_name));

    device_gprox.capabilities = CAP_SIDETONE;
    device_gprox.send_sidetone = &gprox_send_sidetone;

    *device = &device_gprox;
}

static int gprox_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x1a, num };

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

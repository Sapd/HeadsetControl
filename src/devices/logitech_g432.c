#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <string.h>

static struct device device_g432;

#define ID_LOGITECH_G432 0x0a9c
#define ID_LOGITECH_G433 0x0a6d

static const uint16_t PRODUCT_IDS[] = {
    ID_LOGITECH_G432,
    ID_LOGITECH_G433,
};

static int g432_send_sidetone(hid_device* device_handle, uint8_t num);

void g432_init(struct device** device)
{
    device_g432.idVendor            = VENDOR_LOGITECH;
    device_g432.idProductsSupported = PRODUCT_IDS;
    device_g432.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_g432.device_name, "Logitech G432/G433", sizeof(device_g432.device_name));

    device_g432.capabilities  = B(CAP_SIDETONE);
    device_g432.send_sidetone = &g432_send_sidetone;

    *device = &device_g432;
}

static int g432_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x1e, num };

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

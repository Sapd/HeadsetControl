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

void gpro_x2_init(struct device** device)
{
    device_gpro_x2.idVendor            = VENDOR_LOGITECH;
    device_gpro_x2.idProductsSupported = PRODUCT_IDS;
    device_gpro_x2.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_gpro_x2.device_name, "Logitech G PRO X2", sizeof(device_gpro_x2.device_name));

    device_gpro_x2.capabilities = B(CAP_SIDETONE);

    device_gpro_x2.send_sidetone      = &gpro_x2_send_sidetone;

    *device = &device_gpro_x2;
}

static int gpro_x2_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x51, 0x0a, 0x00, 0x03, 0x1b, 0x00, 0x05, 0x00, 0x07, 0x1b, 0x01, num};

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

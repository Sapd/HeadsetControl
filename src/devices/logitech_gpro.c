#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <string.h>

static struct device device_gpro;

#define ID_LOGITECH_PRO         0x0aa7
#define ID_LOGITECH_PRO_X_0     0x0aaa
#define ID_LOGITECH_PRO_X_1     0x0aba

static const uint16_t PRODUCT_IDS[] = {
    ID_LOGITECH_PRO,
    ID_LOGITECH_PRO_X_0,
    ID_LOGITECH_PRO_X_1,
};

static int gpro_send_sidetone(hid_device* device_handle, uint8_t num);

void gpro_init(struct device** device)
{
    device_gpro.idVendor = VENDOR_LOGITECH;
    device_gpro.idProductsSupported = PRODUCT_IDS;
    device_gpro.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_gpro.device_name, "Logitech G PRO Series", sizeof(device_gpro.device_name));

    device_gpro.capabilities = CAP_SIDETONE;
    device_gpro.send_sidetone = &gpro_send_sidetone;

    *device = &device_gpro;
}

static int gpro_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x1a, num };

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

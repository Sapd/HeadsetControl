#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <math.h>
#include <string.h>


#define MSG_SIZE 20

static struct device device_g733;

static const uint16_t PRODUCT_ID = 0x0ab5;

static int g733_send_sidetone(hid_device* device_handle, uint8_t num);

void g733_init(struct device** device)
{
    device_g733.idVendor = VENDOR_LOGITECH;
    device_g733.idProductsSupported = &PRODUCT_ID;
    device_g733.numIdProducts = 1;
    device_g733.idUsagePage = 0xff43;
    device_g733.idUsage = 0x0202;

    strncpy(device_g733.device_name, "Logitech G733", sizeof(device_g733.device_name));

    device_g733.capabilities = CAP_SIDETONE;
    device_g733.send_sidetone = &g733_send_sidetone;

    *device = &device_g733;
}

static int g733_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[MSG_SIZE] = { 0x11, 0xff, 0x07, 0x1f, num, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    return hid_send_feature_report(device_handle, sidetone_data, MSG_SIZE);

}

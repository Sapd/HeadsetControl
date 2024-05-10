#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdlib.h>
#include <string.h>

#define VENDOR_HYPERX_CLOUD 0x03f0
#define ID_CLOUD3_WIRE      0x089d

static struct device device_c3;

static const uint16_t PRODUCT_IDS[] = { ID_CLOUD3_WIRE };

static int hyperx_cloud3_send_sidetone(hid_device* device_handle, uint8_t num);

void hyperx_cloud3_init(struct device** device)
{
    device_c3.idVendor            = VENDOR_HYPERX_CLOUD;
    device_c3.idProductsSupported = PRODUCT_IDS;
    device_c3.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    strncpy(device_c3.device_name, "HyperX Cloud 3", sizeof(device_c3.device_name));

    device_c3.capabilities  = B(CAP_SIDETONE);
    device_c3.send_sidetone = &hyperx_cloud3_send_sidetone;

    *device = &device_c3;
}

static int hyperx_cloud3_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // Supports only on/off
    int on = num > 0 ? 1 : 0;

#define MSG_SIZE 62
    uint8_t data[MSG_SIZE] = { 0x20, 0x86, on };

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

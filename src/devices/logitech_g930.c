#include "../device.h"

#include <string.h>

static struct device device_g930;

static int g930_send_sidetone(hid_device *device_handle, uint8_t num);

void g930_init(struct device** device)
{
    device_g930.idVendor = VENDOR_LOGITECH;
    device_g930.idProduct = 0x0a1f;
    
    strcpy(device_g930.device_name, "Logitech G930");
    
    device_g930.capabilities = CAP_SIDETONE;
    device_g930.send_sidetone = &g930_send_sidetone;
    
    *device = &device_g930;
}

static int g930_send_sidetone(hid_device *device_handle, uint8_t num)
{
    unsigned char data[12] = {0xFF, 0x0A, 0, 0xFF, 0xF4, 0x10, 0x05, 0xDA, 0x8F, 0xF2, 0x01, num};
    
    return hid_send_feature_report(device_handle, data, 12);
}



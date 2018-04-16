#include "../device.h"
#include "../utility.h"

#include <string.h>


static struct device device_g533;

static int g533_send_sidetone(hid_device *device_handle, uint8_t num);

void g533_init(struct device** device)
{
    device_g533.idVendor = VENDOR_LOGITECH;
    device_g533.idProduct = 0x0a66;
    
    strcpy(device_g533.device_name, "Logitech G533");
    
    device_g533.capabilities = CAP_SIDETONE;
    device_g533.send_sidetone = &g533_send_sidetone;
    
    *device = &device_g533;
}

static int g533_send_sidetone(hid_device *device_handle, uint8_t num)
{
    num = map(num, 0, 128, 200, 255);

    unsigned char data[12] = {0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num};
    
    return hid_send_feature_report(device_handle, data, 12);
}


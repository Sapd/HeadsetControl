#include "../device.h"

#include <string.h>

static struct device device_g633;

static int g633_send_sidetone(hid_device *device_handle, uint8_t num);

void g633_init(struct device** device)
{
    device_g633.idVendor = VENDOR_LOGITECH;
    device_g633.idProduct = 0x0a5c;
    
    strcpy(device_g633.device_name, "Logitech G633 Gaming Headset");
    
    device_g633.capabilities = CAP_SIDETONE;
    device_g633.send_sidetone = &g633_send_sidetone;
    
    *device = &device_g633;
}

static int g633_send_sidetone(hid_device *device_handle, uint8_t num)
{
    unsigned char data[5] = {0x11, 0xFF, 0x07, 0x1A, num};
    
    return hid_write(device_handle, data, 5);
}



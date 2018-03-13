#include "../device.h"

#include <string.h>


static struct device device_g533;

static int g533_send_sidetone(libusb_device_handle *device_handle, uint8_t num);

void g533_init(struct device** device)
{
    device_g533.idVendor = VENDOR_LOGITECH;
    device_g533.idProduct = 0x0a66;
    
    strcpy(device_g533.device_name, "Logitech G533");
    
    device_g533.capabilities = CAP_SIDETONE;
    device_g533.send_sidetone = &g533_send_sidetone;
    
    *device = &device_g533;
}

static int g533_send_sidetone(libusb_device_handle *device_handle, uint8_t num)
{
    unsigned char data[64] = {0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num, 0, 0, 0, 0};
    
    for (int i = 16; i < 64; i++)
        data[i] = 0;
    
    int size = libusb_control_transfer(device_handle, LIBUSB_DT_HID, LIBUSB_REQUEST_SET_CONFIGURATION, 0x03ff, 0x0003, data, 64, 1000);
    
    return size;
}


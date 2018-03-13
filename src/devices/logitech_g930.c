#include "../device.h"

#include <string.h>

static struct device device_g930;

static int g930_send_sidetone(libusb_device_handle *device_handle, uint8_t num);

void g930_init(struct device** device)
{
    device_g930.idVendor = VENDOR_LOGITECH;
    device_g930.idProduct = 0x0a1f;
    
    strcpy(device_g930.device_name, "Logitech G930");
    
    device_g930.capabilities = CAP_SIDETONE;
    device_g930.send_sidetone = &g930_send_sidetone;
    
    *device = &device_g930;
}

static int g930_send_sidetone(libusb_device_handle *device_handle, uint8_t num)
{
    unsigned char data[64] = {0xFF, 0x0A, 0, 0xFF, 0xF4, 0x10, 0x05, 0xDA, 0x8F, 0xF2, 0x01, num, 0, 0, 0, 0};
    
    for (int i = 16; i < 64; i++)
        data[i] = 0;
    
    int size = libusb_control_transfer(device_handle, LIBUSB_DT_HID, LIBUSB_REQUEST_SET_CONFIGURATION, 0x03ff, 0x0003, data, 64, 1000);
    
    return size;
}



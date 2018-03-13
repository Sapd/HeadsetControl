#include "../device.h"
#include "../utility.h"

#include <string.h>


static struct device device_void;

static int void_send_sidetone(libusb_device_handle *device_handle, uint8_t num);

void void_init(struct device** device)
{
    device_void.idVendor = VENDOR_CORSAIR;
    device_void.idProduct = 0x1b27;
    
    strcpy(device_void.device_name, "Corsair Void");
    
    device_void.capabilities = CAP_SIDETONE;
    device_void.send_sidetone = &void_send_sidetone;
    
    *device = &device_void;
}

static int void_send_sidetone(libusb_device_handle *device_handle, uint8_t num)
{
    // the range of the void seems to be from 200 to 255
    num = map(num, 0, 128, 200, 255);
    
    unsigned char data[64] = {0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num, 0, 0, 0, 0};
    
    for (int i = 16; i < 64; i++)
        data[i] = 0;
    
    int size = libusb_control_transfer(device_handle, LIBUSB_DT_HID, LIBUSB_REQUEST_SET_CONFIGURATION, 0x03ff, 0x0003, data, 64, 1000);
    
    return size;
}

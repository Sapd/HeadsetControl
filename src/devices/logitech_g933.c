#include "../device.h"

#include <string.h>
#include <hidapi.h>

static struct device device_g933;

static const uint16_t PRODUCT_ID = 0x0a5b;

static int g933_request_battery(hid_device *device_handle);

void g933_init(struct device ** device)
{ 
  device_g933.idVendor = VENDOR_LOGITECH;
  device_g933.idProductsSupported = &PRODUCT_ID;
  device_g933.numIdProducts = 1;

  strcpy(device_g933.device_name, "Logitech G933 Wireless");

  device_g933.capabilities = CAP_BATTERY_STATUS;

  device_g933.request_battery = &g933_request_battery;

  *device = &device_g933;
}

static int g933_request_battery(hid_device *device_handle)
{
  /*
    CREDIT GOES TO https://github.com/A3tra3rpi/ for his project
    https://github.com/A3tra3rpi/G933-battery-information-for-linux/
    I've simply ported that implementation to this project!
  */

  int r = 0;
  // request battery status
  unsigned char data_request[20] = {0x11,0xFF,0x08,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

  
  r = hid_write(device_handle, data_request, 20);
  if (r < 0) return r;

  // read battery status
  /* Received packet contains supposed battery data from byte[3] to byte[6]:
            0xa 0xf 0xe9 0x1
            |   |   |    |
            |   |   |    Headset status
            |   |   Battery level pt2
            |   Battery level pt1
            Unknown level. Often 0xa or 0xc
  */
  unsigned char data_read[7];
  r = hid_read(device_handle, data_read, 7);

  unsigned char state = data_read[6];

  if (r < 0) return r;
  if (state == 0x03) return BATTERY_LOADING;
  unsigned char lvl = data_read[4] - 13;
  unsigned char mx = 0xff;
  return (int)((data_read[5] + lvl * mx) * 100)/(3 * mx);

}
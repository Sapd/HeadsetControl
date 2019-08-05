#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static struct device device_arctis7_2019;

static int arctis7_2019_send_sidetone(hid_device *device_handle, uint8_t num);
static int arctis7_2019_request_battery(hid_device *device_handle);

void arctis7_2019_init(struct device** device)
{
    device_arctis7_2019.idVendor = VENDOR_STEELSERIES;
    device_arctis7_2019.idProduct = 0x12ad;
    device_arctis7_2019.idInterface = 0x05;

    strcpy(device_arctis7_2019.device_name, "SteelSeries Arctis 7");

    device_arctis7_2019.capabilities = CAP_SIDETONE | CAP_BATTERY_STATUS;
    device_arctis7_2019.send_sidetone = &arctis7_2019_send_sidetone;
    device_arctis7_2019.request_battery = &arctis7_2019_request_battery;

    *device = &device_arctis7_2019;
}

static int arctis7_2019_send_sidetone(hid_device *device_handle, uint8_t num)
{
    int ret = -1;

    // the range of the Arctis 7 seems to be from 0 to 0x12 (18)
    num = map(num, 0, 128, 0x00, 0x12);

    unsigned char *buf = calloc(31, 1);

    if (!buf)
    {
        return ret;
    }

    const unsigned char data_on[5] = {0x06, 0x35, 0x01, 0x00, num};
    const unsigned char data_off[2] = {0x06, 0x35};

    if (num)
    {
        memmove(buf, data_on, sizeof(data_on));
    }
    else
    {
        memmove(buf, data_off, sizeof(data_off));
    }

    ret = hid_write(device_handle, buf, 31);

    SAFE_FREE(buf);

    return ret;
}

static int arctis7_2019_request_battery(hid_device *device_handle)
{

    int r = 0;

    // request battery status
    unsigned char data_request[2] = {0x06, 0x18};
  
    r = hid_write(device_handle, data_request, 2);
    
    if (r < 0) return r;
    
    // read battery status
    unsigned char data_read[8];
    
    r = hid_read(device_handle, data_read, 8);
 
    if (r < 0) return r;

    return data_read[2];
}

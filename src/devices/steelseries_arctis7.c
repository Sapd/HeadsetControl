#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <string.h>
#include <stdlib.h>

static struct device device_arctis7;

static int arctis7_send_sidetone(hid_device *device_handle, uint8_t num);

void arctis7_init(struct device** device)
{
    device_arctis7.idVendor = VENDOR_STEELSERIES;
    device_arctis7.idProduct = 0x1260;
    device_arctis7.idInterface = 0x05;

    strcpy(device_arctis7.device_name, "SteelSeries Arctis 7");

    device_arctis7.capabilities = CAP_SIDETONE;
    device_arctis7.send_sidetone = &arctis7_send_sidetone;

    *device = &device_arctis7;
}

static int arctis7_send_sidetone(hid_device *device_handle, uint8_t num)
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

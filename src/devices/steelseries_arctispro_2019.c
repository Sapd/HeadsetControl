#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <string.h>
#include <stdlib.h>

static struct device device_arctispro_2019;

static int arctispro_2019_send_sidetone(hid_device *device_handle, uint8_t num);

void arctispro_2019_init(struct device** device)
{
    device_arctispro_2019.idVendor = VENDOR_STEELSERIES;
    device_arctispro_2019.idProduct = 0x1252;
    device_arctispro_2019.idInterface = 0x05;

    strcpy(device_arctispro_2019.device_name, "Steel Series Arctis Pro 2019");

    device_arctispro_2019.capabilities = CAP_SIDETONE;
    device_arctispro_2019.send_sidetone = &arctispro_2019_send_sidetone;

    *device = &device_arctispro_2019;
}

static int arctispro_2019_send_sidetone(hid_device *device_handle, uint8_t num)
{
    int ret = -1;

    // the range of the Arctis pro 2019 seems to be the same as the 7 - 0 to 0x12 (18)
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

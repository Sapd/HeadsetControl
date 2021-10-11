#include "../device.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct device device_elo71USB;

static const uint16_t PRODUCT_ID = 0x3a34;

static int elo71USB_switch_lights(hid_device* hid_device, uint8_t on);

void elo71USB_init(struct device** device)
{
    device_elo71USB.idVendor = VENDOR_ROCCAT;
    device_elo71USB.idProductsSupported = &PRODUCT_ID;
    device_elo71USB.numIdProducts = 1;
    device_elo71USB.idInterface = 0;

    strncpy(device_elo71USB.device_name, "ROCCAT Elo 7.1 USB", sizeof(device_elo71USB.device_name));

    device_elo71USB.capabilities = CAP_LIGHTS;
    device_elo71USB.switch_lights = &elo71USB_switch_lights;

    *device = &device_elo71USB;
}

/*
 * Sends size bytes from data to the headset.
 * If out_buffer points to 64 byte of preallocated memory, send_receive will
 * receive 64 byte into id over the HID stream.
 *
 * The Roccat Elo 71 Air hates, if it gets 64 bytes blocks to quickly. The
 * windows software seems just to wait a bit, so send_receive does as well. :-/
 *
 * This function returns the code from hid_read or hid_write, if either no read
 * was requested or hid_write already failed.
 */
static int send_receive(hid_device* hid_device, const uint8_t* data,
    int size, uint8_t* out_buffer)
{
    struct timespec ts;
    int r;

    r = hid_write(hid_device, data, size);

    if ((out_buffer != NULL) && (r >= 0)) {
        r = hid_read(hid_device, out_buffer, 16);
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 40 * 1000000;
    nanosleep(&ts, NULL);

    return r;
}

static int elo71USB_switch_lights(hid_device* hid_device, uint8_t on)
{
    uint8_t cmd92[16] = { 0xff, 0x02 };
    uint8_t cmd93[16] = { 0xff, 0x03, 0x00, 0x01 };
    uint8_t cmd94[16] = { 0xff, 0x04, 0x00, 0x00, 0xf4 };
    uint8_t cmd95[16] = { 0xff, 0x01 };
    uint8_t cmd97[16] = { 0xff, 0x04 };

    int r;

    r = send_receive(hid_device, cmd92, sizeof(cmd92), NULL);
    if (r <= 0)
        return r;

    r = send_receive(hid_device, cmd93, sizeof(cmd93), NULL);
    if (r <= 0)
        return r;

    if (on == 1) {
        r = send_receive(hid_device, cmd94, sizeof(cmd94), NULL);
        if (r <= 0)
            return r;
    } else {
        r = send_receive(hid_device, cmd97, sizeof(cmd97), NULL);
        if (r <= 0)
            return r;
    }

    r = send_receive(hid_device, cmd95, sizeof(cmd95), NULL);
    if (r <= 0)
        return r;

    return 0;
}

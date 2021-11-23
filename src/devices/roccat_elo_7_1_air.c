#include "../device.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct device device_elo71Air;

static const uint16_t PRODUCT_ID = 0x3a37;

static int elo71Air_switch_lights(hid_device* hid_device, uint8_t on);
static int elo71Air_send_inactive_time(hid_device* hid_device, uint8_t num);

void elo71Air_init(struct device** device)
{
    device_elo71Air.idVendor            = VENDOR_ROCCAT;
    device_elo71Air.idProductsSupported = &PRODUCT_ID;
    device_elo71Air.numIdProducts       = 1;

    strncpy(device_elo71Air.device_name, "ROCCAT Elo 7.1 Air", sizeof(device_elo71Air.device_name));

    device_elo71Air.capabilities       = B(CAP_LIGHTS) | B(CAP_INACTIVE_TIME);
    device_elo71Air.switch_lights      = &elo71Air_switch_lights;
    device_elo71Air.send_inactive_time = &elo71Air_send_inactive_time;

    *device = &device_elo71Air;
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
        r = hid_read_timeout(hid_device, out_buffer, 64, hsc_device_timeout);
    }

    ts.tv_sec  = 0;
    ts.tv_nsec = 75 * 1000000;
    nanosleep(&ts, NULL);

    return r;
}

static int elo71Air_switch_lights(hid_device* hid_device, uint8_t on)
{
    // All commands are related to the LEDs, the following Hex codes cause
    // the LEDs to become 'static', the exact meaning of most commands & their
    // parameters is further unknown.
    uint8_t cmd92[64] = { 0xff, 0x02 };
    uint8_t cmd93[64] = { 0xff, 0x03, 0x00, 0x01, 0x00, 0x01 };

    // sets LED color as RGB value
    uint8_t cmd94[64] = { 0xff, 0x04, 0x00, 0x00,
        on ? 0xff : 0, on ? 0xff : 0, on ? 0xff : 0 };
    uint8_t cmd95[64] = { 0xff, 0x01 };
    int r;

    r = send_receive(hid_device, cmd92, sizeof(cmd92), NULL);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    r = send_receive(hid_device, cmd93, sizeof(cmd93), NULL);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    r = send_receive(hid_device, cmd94, sizeof(cmd94), NULL);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    r = send_receive(hid_device, cmd95, sizeof(cmd95), NULL);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    return 0;
}

static int elo71Air_send_inactive_time(hid_device* hid_device, uint8_t num)
{
    if (num > 60) {
        printf("Device only supports up to 60 minutes.\n");
        return -2;
    }

    int seconds     = num * 60;
    uint8_t cmd[64] = { 0xa1, 0x04, 0x06, 0x54, seconds >> 8, seconds & 0xFF };
    uint8_t response[64];

    return send_receive(hid_device, cmd, sizeof(cmd), response);
    //meaning of response of headset is not clear yet, fetch & ignore it for now
}

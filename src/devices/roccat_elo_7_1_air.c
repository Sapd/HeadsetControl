#include "../device.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static struct device device_elo71Air;

static const uint16_t PRODUCT_ID = 0x3a37;

static int elo71Air_switch_lights(hid_device* hid_device, uint8_t on);

void elo71Air_init(struct device** device)
{
    device_elo71Air.idVendor = VENDOR_ROCCAT;
    device_elo71Air.idProductsSupported = &PRODUCT_ID;
    device_elo71Air.numIdProducts = 2;
    device_elo71Air.idInterface = 0;

    strncpy(device_elo71Air.device_name, "ROCCAT Elo 7.1 Air", sizeof(device_elo71Air.device_name));

    device_elo71Air.capabilities = CAP_LIGHTS;
    device_elo71Air.switch_lights = &elo71Air_switch_lights;

    *device = &device_elo71Air;
}

/*
 * Headset exchanges 64 Byte blocks with mostly zeros with driver. => This wrapper
 * around hid_write and hid_read pads zeros between the passed, variable length
 * data (length specified by size) and 64. If out_buffer points to 64 byte of 
 * preallocated memory, send_receive will receive 64 byte into id over the HID
 * stream.
 *
 * The Roccat Elo 71 Air hates, if it gets 64 bytes blocks to quickly. The 
 * windows software seems just to wait a bit, so send_receive does as well. :-/
 *
 * This function returns the code from hid_read or hid_write, if either no read
 * was requested or hid_write already failed.
 */
static int send_receive(hid_device* hid_device, const unsigned char* data, 
			int size, unsigned char* out_buffer) 
{
    struct timespec ts;	
    unsigned char send_buffer[64];
    int r;

    memcpy(send_buffer, data, size);
    r = hid_write(hid_device, send_buffer, sizeof(send_buffer));
    if ((out_buffer != NULL) && (r >= 0)) {
    	r = hid_read(hid_device, out_buffer, 64);
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 75 * 1000000;
    nanosleep(&ts, NULL);

    return r;
}

static int elo71Air_switch_lights(hid_device* hid_device, uint8_t on)
{
    // All commands are related to the LEDs, the following Hex codes cause
    // the LEDs to become 'static', the exact meaning of most commands & their
    // parameters is further unknown.
    unsigned char cmd92[] = {0xff, 0x02};
    unsigned char cmd93[] = {0xff, 0x03, 0x00, 0x01, 0x00, 0x01};

    // sets LED color as RGB value
    unsigned char cmd94[] = {0xff, 0x04, 0x00, 0x00, 
	                     on ? 0xff : 0, on ? 0xff : 0, on ? 0xff : 0};
    unsigned char cmd95[] = {0xff, 0x01};
    int r;

    r = send_receive(hid_device, cmd92, sizeof(cmd92), NULL);
    if (r <= 0) {
    	return r;
    }
    
    r = send_receive(hid_device, cmd93, sizeof(cmd93), NULL);
    if (r <= 0) {
    	return r;
    }

    r = send_receive(hid_device, cmd94, sizeof(cmd94), NULL);
    if (r <= 0) {
    	return r;
    }

    r = send_receive(hid_device, cmd95, sizeof(cmd95), NULL);
    if (r <= 0) {
    	return r;
    }

    return 0;
}

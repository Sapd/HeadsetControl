#include "../device.h"
#include "../utility.h"
#include "hidapi.h"

#include <stdint.h>
#include <string.h>

static struct device device_maxwell;

#define ID_MAXWELL 0x4b19

static const uint16_t PRODUCT_IDS[] = { ID_MAXWELL };

static int audeze_maxwell_send_sidetone(hid_device* device_handle, uint8_t num);
static int audeze_maxwell_send_inactive_time(hid_device* device_handle, uint8_t num);
static int audeze_maxwell_send_volume_limiter(hid_device* hid_device, uint8_t on);
static int audeze_maxwell_send_equalizer_custom_preset(hid_device* hid_device, uint8_t num);
static BatteryInfo audeze_maxwell_get_battery(hid_device* device_handle);

#define MSG_SIZE  62
#define REPORT_ID 0x06

void audeze_maxwell_init(struct device** device)
{
    device_maxwell.idVendor            = VENDOR_AUDEZE;
    device_maxwell.idProductsSupported = PRODUCT_IDS;
    device_maxwell.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_maxwell.device_name, "Audeze Maxwell", sizeof(device_maxwell.device_name));

    device_maxwell.capabilities                             = B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER) | B(CAP_EQUALIZER_PRESET) | B(CAP_BATTERY_STATUS);
    device_maxwell.capability_details[CAP_SIDETONE]         = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };
    device_maxwell.capability_details[CAP_INACTIVE_TIME]    = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };
    device_maxwell.capability_details[CAP_VOLUME_LIMITER]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };
    device_maxwell.capability_details[CAP_EQUALIZER_PRESET] = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };
    device_maxwell.capability_details[CAP_BATTERY_STATUS]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };

    device_maxwell.send_sidetone         = &audeze_maxwell_send_sidetone;
    device_maxwell.send_inactive_time    = &audeze_maxwell_send_inactive_time;
    device_maxwell.send_volume_limiter   = &audeze_maxwell_send_volume_limiter;
    device_maxwell.send_equalizer_preset = &audeze_maxwell_send_equalizer_custom_preset;
    device_maxwell.request_battery       = &audeze_maxwell_get_battery;

    *device = &device_maxwell;
}

int read_audeze_maxwell_status(hid_device* device_handle, unsigned char* buff)
{
    buff[0] = 0x7;
    return hid_get_input_report(device_handle, buff, MSG_SIZE);
}

static BatteryInfo audeze_maxwell_get_battery(hid_device* device_handle)
{

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    unsigned char data_request[MSG_SIZE] = { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C };

    if (hid_write(device_handle, data_request, MSG_SIZE) < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    unsigned char buf[MSG_SIZE];
    switch (read_audeze_maxwell_status(device_handle, buf)) {
    case 0:
        info.status = BATTERY_TIMEOUT;
        break;
    case MSG_SIZE:
        unsigned char BATTERY_STATUS_FLAG = buf[1];

        // Headset Off
        if (BATTERY_STATUS_FLAG == 0x00) {
            info.status = BATTERY_UNAVAILABLE;
            break;
        }

        for (int i = 0; i < MSG_SIZE - 4; ++i) {
            if (buf[i] == 0xD6 && buf[i + 1] == 0x0C && buf[i + 2] == 0x00 && buf[i + 3] == 0x00) {
                info.level  = buf[i + 4];
                info.status = BATTERY_AVAILABLE;
                break;
            }
        }

        break;
    default:
        info.status = BATTERY_HIDERROR;
        break;
    }

    return info;
}

int audeze_maxwell_toggle_sidetone(hid_device* device_handle)
{
    // Audeze HQ changes the byte at index 11, but it has no effect, it’s always toggleable regardless of what’s sent.
    unsigned char data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x82, 0x2c, 0x7, 0x0, 0x1 };

    return hid_write(device_handle, data_request, MSG_SIZE);
}

int audeze_maxwell_send_sidetone(hid_device* device_handle, uint8_t num)
{

    // The range of the maxwell seems to be from 0 to 31
    num = map(num, 0, 128, 0, 31);

    unsigned char data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x2c, 0x0, num };

    // The sidetone is enabled whenever its level changes.
    int res = hid_write(device_handle, data_request, MSG_SIZE);

    if (num == 0) {
        return audeze_maxwell_toggle_sidetone(device_handle);
    }

    return res;

}

// Audeze HQ supports up to 6 hours of idle time, but the send_inactive_time API caps it at 90 minutes.
int audeze_maxwell_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    unsigned char data_request[MSG_SIZE] = { 0x6, 0x10, 0x80, 0x5, 0x5a, 0xc, 0x0, 0x82, 0x2c, 0x1, 0x0 };

    // If num is 0, the inactive time flag is set to 0x00, disabling the automatic shutdown feature.
    if (num > 0) {
        uint16_t minutes = 360;

        if (num <= 5) {
            minutes = 5;
        } else if (num <= 10) {
            minutes = 10;
        } else if (num <= 15) {
            minutes = 15;
        } else if (num <= 30) {
            minutes = 30;
        } else if (num <= 45) {
            minutes = 45;
        } else if (num <= 60) {
            minutes = 60;
        } else if (num <= 90) {
            minutes = 90;
        } else if (num <= 120) {
            minutes = 120;
        } else if (num <= 240) {
            minutes = 240;
        }

        uint16_t seconds = minutes * 60;

        data_request[11] = 0x01; // Inactive time flag
        data_request[13] = seconds & 0xFF; // LSB seconds
        data_request[14] = seconds >> 8 & 0xFF; // MSB seconds
        data_request[15] = 0x01; // Constant Flag
        data_request[17] = data_request[13]; // LSB Again
        data_request[18] = data_request[14]; // MSB Again
    }

    return hid_write(device_handle, data_request, MSG_SIZE);
}

int audeze_maxwell_send_volume_limiter(hid_device* hid_device, uint8_t on)
{
    unsigned char data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x28, 0x0, on == 1 ? 0x88 : 0x8e };
    return hid_write(hid_device, data_request, MSG_SIZE);
}

// Audeze Maxwell has 6 default presets and 4 custom presets (Audeze, Treble Boost, Bass Boost, Immersive, Competition, Footsteps, Preset 1, Preset 2, Preset 3, Preset 4)
int audeze_maxwell_send_equalizer_preset(hid_device* hid_device, uint8_t num)
{
    if (num < 1 || num > 10) {
        return HSC_OUT_OF_BOUNDS;
    }

    unsigned char data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x0, 0x0, num };
    return hid_write(hid_device, data_request, MSG_SIZE);
}

int audeze_maxwell_send_equalizer_custom_preset(hid_device* hid_device, uint8_t num)
{
    // Getting only custom presets
    return audeze_maxwell_send_equalizer_preset(hid_device, 7 + num);
}

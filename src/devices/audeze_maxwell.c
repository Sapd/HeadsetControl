#include "../device.h"
#include "../utility.h"
#include "hidapi.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct device device_maxwell;

#define ID_MAXWELL             0x4b19
#define ID_MAXWELL_XBOX_DONGLE 0x4b18

static const uint16_t PRODUCT_IDS[] = { ID_MAXWELL, ID_MAXWELL_XBOX_DONGLE };

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
    device_maxwell.capability_details[CAP_SIDETONE]         = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_INACTIVE_TIME]    = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_VOLUME_LIMITER]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_EQUALIZER_PRESET] = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_BATTERY_STATUS]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };

    device_maxwell.send_sidetone         = &audeze_maxwell_send_sidetone;
    device_maxwell.send_inactive_time    = &audeze_maxwell_send_inactive_time;
    device_maxwell.send_volume_limiter   = &audeze_maxwell_send_volume_limiter;
    device_maxwell.send_equalizer_preset = &audeze_maxwell_send_equalizer_custom_preset;
    device_maxwell.request_battery       = &audeze_maxwell_get_battery;

    *device = &device_maxwell;
}

static int send_get_input_report(hid_device* device_handle, const uint8_t* data, uint8_t* buff)
{
    // Audeze HQ sends packets at intervals of approximately 60ms, so we do the same. Sending too quickly causes issues.
    usleep(60000);

    int res = hid_write(device_handle, data, MSG_SIZE);
    if (res >= 0) {
        if (buff == NULL) {
            uint8_t tempBuff[MSG_SIZE] = { 0x7 };
            buff                       = tempBuff;
        } else {
            buff[0] = 0x7;
        }
        res = hid_get_input_report(device_handle, buff, MSG_SIZE);
    }

    return res;
}

static const uint8_t UNIQUE_REQUESTS[13][MSG_SIZE] = {
    { 0x6, 0x8, 0x80, 0x5, 0x5a, 0x4, 0x0, 0x1, 0x9, 0x20 },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x25 },
    { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0x07, 0x1C },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x28 },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x01 },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x07 },
    { 0x06, 0x07, 0x00, 0x05, 0x5A, 0x03, 0x00, 0x07, 0x1C },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2D },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2C },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09 },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x0B },
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2F },
    { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C } // Sent after the first 12 unique requests and the 5 info requests
};

// INFO_REQUESTS is used to request information from the headset, such as battery status and microphone status. All the known return values are annotated in the comments.
static const uint8_t INFO_REQUESTS[5][MSG_SIZE] = {
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x22 }, // Battery and Mic
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09 }, // Battery and Mic
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x0B }, // Battery
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2C }, // Battery
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x07 } // Battery
};

static BatteryInfo audeze_maxwell_get_battery(hid_device* device_handle)
{

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    uint8_t buf[MSG_SIZE];
    switch (send_get_input_report(device_handle, INFO_REQUESTS[0], buf)) {
    case 0:
        info.status = BATTERY_TIMEOUT;
        break;
    case MSG_SIZE:
        // Headset Off
        if (buf[1] == 0x00) {
            info.status = BATTERY_UNAVAILABLE;
            break;
        }

        if (buf[12] == 0xFF) {
            info.microphone_status = MICROPHONE_UP;
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

    if (info.status != BATTERY_AVAILABLE) {
        // When initializing, Audeze MQ sends 18 packets: the first 12 are unique, followed by 5 which repeat every second and 1 unique. The following packet is the last unique one before the repetition cycle begins.
        // This one seems to "reset" something in the headset, so we send it to ensure we get the correct data. It is necessary when the headset is turned off and on again.
        // Sending the other 12 unique requests seems unnecessary.
        send_get_input_report(device_handle, UNIQUE_REQUESTS[12], NULL);
    }

    return info;
}

static int audeze_maxwell_toggle_sidetone(hid_device* device_handle)
{
    // Audeze HQ changes the byte at index 11, but it has no effect, it’s always toggleable regardless of what’s sent.
    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x82, 0x2c, 0x7, 0x0, 0x1 };

    return hid_write(device_handle, data_request, MSG_SIZE);
}

static int audeze_maxwell_send_sidetone(hid_device* device_handle, uint8_t num)
{

    // The range of the maxwell seems to be from 0 to 31
    num = map(num, 0, 128, 0, 31);

    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x2c, 0x0, num };

    // The sidetone is enabled whenever its level changes.
    int res = hid_write(device_handle, data_request, MSG_SIZE);

    if (num == 0) {
        return audeze_maxwell_toggle_sidetone(device_handle);
    }

    return res;
}

// Audeze HQ supports up to 6 hours of idle time, but the send_inactive_time API caps it at 90 minutes.
static int audeze_maxwell_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    uint8_t data_request[MSG_SIZE] = { 0x6, 0x10, 0x80, 0x5, 0x5a, 0xc, 0x0, 0x82, 0x2c, 0x1, 0x0 };

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

static int audeze_maxwell_send_volume_limiter(hid_device* hid_device, uint8_t on)
{
    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x28, 0x0, on == 1 ? 0x88 : 0x8e };
    return hid_write(hid_device, data_request, MSG_SIZE);
}

// Audeze Maxwell has 6 default presets and 4 custom presets (Audeze, Treble Boost, Bass Boost, Immersive, Competition, Footsteps, Preset 1, Preset 2, Preset 3, Preset 4)
static int audeze_maxwell_send_equalizer_preset(hid_device* hid_device, uint8_t num)
{
    if (num < 1 || num > 10) {
        return HSC_OUT_OF_BOUNDS;
    }

    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x0, 0x0, num };
    return hid_write(hid_device, data_request, MSG_SIZE);
}

static int audeze_maxwell_send_equalizer_custom_preset(hid_device* hid_device, uint8_t num)
{
    // Getting only custom presets
    return audeze_maxwell_send_equalizer_preset(hid_device, 7 + num);
}

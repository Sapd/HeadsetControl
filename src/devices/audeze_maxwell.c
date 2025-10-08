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
static int audeze_maxwell_get_chatmix(hid_device* device_handle);
static int audeze_maxwell_switch_voice_prompts(hid_device* device_handle, uint8_t on);

#define MSG_SIZE  62
#define REPORT_ID 0x06

void audeze_maxwell_init(struct device** device)
{
    device_maxwell.idVendor            = VENDOR_AUDEZE;
    device_maxwell.idProductsSupported = PRODUCT_IDS;
    device_maxwell.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    device_maxwell.equalizer_presets_count = 10;

    strncpy(device_maxwell.device_name, "Audeze Maxwell", sizeof(device_maxwell.device_name));

    device_maxwell.capabilities                             = B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER) | B(CAP_EQUALIZER_PRESET) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_VOICE_PROMPTS);
    device_maxwell.capability_details[CAP_SIDETONE]         = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_INACTIVE_TIME]    = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_VOLUME_LIMITER]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_EQUALIZER_PRESET] = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_BATTERY_STATUS]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_CHATMIX_STATUS]   = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };
    device_maxwell.capability_details[CAP_VOICE_PROMPTS]    = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 0 };

    device_maxwell.send_sidetone         = &audeze_maxwell_send_sidetone;
    device_maxwell.send_inactive_time    = &audeze_maxwell_send_inactive_time;
    device_maxwell.send_volume_limiter   = &audeze_maxwell_send_volume_limiter;
    device_maxwell.send_equalizer_preset = &audeze_maxwell_send_equalizer_custom_preset;
    device_maxwell.request_battery       = &audeze_maxwell_get_battery;
    device_maxwell.request_chatmix       = &audeze_maxwell_get_chatmix;
    device_maxwell.switch_voice_prompts  = &audeze_maxwell_switch_voice_prompts;

    *device = &device_maxwell;
}

typedef struct {
    uint8_t sidetone_enabled;
    int sidetone_level;
} AudezeMaxwellSidetoneInfo;

typedef struct {
    BatteryInfo battery_info;
    int chatmix_level;
    AudezeMaxwellSidetoneInfo sidetone_info;
} AudezeMaxwellStatus;

// When initializing, Audeze MQ sends 18 packets: the first 12 are unique, followed by 5 which repeat every second and 1 unique. The last package is sent after the first 12 unique requests and the 5 info requests.
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
static const uint8_t STATUS_REQUESTS[5][MSG_SIZE] = {
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x22 }, // Battery, Sidetone enabled
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09 }, // Battery, Mic
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x0B }, // Battery
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x01, 0x09, 0x2C }, // Battery, ChatMix
    { 0x06, 0x08, 0x80, 0x05, 0x5A, 0x04, 0x00, 0x83, 0x2C, 0x07 } // Battery, Sidetone Level
};

static int send_get_input_report(hid_device* device_handle, const uint8_t* data, uint8_t* buff)
{
    // Audeze HQ sends packets at intervals of approximately 60ms, so we do the same. Sending too quickly causes issues.
    usleep(60000);

    int res = hid_write(device_handle, data, MSG_SIZE);
    if (res == MSG_SIZE) {
        if (buff == NULL) {
            uint8_t tempBuff[MSG_SIZE] = { 0x7 };
            res                        = hid_get_input_report(device_handle, tempBuff, MSG_SIZE);
        } else {
            buff[0] = 0x7;
            res     = hid_get_input_report(device_handle, buff, MSG_SIZE);
        }
    }

    return res;
}

static BatteryInfo audeze_maxwell_request_battery(const uint8_t (*status_buffs)[MSG_SIZE])
{
    BatteryInfo battery_info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    const uint8_t* buff = status_buffs[0];

    // Headset Off
    if (buff[1] == 0x00) {
        battery_info.status = BATTERY_UNAVAILABLE;
        return battery_info;
    }

    if (status_buffs[1][12] == 0xFF) {
        battery_info.microphone_status = MICROPHONE_UP;
    }

    for (int i = 0; i < MSG_SIZE - 4; ++i) {
        if (buff[i] == 0xD6 && buff[i + 1] == 0x0C && buff[i + 2] == 0x00 && buff[i + 3] == 0x00) {
            battery_info.level  = buff[i + 4];
            battery_info.status = BATTERY_AVAILABLE;
            break;
        }
    }

    return battery_info;
}

static int audeze_maxwell_request_chatmix(const uint8_t (*status_buffs)[MSG_SIZE])
{
    // The chatmix level is in the range of 0 to 20, where 10 is the center position.
    return map(status_buffs[3][12], 0, 20, 0, 128);
}

static AudezeMaxwellSidetoneInfo audeze_maxwell_request_sidetone(const uint8_t (*status_buffs)[MSG_SIZE])
{
    AudezeMaxwellSidetoneInfo sidetone_info = { .sidetone_enabled = 0, .sidetone_level = -1 };

    // The sidetone level is in the range of 0 to 31, where 15 is the center position.
    sidetone_info.sidetone_level   = map(status_buffs[4][12], 0, 31, 0, 128);
    sidetone_info.sidetone_enabled = status_buffs[0][12];

    return sidetone_info;
}

// This function exists because getting just one piece of information from the headset can lead to inconsistencies and failures. So, we follow what Audeze HQ does, sending all packets every time we want to get the headset status.
static AudezeMaxwellStatus audeze_maxwell_status(hid_device* device_handle)
{
    AudezeMaxwellStatus status = { .battery_info = { .status = BATTERY_UNAVAILABLE, .level = -1 }, .chatmix_level = -1, .sidetone_info = { .sidetone_level = -1 } };

    uint8_t status_buffs[5][MSG_SIZE];
    for (int i = 0; i < 5; ++i) {
        if (send_get_input_report(device_handle, STATUS_REQUESTS[i], status_buffs[i]) != MSG_SIZE) {
            status.battery_info.status          = BATTERY_HIDERROR;
            status.chatmix_level                = HSC_ERROR;
            status.sidetone_info.sidetone_level = HSC_ERROR;

            return status;
        }
    }

    status.battery_info  = audeze_maxwell_request_battery(status_buffs);
    status.chatmix_level = audeze_maxwell_request_chatmix(status_buffs);
    status.sidetone_info = audeze_maxwell_request_sidetone(status_buffs);

    if (status.battery_info.status != BATTERY_AVAILABLE) {
        // Audeze HQ sends 13 unique requests when opened, so to ensure we get the correct information, we send all of them.
        for (int i = 0; i < 13; ++i) {
            send_get_input_report(device_handle, UNIQUE_REQUESTS[i], NULL);
        }
    }

    return status;
}

static BatteryInfo audeze_maxwell_get_battery(hid_device* device_handle)
{
    return audeze_maxwell_status(device_handle).battery_info;
}

static int audeze_maxwell_toggle_sidetone(hid_device* device_handle)
{
    // Audeze HQ changes the byte at index 11, but it has no effect, it’s always toggleable regardless of what’s sent.
    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x82, 0x2c, 0x7, 0x0, 0x1 };

    return send_get_input_report(device_handle, data_request, NULL);
}

static int audeze_maxwell_send_sidetone(hid_device* device_handle, uint8_t num)
{

    // The range of the maxwell seems to be from 0 to 31
    num = map(num, 0, 128, 0, 31);

    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x2c, 0x0, num };

    // The sidetone is enabled whenever its level changes.
    int res = send_get_input_report(device_handle, data_request, NULL);

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

    return send_get_input_report(device_handle, data_request, NULL);
}

static int audeze_maxwell_send_volume_limiter(hid_device* hid_device, uint8_t on)
{
    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x28, 0x0, on == 1 ? 0x88 : 0x8e };
    return send_get_input_report(hid_device, data_request, NULL);
}

// Audeze Maxwell has 6 default presets and 4 custom presets (Audeze, Treble Boost, Bass Boost, Immersive, Competition, Footsteps, Preset 1, Preset 2, Preset 3, Preset 4)
static int audeze_maxwell_send_equalizer_preset(hid_device* hid_device, uint8_t num)
{
    if (num < 1 || num > 10) {
        return HSC_OUT_OF_BOUNDS;
    }

    uint8_t data_request[MSG_SIZE] = { 0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x0, 0x9, 0x0, 0x0, num };
    return send_get_input_report(hid_device, data_request, NULL);
}

static int audeze_maxwell_send_equalizer_custom_preset(hid_device* hid_device, uint8_t num)
{
    // Getting only custom presets
    return audeze_maxwell_send_equalizer_preset(hid_device, num + 1);
}

// The chatmix level is in the range of 0 to 20, where 10 is the center position.
// static int audeze_maxwell_set_chatmix(hid_device* device_handle, uint8_t chatmix_level)
// {
//     // The chatmix level is in the range of 0 to 20, where 10 is the center position.
//     if (chatmix_level > 128 || chatmix_level < 0) {
//         return HSC_OUT_OF_BOUNDS;
//     }
//
//     // Map the chatmix level from 0-128 to 0-20
//     chatmix_level = map(chatmix_level, 0, 128, 0, 20);
//
//     uint8_t data_request[MSG_SIZE] = { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x82, 0x2C, 0x0B, 0x00, chatmix_level };
//
//     return send_get_input_report(device_handle, data_request, NULL);
// }

static int audeze_maxwell_get_chatmix(hid_device* device_handle)
{
    return audeze_maxwell_status(device_handle).chatmix_level;
}

// The voice prompt level is in the range of 0 (0%) to 15 (100%), where 7 (50%) is the default level.
// The Headset Control API does not support setting the voice prompt level, so we set it to 7 (50%) when on, and 0 (0%) otherwise.
static int audeze_maxwell_switch_voice_prompts(hid_device* device_handle, uint8_t on)
{
    uint8_t data_request[MSG_SIZE] = { 0x06, 0x09, 0x80, 0x05, 0x5A, 0x05, 0x00, 0x00, 0x09, 0x20, 0x00, on == 1 ? 0x07 : 0x00 };
    return send_get_input_report(device_handle, data_request, NULL);
}

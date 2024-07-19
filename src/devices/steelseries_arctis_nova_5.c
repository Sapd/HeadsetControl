#include "../device.h"

#include <hidapi.h>
#include <string.h>

static struct device device_arctis;

enum { ID_ARCTIS_NOVA_5_BASE_STATION = 0x2232 };

enum {
    MSG_SIZE        = 64,
    STATUS_BUF_SIZE = 128,
};

enum {
    CONNECTION_STATUS_BYTE = 0x01,
    BATTERY_LEVEL_BYTE     = 0x03,
    BATTERY_STATUS_BYTE    = 0x04,
};

enum {
    HEADSET_CHARGING               = 0x01,
    HEADSET_OFFLINE                = 0x02,
};

static const uint16_t PRODUCT_IDS[]
    = { ID_ARCTIS_NOVA_5_BASE_STATION };

static const uint8_t SAVE_DATA1[MSG_SIZE] = { 0x0, 0x09 }; // Command1 to save settings to headset
static const uint8_t SAVE_DATA2[MSG_SIZE] = { 0x0, 0x35, 0x01 }; // Command2 to save settings to headset

static int set_sidetone(hid_device* device_handle, uint8_t num);
static int set_mic_mute_led_brightness(hid_device* device_handle, uint8_t num);
static int set_mic_volume(hid_device* device_handle, uint8_t num);
static int set_inactive_time(hid_device* device_handle, uint8_t num);
static int set_volume_limiter(hid_device* device_handle, uint8_t num);
static BatteryInfo get_battery(hid_device* device_handle);

static int read_device_status(hid_device* device_handle, unsigned char* data_read);
static int save_state(hid_device* device_handle);

void arctis_nova_5_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 5", sizeof(device_arctis.device_name));

    device_arctis.capabilities                                           = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER);
    device_arctis.capability_details[CAP_SIDETONE]                       = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_MICROPHONE_VOLUME]              = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]                  = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_VOLUME_LIMITER]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };

    device_arctis.send_sidetone                       = &set_sidetone;
    device_arctis.send_microphone_mute_led_brightness = &set_mic_mute_led_brightness;
    device_arctis.send_microphone_volume              = &set_mic_volume;
    device_arctis.request_battery                     = &get_battery;
    device_arctis.send_inactive_time                  = &set_inactive_time;
    device_arctis.send_volume_limiter                 = &set_volume_limiter;
    *device                                           = &device_arctis;
}

static int read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    unsigned char data_request[2] = { 0x0, 0xB0 };
    int r                         = hid_write(device_handle, data_request, sizeof(data_request));

    if (r < 0)
        return r;

    return hid_read_timeout(device_handle, data_read, STATUS_BUF_SIZE, hsc_device_timeout);
}

static int save_state(hid_device* device_handle)
{
    int r = hid_write(device_handle, SAVE_DATA1, MSG_SIZE);
    if (r < 0)
        return r;

    return hid_write(device_handle, SAVE_DATA2, MSG_SIZE);
}

static int set_sidetone(hid_device* device_handle, uint8_t num)
{
    // This headset only supports 10 levels of sidetone volume,
    // but we allow a full range of 0-128 for it. Map the volume to the correct numbers.
    if (num < 13) {
        num = 0x00;
    } else if (num < 25) {
        num = 0x01;
    } else if (num < 37) {
        num = 0x02;
    } else if (num < 49) {
        num = 0x03;
    } else if (num < 61) {
        num = 0x04;
    } else if (num < 73) {
        num = 0x05;
    } else if (num < 85) {
        num = 0x06;
    } else if (num < 97) {
        num = 0x07;
    } else if (num < 109) {
        num = 0x08;
    } else if (num < 121) {
        num = 0x09;
    } else {
        num = 0x0a;
    }
    uint8_t data[MSG_SIZE] = { 0x0, 0x39, num };
    int r                  = hid_write(device_handle, data, MSG_SIZE);
    if (r < 0)
        return r;

    return save_state(device_handle);
}

static int set_mic_volume(hid_device* device_handle, uint8_t num)
{
    // 0x00 off
    // step + 0x01
    // 0x0F max
    num = num / 8;
    if (num == 16)
        num--;

    uint8_t volume[MSG_SIZE] = { 0x0, 0x37, num };
    int r                    = hid_write(device_handle, volume, MSG_SIZE);
    if (r < 0)
        return r;

    return save_state(device_handle);
}

static int set_mic_mute_led_brightness(hid_device* device_handle, uint8_t num)
{
    // Off = 0x00
    // low = 0x01
    // medium (default) = 0x04
    // high = 0x0a
    if (num == 2) {
        num = 0x04;
    } else if (num == 3) {
        num = 0x0A;
    }

    uint8_t brightness[MSG_SIZE] = { 0x0, 0xAE, num };
    int r                        = hid_write(device_handle, brightness, MSG_SIZE);

    if (r < 0)
        return r;

    return save_state(device_handle);
}

static BatteryInfo get_battery(hid_device* device_handle)
{
    unsigned char data_read[STATUS_BUF_SIZE];
    int r = read_device_status(device_handle, data_read);

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (r == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    if (r < 16)
        return info;

    unsigned char connection_status = data_read[CONNECTION_STATUS_BYTE];
    if (connection_status == HEADSET_OFFLINE)
        return info;

    unsigned char battery_status = data_read[BATTERY_STATUS_BYTE];
    switch (battery_status) {
    case HEADSET_CHARGING:
        info.status = BATTERY_CHARGING;
        break;
    default:
        info.status = BATTERY_AVAILABLE;
        break;
    }

    info.level = data_read[BATTERY_LEVEL_BYTE];

    return info;
}

static int set_inactive_time(hid_device* device_handle, uint8_t num)
{
    uint8_t data[MSG_SIZE] = { 0x0, 0xA3, num };

    return hid_write(device_handle, data, MSG_SIZE);
}

static int set_volume_limiter(hid_device* device_handle, uint8_t num)
{
    unsigned char data[MSG_SIZE] = { 0x0, 0x27, num };
    int r                        = hid_write(device_handle, data, MSG_SIZE);

    if (r < 0)
        return r;

    return save_state(device_handle);
}

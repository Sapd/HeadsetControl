#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct device device_arctis;

enum {
    ID_ARCTIS_NOVA_5_BASE_STATION = 0x2232,
    ID_ARCTIS_NOVA_5X_BASE_STATION = 0x2253,
};

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
    HEADSET_CHARGING = 0x01,
    HEADSET_OFFLINE  = 0x02,
};
enum {
    EQUALIZER_BANDS_COUNT = 10,
    EQUALIZER_BAND_MIN    = -10,
    EQUALIZER_BAND_MAX    = 10,
    EQUALIZER_BAND_BASE   = 0x14,
};

static const uint16_t PRODUCT_IDS[]
    = { ID_ARCTIS_NOVA_5_BASE_STATION, ID_ARCTIS_NOVA_5X_BASE_STATION };

static const uint8_t SAVE_DATA1[MSG_SIZE] = { 0x0, 0x09 }; // Command1 to save settings to headset
static const uint8_t SAVE_DATA2[MSG_SIZE] = { 0x0, 0x35, 0x01 }; // Command2 to save settings to headset

static int set_sidetone(hid_device* device_handle, uint8_t num);
static int set_mic_mute_led_brightness(hid_device* device_handle, uint8_t num);
static int set_mic_volume(hid_device* device_handle, uint8_t num);
static int set_inactive_time(hid_device* device_handle, uint8_t num);
static int set_volume_limiter(hid_device* device_handle, uint8_t num);
static int set_eq_preset(hid_device* device_handle, uint8_t num);
static int set_eq(hid_device* device_handle, struct equalizer_settings* settings);
static BatteryInfo get_battery(hid_device* device_handle);
static int get_chatmix(hid_device* device_handle);

static int read_device_status(hid_device* device_handle, unsigned char* data_read);
static int save_state(hid_device* device_handle);

void arctis_nova_5_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova (5/5X)", sizeof(device_arctis.device_name));

    device_arctis.capabilities                                           = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER) | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER);
    device_arctis.capability_details[CAP_SIDETONE]                       = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_CHATMIX_STATUS]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_MICROPHONE_VOLUME]              = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]                  = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_VOLUME_LIMITER]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET]               = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER]                      = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };

    device_arctis.send_sidetone                       = &set_sidetone;
    device_arctis.send_microphone_mute_led_brightness = &set_mic_mute_led_brightness;
    device_arctis.send_microphone_volume              = &set_mic_volume;
    device_arctis.request_battery                     = &get_battery;
    device_arctis.request_chatmix                     = &get_chatmix;
    device_arctis.send_inactive_time                  = &set_inactive_time;
    device_arctis.send_volume_limiter                 = &set_volume_limiter;
    device_arctis.send_equalizer_preset               = &set_eq_preset;
    device_arctis.send_equalizer                      = &set_eq;
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

static int get_chatmix(hid_device* device_handle)
{
    // modified from SteelSeries Arctis Nova 7
    // max chat 0x00, 0x64
    // max game 0x64, 0x00
    // neutral 0x64, 0x64
    // read device info
    unsigned char data_read[STATUS_BUF_SIZE];
    int r = read_device_status(device_handle, data_read);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    // it's a slider, but setting for game and chat
    // are reported as separate values, we combine
    // them back into one setting of the slider

    // the two values are between 0 and 100,
    // we translate that to a value from 0 to 128
    // with "64" being in the middle

    int game = map(data_read[5], 0, 100, 0, 64);
    int chat = map(data_read[6], 0, 100, 0, -64);

    return 64 - (chat + game);
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

static int set_eq_preset(hid_device* device_handle, uint8_t num)
{
    struct equalizer_settings preset;
    preset.size                         = EQUALIZER_BANDS_COUNT;
    float flat[EQUALIZER_BANDS_COUNT]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    float bass[EQUALIZER_BANDS_COUNT]   = { 3.5f, 5.5f, 4, 1, -1.5f, -1.5f, -1, -1, -1, -1 };
    float focus[EQUALIZER_BANDS_COUNT]  = { -5, -3.5f, -1, -3.5f, -2.5f, 4, 6, -3.5f, 0 };
    float smiley[EQUALIZER_BANDS_COUNT] = { 3, 3.5f, 1.5f, -1.5f, -4, -4, -2.5f, 1.5f, 3, 4 };
    switch (num) {
    case 0: {
        preset.bands_values = &flat[0];
        break;
    }
    case 1: {
        preset.bands_values = &bass[0];
        break;
    }
    case 2: {
        preset.bands_values = &focus[0];
        break;
    }
    case 3: {
        preset.bands_values = &smiley[0];
        break;
    }
    default:
        printf("Preset %d out of bounds\n", num);
        return HSC_OUT_OF_BOUNDS;
    }
    return set_eq(device_handle, &preset);
}

/**
 * This headset is using a Peaking EQ with configurable frequency center of a EQ band, gain and a quality factor
 */
static int set_eq(hid_device* device_handle, struct equalizer_settings* settings)
{
    if (settings->size != EQUALIZER_BANDS_COUNT) {
        printf("Device only supports %d bands.\n", EQUALIZER_BANDS_COUNT);
        return HSC_OUT_OF_BOUNDS;
    }
    uint8_t data[MSG_SIZE] = { 0x0, 0x33 };

    /**
     * constains the eq band frequencies used in "default" well known profiles used in presets
     */
    const unsigned char band_freq_le[2 * EQUALIZER_BANDS_COUNT] = {
        0x20,
        0x0,
        0x40,
        0x0,
        0x7d,
        0x0,
        0xfa,
        0x0,
        0xf4,
        0x1,
        0xe8,
        0x3,
        0xd0,
        0x7,
        0xa0,
        0xf,
        0x40,
        0x1f,
        0x80,
        0x3e,
    };

    for (size_t i = 0; i < settings->size; i++) {
        float band_value = settings->bands_values[i];
        if (band_value < EQUALIZER_BAND_MIN || band_value > EQUALIZER_BAND_MAX) {
            printf("Device only supports bands ranging from %d to %d.\n", EQUALIZER_BAND_MIN, EQUALIZER_BAND_MAX);
            return HSC_OUT_OF_BOUNDS;
        }
        uint8_t raw_gain_value = (uint8_t)(EQUALIZER_BAND_BASE + band_value * 2);
        uint8_t gain_flag      = 0x01;
        if (raw_gain_value != EQUALIZER_BAND_BASE) {
            if (i == 0) {
                gain_flag = 0x04;
            } else if (i == (settings->size - 1)) {
                gain_flag = 0x05;
            }
        }
        data[2 + 6 * i + 0] = band_freq_le[2 * i];
        data[2 + 6 * i + 1] = band_freq_le[2 * i + 1];
        data[2 + 6 * i + 2] = gain_flag;
        data[2 + 6 * i + 3] = raw_gain_value;
        // use a default quality factor of 1.414 (approximated sqrt(2)) multiplied by 1000 to have a fixed 16 bit number number
        data[2 + 6 * i + 4] = 0x86;
        data[2 + 6 * i + 5] = 0x5;
    }

    int r = hid_write(device_handle, data, MSG_SIZE);
    if (r < 0)
        return r;

    return save_state(device_handle);
}

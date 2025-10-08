#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct device device_arctis;

#define ID_ARCTIS_NOVA_5_BASE_STATION  0x2232
#define ID_ARCTIS_NOVA_5X_BASE_STATION 0x2253

#define MSG_SIZE        64
#define STATUS_BUF_SIZE 128

#define CONNECTION_STATUS_BYTE 0x01
#define BATTERY_LEVEL_BYTE     0x03
#define BATTERY_STATUS_BYTE    0x04

#define HEADSET_CHARGING 0x01
#define HEADSET_OFFLINE  0x02

// general equalizer
#define EQUALIZER_BANDS_COUNT 10
#define EQUALIZER_GAIN_STEP   0.5
#define EQUALIZER_GAIN_BASE   20 // default gain
#define EQUALIZER_GAIN_MIN    -10 // gain
#define EQUALIZER_GAIN_MAX    10
// equalizer presets
#define EQUALIZER_PRESETS_COUNT 4

// parametric equalizer only
#define EQUALIZER_Q_FACTOR_MIN     0.2 // q factor
#define EQUALIZER_Q_FACTOR_MAX     10.0
#define EQUALIZER_Q_FACTOR_DEFAULT 1.414
#define EQUALIZER_FREQ_MIN         20 // frequency
#define EQUALIZER_FREQ_MAX         20000
#define EQUALIZER_FREQ_DISABLED    20001
#define EQUALIZER_FILTERS          (B(EQ_FILTER_LOWSHELF) | B(EQ_FILTER_LOWPASS) | B(EQ_FILTER_PEAKING) | B(EQ_FILTER_HIGHPASS) | B(EQ_FILTER_HIGHSHELF))

static const uint16_t PRODUCT_IDS[]       = { ID_ARCTIS_NOVA_5_BASE_STATION, ID_ARCTIS_NOVA_5X_BASE_STATION };
static const uint8_t SAVE_DATA1[MSG_SIZE] = { 0x0, 0x09 }; // Command1 to save settings to headset
static const uint8_t SAVE_DATA2[MSG_SIZE] = { 0x0, 0x35, 0x01 }; // Command2 to save settings to headset

static float flat[EQUALIZER_BANDS_COUNT]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static float bass[EQUALIZER_BANDS_COUNT]   = { 3.5f, 5.5f, 4, 1, -1.5f, -1.5f, -1, -1, -1, -1 };
static float focus[EQUALIZER_BANDS_COUNT]  = { -5, -3.5f, -1, -3.5f, -2.5f, 4, 6, -3.5f, 0 };
static float smiley[EQUALIZER_BANDS_COUNT] = { 3, 3.5f, 1.5f, -1.5f, -4, -4, -2.5f, 1.5f, 3, 4 };

static EqualizerInfo EQUALIZER = { EQUALIZER_BANDS_COUNT, 0, 0.5, EQUALIZER_GAIN_MIN, EQUALIZER_GAIN_MAX };

static EqualizerPresets EQUALIZER_PRESETS = {
    EQUALIZER_PRESETS_COUNT,
    { { "flat", flat },
        { "bass", bass },
        { "focus", focus },
        { "smiley", smiley } }
};

static ParametricEqualizerInfo PARAMETRIC_EQUALIZER = {
    EQUALIZER_BANDS_COUNT,
    EQUALIZER_GAIN_BASE,
    EQUALIZER_GAIN_STEP,
    EQUALIZER_GAIN_MIN,
    EQUALIZER_GAIN_MAX,
    EQUALIZER_Q_FACTOR_MIN,
    EQUALIZER_Q_FACTOR_MAX,
    EQUALIZER_FREQ_MIN,
    EQUALIZER_FREQ_MAX,
    EQUALIZER_FILTERS
};

// map filter types to device band filter byte
static uint8_t EQUALIZER_FILTER_MAP[NUM_EQ_FILTER_TYPES] = {
    [EQ_FILTER_PEAKING]   = 1,
    [EQ_FILTER_LOWPASS]   = 2,
    [EQ_FILTER_HIGHPASS]  = 3,
    [EQ_FILTER_LOWSHELF]  = 4,
    [EQ_FILTER_HIGHSHELF] = 5,
};

static int nova_5_send_sidetone(hid_device* device_handle, uint8_t num);
static int nova_5_send_microphone_mute_led_brightness(hid_device* device_handle, uint8_t num);
static int nova_5_send_microphone_volume(hid_device* device_handle, uint8_t num);
static int nova_5_send_inactive_time(hid_device* device_handle, uint8_t num);
static int nova_5_send_volume_limiter(hid_device* device_handle, uint8_t num);
static int nova_5_send_equalizer_preset(hid_device* device_handle, uint8_t num);
static int nova_5_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings);
static int nova_5_send_parametric_equalizer(hid_device* device_handle, struct parametric_equalizer_settings* settings);
static int nova_5_write_device_band(struct parametric_equalizer_band* filter, uint8_t* data);

static BatteryInfo nova_5_get_battery(hid_device* device_handle);
static int nova_5_get_chatmix(hid_device* device_handle);

static int nova_5_read_device_status(hid_device* device_handle, unsigned char* data_read);
static int nova_5_save_state(hid_device* device_handle);

void arctis_nova_5_init(struct device** device)
{
    device_arctis.idVendor                = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported     = PRODUCT_IDS;
    device_arctis.numIdProducts           = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_arctis.equalizer               = &EQUALIZER;
    device_arctis.equalizer_presets       = &EQUALIZER_PRESETS;
    device_arctis.equalizer_presets_count = EQUALIZER_PRESETS_COUNT;
    device_arctis.parametric_equalizer    = &PARAMETRIC_EQUALIZER;

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova (5/5X)", sizeof(device_arctis.device_name));

    device_arctis.capabilities                                           = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER) | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER);
    device_arctis.capability_details[CAP_SIDETONE]                       = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_CHATMIX_STATUS]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_MICROPHONE_VOLUME]              = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]                  = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_VOLUME_LIMITER]                 = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET]               = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_EQUALIZER]                      = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };

    device_arctis.send_sidetone                       = &nova_5_send_sidetone;
    device_arctis.send_microphone_mute_led_brightness = &nova_5_send_microphone_mute_led_brightness;
    device_arctis.send_microphone_volume              = &nova_5_send_microphone_volume;
    device_arctis.request_battery                     = &nova_5_get_battery;
    device_arctis.request_chatmix                     = &nova_5_get_chatmix;
    device_arctis.send_inactive_time                  = &nova_5_send_inactive_time;
    device_arctis.send_volume_limiter                 = &nova_5_send_volume_limiter;
    device_arctis.send_equalizer_preset               = &nova_5_send_equalizer_preset;
    device_arctis.send_equalizer                      = &nova_5_send_equalizer;
    device_arctis.send_parametric_equalizer           = &nova_5_send_parametric_equalizer;
    *device                                           = &device_arctis;
}

static int nova_5_read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    unsigned char data_request[2] = { 0x0, 0xB0 };
    int r                         = hid_write(device_handle, data_request, sizeof(data_request));

    if (r < 0)
        return r;

    return hid_read_timeout(device_handle, data_read, STATUS_BUF_SIZE, hsc_device_timeout);
}

static int nova_5_save_state(hid_device* device_handle)
{
    int r = hid_write(device_handle, SAVE_DATA1, MSG_SIZE);
    if (r < 0)
        return r;

    return hid_write(device_handle, SAVE_DATA2, MSG_SIZE);
}

static int nova_5_send_sidetone(hid_device* device_handle, uint8_t num)
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

    return nova_5_save_state(device_handle);
}

static int nova_5_send_microphone_volume(hid_device* device_handle, uint8_t num)
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

    return nova_5_save_state(device_handle);
}

static int nova_5_send_microphone_mute_led_brightness(hid_device* device_handle, uint8_t num)
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

    return nova_5_save_state(device_handle);
}

static BatteryInfo nova_5_get_battery(hid_device* device_handle)
{
    unsigned char data_read[STATUS_BUF_SIZE];
    int r = nova_5_read_device_status(device_handle, data_read);

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

static int nova_5_get_chatmix(hid_device* device_handle)
{
    // modified from SteelSeries Arctis Nova 7
    // max chat 0x00, 0x64
    // max game 0x64, 0x00
    // neutral 0x64, 0x64
    // read device info
    unsigned char data_read[STATUS_BUF_SIZE];
    int r = nova_5_read_device_status(device_handle, data_read);

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

static int nova_5_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    uint8_t data[MSG_SIZE] = { 0x0, 0xA3, num };

    return hid_write(device_handle, data, MSG_SIZE);
}

static int nova_5_send_volume_limiter(hid_device* device_handle, uint8_t num)
{
    unsigned char data[MSG_SIZE] = { 0x0, 0x27, num };
    int r                        = hid_write(device_handle, data, MSG_SIZE);

    if (r < 0)
        return r;

    return nova_5_save_state(device_handle);
}

static int nova_5_send_equalizer_preset(hid_device* device_handle, uint8_t num)
{
    struct equalizer_settings preset;
    preset.size = EQUALIZER_BANDS_COUNT;
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
    return nova_5_send_equalizer(device_handle, &preset);
}

/**
 * This headset is using a Peaking EQ with configurable frequency center of a EQ band, gain and a quality factor
 */
static int nova_5_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings)
{
    if (settings->size != EQUALIZER_BANDS_COUNT) {
        printf("Device only supports %d bands.\n", EQUALIZER_BANDS_COUNT);
        return HSC_OUT_OF_BOUNDS;
    }
    uint8_t data[MSG_SIZE] = { 0x0, 0x33 };

    /**
     * contains the eq band frequencies used in "default" well known profiles used in presets
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
        float gain_value = settings->bands_values[i];
        if (gain_value < EQUALIZER_GAIN_MIN || gain_value > EQUALIZER_GAIN_MAX) {
            printf("Device only supports gains ranging from %d to %d.\n", EQUALIZER_GAIN_MIN, EQUALIZER_GAIN_MAX);
            return HSC_OUT_OF_BOUNDS;
        }
        uint8_t raw_gain_value = (uint8_t)(EQUALIZER_GAIN_BASE + gain_value * 2);
        uint8_t gain_flag      = 0x01;
        if (raw_gain_value != EQUALIZER_GAIN_BASE) {
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

    return nova_5_save_state(device_handle);
}

static int nova_5_send_parametric_equalizer(hid_device* device_handle, struct parametric_equalizer_settings* settings)
{
    struct parametric_equalizer_band disabled_band = {
        .frequency = EQUALIZER_FREQ_DISABLED,
        .gain      = 0,
        .q_factor  = EQUALIZER_Q_FACTOR_DEFAULT,
        .type      = EQ_FILTER_PEAKING,
    };

    if (settings->size > EQUALIZER_BANDS_COUNT) {
        printf("Device only supports up to %d equalizer bands.\n", EQUALIZER_BANDS_COUNT);
        return HSC_OUT_OF_BOUNDS;
    }

    uint8_t data[MSG_SIZE] = { 0x0, 0x33 };
    int error              = 0;

    for (size_t i = 0; i < EQUALIZER_BANDS_COUNT; i++) {
        if (i < settings->size) {
            error = nova_5_write_device_band(&settings->bands[i], &data[2 + i * 6]);
        } else {
            error = nova_5_write_device_band(&disabled_band, &data[2 + i * 6]);
        }
        if (error != 0) {
            return error;
        }
    }

    int r = hid_write(device_handle, data, MSG_SIZE);
    if (r < 0)
        return r;

    return nova_5_save_state(device_handle);
    return 0;
}

static int nova_5_write_device_band(struct parametric_equalizer_band* filter, uint8_t* data)
{
    // fprintf(stderr, "freq: %g; gain: %g; q: %g; type: %d\n", filter->frequency, filter->gain, filter->q_factor, filter->type);
    if (filter->frequency != EQUALIZER_FREQ_DISABLED) {
        if (filter->frequency < EQUALIZER_FREQ_MIN || filter->frequency > EQUALIZER_FREQ_MAX) {
            printf("Device only supports filter frequencies ranging from %d to %d.\n", EQUALIZER_FREQ_MIN, EQUALIZER_FREQ_MAX);
            return HSC_OUT_OF_BOUNDS;
        }
    }
    if (!has_capability(EQUALIZER_FILTERS, (int)filter->type)) {
        printf("Unsupported filter type.\n");
        return HSC_ERROR;
    }
    if (filter->q_factor < EQUALIZER_Q_FACTOR_MIN || filter->q_factor > EQUALIZER_Q_FACTOR_MAX) {
        printf("Device only supports filter q-factor ranging from %g to %g.\n", EQUALIZER_Q_FACTOR_MIN, EQUALIZER_Q_FACTOR_MAX);
        return HSC_OUT_OF_BOUNDS;
    }
    if (filter->gain < EQUALIZER_GAIN_MIN || filter->gain > EQUALIZER_GAIN_MAX) {
        printf("Device only supports filter gains ranging from %d to %d.\n", EQUALIZER_GAIN_MIN, EQUALIZER_GAIN_MAX);
        return HSC_OUT_OF_BOUNDS;
    }

    // write filter frequency
    data[0] = (uint16_t)filter->frequency & 0xFF; // low byte
    data[1] = ((uint16_t)filter->frequency >> 8) & 0xFF; // high byte

    // write filter type
    data[2] = EQUALIZER_FILTER_MAP[filter->type];

    // write filter gain
    data[3] = (filter->gain + 10.0) * 2.0;

    // write filter q-factor
    uint16_t q = filter->q_factor * 1000;
    data[4]    = q & 0xFF; // low byte
    data[5]    = (q >> 8) & 0xFF; // high byte

    return 0;
}

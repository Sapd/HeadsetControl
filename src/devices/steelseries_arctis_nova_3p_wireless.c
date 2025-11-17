#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdio.h>
#include <string.h>

#define MSG_SIZE 64

#define ID_ARCTIS_NOVA_3P_WIRELESS 0x2269
#define ID_ARCTIS_NOVA_3X_WIRELESS 0x226d

#define HEADSET_ONLINE  0x03
#define HEADSET_OFFLINE 0x02

#define BATTERY_MAX 0x64
#define BATTERY_MIN 0x00

#define EQUALIZER_BANDS_COUNT 10
#define EQUALIZER_GAIN_STEP   0.1
#define EQUALIZER_GAIN_BASE   0
#define EQUALIZER_GAIN_MIN    -12
#define EQUALIZER_GAIN_MAX    12

#define EQUALIZER_PRESETS_COUNT 4

#define EQUALIZER_Q_FACTOR_MIN     0.2
#define EQUALIZER_Q_FACTOR_MAX     10.0
#define EQUALIZER_Q_FACTOR_DEFAULT 1.414
#define EQUALIZER_FREQ_MIN         20
#define EQUALIZER_FREQ_MAX         20000
#define EQUALIZER_FREQ_DISABLED    20001
#define EQUALIZER_FILTERS          (B(EQ_FILTER_LOWSHELF) | B(EQ_FILTER_LOWPASS) | B(EQ_FILTER_PEAKING) | B(EQ_FILTER_HIGHPASS) | B(EQ_FILTER_HIGHSHELF) | B(EQ_FILTER_NOTCH))

static EqualizerInfo equalizer = { EQUALIZER_BANDS_COUNT, 0, EQUALIZER_GAIN_STEP, EQUALIZER_GAIN_MIN, EQUALIZER_GAIN_MAX };

static float flat[EQUALIZER_BANDS_COUNT]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static float bass[EQUALIZER_BANDS_COUNT]   = { 3.5f, 5.5f, 4, 1, -1.5f, -1.5f, -1, -1, -1, -1 };
static float focus[EQUALIZER_BANDS_COUNT]  = { -5, -3.5f, -1, -3.5f, -2.5f, 4, 6, -3.5f, 0 };
static float smiley[EQUALIZER_BANDS_COUNT] = { 3, 3.5f, 1.5f, -1.5f, -4, -4, -2.5f, 1.5f, 3, 4 };

static EqualizerPresets equalizer_presets = {
    EQUALIZER_PRESETS_COUNT,
    { { "flat", flat },
        { "bass", bass },
        { "focus", focus },
        { "smiley", smiley } }
};

static ParametricEqualizerInfo parametric_equalizer = {
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

static uint8_t EQUALIZER_FILTER_MAP[NUM_EQ_FILTER_TYPES] = {
    [EQ_FILTER_PEAKING]   = 1,
    [EQ_FILTER_LOWPASS]   = 2,
    [EQ_FILTER_HIGHPASS]  = 3,
    [EQ_FILTER_LOWSHELF]  = 4,
    [EQ_FILTER_HIGHSHELF] = 5,
    [EQ_FILTER_NOTCH]     = 6,
};

static struct device device_arctis;

static const uint16_t PRODUCT_IDS[]      = { ID_ARCTIS_NOVA_3P_WIRELESS, ID_ARCTIS_NOVA_3X_WIRELESS };
static const uint8_t SAVE_DATA[MSG_SIZE] = { 0x09 };

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_inactive_time(hid_device* device_handle, uint8_t num);
static BatteryInfo arctis_nova_3p_wireless_request_battery(hid_device* device_handle);
static int arctis_nova_3p_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings);
static int arctis_nova_3p_send_equalizer_preset(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_send_parametric_equalizer(hid_device* device_handle, struct parametric_equalizer_settings* settings);
static int arctis_nova_3p_write_device_band(struct parametric_equalizer_band* filter, uint8_t* data);
static int arctis_nova_3p_wireless_save_state(hid_device* device_handle);

void arctis_nova_3p_wireless_init(struct device** device)
{
    device_arctis.idVendor             = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported  = PRODUCT_IDS;
    device_arctis.numIdProducts        = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_arctis.equalizer            = &equalizer;
    device_arctis.equalizer_presets    = &equalizer_presets;
    device_arctis.parametric_equalizer = &parametric_equalizer;

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 3P Wireless", sizeof(device_arctis.device_name));

    // Windows support not yet working - HID packet length issues
    // Functions execute but have no effect on the device
    // See: https://github.com/Sapd/HeadsetControl/pull/417
    device_arctis.supported_platforms = PLATFORM_LINUX | PLATFORM_MACOS;

    device_arctis.capabilities = B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_MICROPHONE_VOLUME) | B(CAP_BATTERY_STATUS) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET) | B(CAP_PARAMETRIC_EQUALIZER);

    device_arctis.capability_details[CAP_SIDETONE]             = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]        = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };
    device_arctis.capability_details[CAP_MICROPHONE_VOLUME]    = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]       = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };
    device_arctis.capability_details[CAP_EQUALIZER]            = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET]     = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };
    device_arctis.capability_details[CAP_PARAMETRIC_EQUALIZER] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 0 };

    device_arctis.send_sidetone             = &arctis_nova_3p_wireless_send_sidetone;
    device_arctis.send_inactive_time        = &arctis_nova_3p_wireless_send_inactive_time;
    device_arctis.send_microphone_volume    = &arctis_nova_3p_wireless_send_microphone_volume;
    device_arctis.request_battery           = &arctis_nova_3p_wireless_request_battery;
    device_arctis.send_equalizer            = &arctis_nova_3p_send_equalizer;
    device_arctis.send_equalizer_preset     = &arctis_nova_3p_send_equalizer_preset;
    device_arctis.send_parametric_equalizer = &arctis_nova_3p_send_parametric_equalizer;

    *device = &device_arctis;
}

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 0xa);

    uint8_t data[MSG_SIZE] = { 0x39, num };
    hid_send_feature_report(device_handle, data, MSG_SIZE);

    return arctis_nova_3p_wireless_save_state(device_handle);
}

static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 0x0e);

    uint8_t volume[MSG_SIZE] = { 0x37, num };
    hid_send_feature_report(device_handle, volume, MSG_SIZE);

    return arctis_nova_3p_wireless_save_state(device_handle);
}

static int arctis_nova_3p_wireless_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // the unit of time is the minute
    if (num >= 90) {
        num = 90;
    } else if (num >= 75) {
        num = 75;
    } else if (num >= 60) {
        num = 60;
    } else if (num >= 45) {
        num = 45;
    } else if (num >= 30) {
        num = 30;
    } else if (num >= 15) {
        num = 15;
    } else if (num >= 10) {
        num = 10;
    } else if (num >= 5) {
        num = 5;
    } else if (num >= 1) {
        num = 1;
    } else {
        // this will NEVER turn off the headset after a period of inactivity
        num = 0;
    }

    uint8_t data[MSG_SIZE] = { 0xa3, num };
    hid_send_feature_report(device_handle, data, MSG_SIZE);

    return arctis_nova_3p_wireless_save_state(device_handle);
}

static BatteryInfo arctis_nova_3p_wireless_request_battery(hid_device* device_handle)
{
    int r = 0;

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    uint8_t data[MSG_SIZE] = { 0xb0 };
    r                      = hid_send_feature_report(device_handle, data, MSG_SIZE);

    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (r == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    r = hid_read_timeout(device_handle, data, 4, hsc_device_timeout);

    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (r == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    if (data[1] == HEADSET_OFFLINE) {
        info.status = BATTERY_UNAVAILABLE;
        return info;
    }

    info.status = BATTERY_AVAILABLE;
    int bat     = data[3];
    info.level  = map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
    return info;
}

static int arctis_nova_3p_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings)
{
    if (settings->size != EQUALIZER_BANDS_COUNT) {
        printf("Device only supports %d bands.\n", EQUALIZER_BANDS_COUNT);
        return HSC_OUT_OF_BOUNDS;
    }
    uint8_t data[MSG_SIZE] = { 0x33 };

    // default frequencies for each band (2 bytes per band)
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
        uint8_t raw_gain_value;
        if (gain_value < 0) {
            gain_value     = (-gain_value) / EQUALIZER_GAIN_STEP;
            raw_gain_value = (0xff - ((uint8_t)gain_value)) + 1;
        } else {
            raw_gain_value = (uint8_t)(gain_value / EQUALIZER_GAIN_STEP);
        }

        data[1 + 6 * i + 0] = band_freq_le[2 * i];
        data[1 + 6 * i + 1] = band_freq_le[2 * i + 1];
        data[1 + 6 * i + 2] = 0x1; // use Peaking EQ by default
        data[1 + 6 * i + 3] = raw_gain_value;
        // use a default quality factor of 1.414 (approximated sqrt(2)) multiplied by 1000 to have a fixed 16 bit number number
        data[1 + 6 * i + 4] = 0x86;
        data[1 + 6 * i + 5] = 0x5;
    }

    hid_send_feature_report(device_handle, data, MSG_SIZE);
    return arctis_nova_3p_wireless_save_state(device_handle);
}

static int arctis_nova_3p_send_equalizer_preset(hid_device* device_handle, uint8_t num)
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
    return arctis_nova_3p_send_equalizer(device_handle, &preset);
}

static int arctis_nova_3p_send_parametric_equalizer(hid_device* device_handle, struct parametric_equalizer_settings* settings)
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

    uint8_t data[MSG_SIZE] = { 0x33 };
    int error              = 0;

    for (size_t i = 0; i < EQUALIZER_BANDS_COUNT; i++) {
        if (i < settings->size) {
            error = arctis_nova_3p_write_device_band(&settings->bands[i], &data[1 + i * 6]);
        } else {
            error = arctis_nova_3p_write_device_band(&disabled_band, &data[1 + i * 6]);
        }
        if (error != 0) {
            return error;
        }
    }

    hid_send_feature_report(device_handle, data, MSG_SIZE);
    return arctis_nova_3p_wireless_save_state(device_handle);
}

static int arctis_nova_3p_write_device_band(struct parametric_equalizer_band* filter, uint8_t* data)
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
    float gain_value = filter->gain;
    uint8_t raw_gain_value;
    if (gain_value < 0) {
        gain_value     = (-gain_value) / EQUALIZER_GAIN_STEP;
        raw_gain_value = (0xff - ((uint8_t)gain_value)) + 1;
    } else {
        raw_gain_value = (uint8_t)(gain_value / EQUALIZER_GAIN_STEP);
    }
    data[3] = raw_gain_value;

    // write filter q-factor
    uint16_t q = filter->q_factor * 1000;
    data[4]    = q & 0xFF; // low byte
    data[5]    = (q >> 8) & 0xFF; // high byte

    return 0;
}

static int arctis_nova_3p_wireless_save_state(hid_device* device_handle)
{
    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

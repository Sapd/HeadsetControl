#include "../device.h"
#include "../utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct device device_headsetcontrol_test;

#define TESTBYTES_SEND 32

#define EQUALIZER_BANDS_COUNT   10
#define EQUALIZER_BASELINE      0
#define EQUALIZER_STEP          0.5
#define EQUALIZER_BAND_MIN      -10
#define EQUALIZER_BAND_MAX      +10
#define EQUALIZER_PRESETS_COUNT 2

// parametric equalizer
#define EQUALIZER_GAIN_BASE    20
#define EQUALIZER_GAIN_STEP    0.5
#define EQUALIZER_GAIN_MIN     -10
#define EQUALIZER_GAIN_MAX     10
#define EQUALIZER_Q_FACTOR_MIN 0.2
#define EQUALIZER_Q_FACTOR_MAX 10.0
#define EQUALIZER_FREQ_MIN     20
#define EQUALIZER_FREQ_MAX     20000
#define EQUALIZER_FILTERS      (B(EQ_FILTER_LOWSHELF) | B(EQ_FILTER_LOWPASS) | B(EQ_FILTER_PEAKING) | B(EQ_FILTER_HIGHPASS) | B(EQ_FILTER_HIGHSHELF))

static const uint16_t PRODUCT_IDS[]               = { PRODUCT_TESTDEVICE };
static EqualizerInfo EQUALIZER                    = { EQUALIZER_BANDS_COUNT, EQUALIZER_BASELINE, EQUALIZER_STEP, EQUALIZER_BAND_MIN, EQUALIZER_BAND_MAX };
static float preset_flat[EQUALIZER_BANDS_COUNT]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static float preset_random[EQUALIZER_BANDS_COUNT] = { 6, -8, 1.5, 7, -1, -7.5, -9, 0, 9, 10 };
static EqualizerPresets EQUALIZER_PRESETS         = {
    EQUALIZER_PRESETS_COUNT,
    { { "flat", preset_flat },
                { "random", preset_random } }
};
static ParametricEqualizerInfo PARAMETRIC_EQUALIZER = {
    EQUALIZER_BANDS_COUNT, EQUALIZER_GAIN_BASE, EQUALIZER_GAIN_STEP, EQUALIZER_GAIN_MIN, EQUALIZER_GAIN_MAX,
    EQUALIZER_Q_FACTOR_MIN, EQUALIZER_Q_FACTOR_MAX, EQUALIZER_FREQ_MIN, EQUALIZER_FREQ_MAX, EQUALIZER_FILTERS
};

static int headsetcontrol_test_send_sidetone(hid_device* device_handle, uint8_t num);
static BatteryInfo headsetcontrol_test_request_battery(hid_device* device_handle);
static int headsetcontrol_test_notification_sound(hid_device* device_handle, uint8_t soundid);
static int headsetcontrol_test_lights(hid_device* device_handle, uint8_t on);
static int headsetcontrol_test_send_equalizer_preset(hid_device* device_handle, uint8_t num);
static int headsetcontrol_test_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings);
static int headsetcontrol_test_send_parametric_equalizer(hid_device* device_handle, struct parametric_equalizer_settings* settings);
static int headsetcontrol_test_send_microphone_mute_led_brightness(hid_device* device_handle, uint8_t num);
static int headsetcontrol_test_send_microphone_volume(hid_device* device_handle, uint8_t num);
static int headsetcontrol_test_switch_voice_prompts(hid_device* device_handle, uint8_t on);
static int headsetcontrol_test_switch_rotate_to_mute(hid_device* device_handle, uint8_t on);
static int headsetcontrol_test_request_chatmix(hid_device* device_handle);
static int headsetcontrol_test_set_inactive_time(hid_device* device_handle, uint8_t minutes);
static int headsetcontrol_test_volume_limiter(hid_device* device_handle, uint8_t num);
static int headsetcontrol_test_bluetooth_when_powered_on(hid_device* device_handle, uint8_t num);
static int headsetcontrol_test_bluetooth_call_volume(hid_device* device_handle, uint8_t num);

extern int test_profile;

void headsetcontrol_test_init(struct device** device)
{
    if (test_profile < 0 || test_profile > 10) {
        printf("test_profile must be between 0 and 10\n");
        abort();
    }

    device_headsetcontrol_test.idVendor             = VENDOR_TESTDEVICE;
    device_headsetcontrol_test.idProductsSupported  = PRODUCT_IDS;
    device_headsetcontrol_test.numIdProducts        = 1;
    device_headsetcontrol_test.equalizer            = &EQUALIZER;
    device_headsetcontrol_test.equalizer_presets    = &EQUALIZER_PRESETS;
    device_headsetcontrol_test.parametric_equalizer = &PARAMETRIC_EQUALIZER;

    strncpy(device_headsetcontrol_test.device_name, "HeadsetControl Test device", sizeof(device_headsetcontrol_test.device_name));
    // normally filled by hid in main.c
    wcsncpy(device_headsetcontrol_test.device_hid_vendorname, L"HeadsetControl", sizeof(device_headsetcontrol_test.device_hid_vendorname) / sizeof(device_headsetcontrol_test.device_hid_vendorname[0]));
    wcsncpy(device_headsetcontrol_test.device_hid_productname, L"Test device", sizeof(device_headsetcontrol_test.device_hid_productname) / sizeof(device_headsetcontrol_test.device_hid_productname[0]));

    if (test_profile != 10) {
        device_headsetcontrol_test.capabilities = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_NOTIFICATION_SOUND) | B(CAP_LIGHTS) | B(CAP_INACTIVE_TIME) | B(CAP_CHATMIX_STATUS) | B(CAP_VOICE_PROMPTS) | B(CAP_ROTATE_TO_MUTE) | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME) | B(CAP_VOLUME_LIMITER) | B(CAP_BT_WHEN_POWERED_ON) | B(CAP_BT_CALL_VOLUME);
    } else {
        device_headsetcontrol_test.capabilities = B(CAP_SIDETONE) | B(CAP_LIGHTS) | B(CAP_BATTERY_STATUS);
    }

    device_headsetcontrol_test.send_sidetone                       = &headsetcontrol_test_send_sidetone;
    device_headsetcontrol_test.request_battery                     = &headsetcontrol_test_request_battery;
    device_headsetcontrol_test.notification_sound                  = &headsetcontrol_test_notification_sound;
    device_headsetcontrol_test.switch_lights                       = &headsetcontrol_test_lights;
    device_headsetcontrol_test.send_inactive_time                  = &headsetcontrol_test_set_inactive_time;
    device_headsetcontrol_test.request_chatmix                     = &headsetcontrol_test_request_chatmix;
    device_headsetcontrol_test.switch_voice_prompts                = &headsetcontrol_test_switch_voice_prompts;
    device_headsetcontrol_test.switch_rotate_to_mute               = &headsetcontrol_test_switch_rotate_to_mute;
    device_headsetcontrol_test.send_equalizer_preset               = &headsetcontrol_test_send_equalizer_preset;
    device_headsetcontrol_test.send_equalizer                      = &headsetcontrol_test_send_equalizer;
    device_headsetcontrol_test.send_parametric_equalizer           = &headsetcontrol_test_send_parametric_equalizer;
    device_headsetcontrol_test.send_microphone_mute_led_brightness = &headsetcontrol_test_send_microphone_mute_led_brightness;
    device_headsetcontrol_test.send_microphone_volume              = &headsetcontrol_test_send_microphone_volume;
    device_headsetcontrol_test.send_volume_limiter                 = &headsetcontrol_test_volume_limiter;
    device_headsetcontrol_test.send_bluetooth_when_powered_on      = &headsetcontrol_test_bluetooth_when_powered_on;
    device_headsetcontrol_test.send_bluetooth_call_volume          = &headsetcontrol_test_bluetooth_call_volume;

    *device = &device_headsetcontrol_test;
}

static int headsetcontrol_test_send_sidetone(hid_device* device_handle, uint8_t num)
{
    if (test_profile == 1) {
        return -1;
    }

    return TESTBYTES_SEND;
}

static BatteryInfo headsetcontrol_test_request_battery(hid_device* device_handle)
{
    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    switch (test_profile) {
    case 0:
        info.status = BATTERY_AVAILABLE;
        info.level  = 42;
        break;
    case 1:
        info.status = BATTERY_HIDERROR;
        break;
    case 2:
        info.status = BATTERY_CHARGING;
        info.level  = 50;
        break;
    case 3:
        info.status = BATTERY_AVAILABLE;
        info.level  = 64;
        break;
    case 4:
        info.status = BATTERY_UNAVAILABLE;
        break;
    case 5:
        info.status = BATTERY_TIMEOUT;
        break;
    }

    return info;
}

static int headsetcontrol_test_notification_sound(hid_device* device_handle, uint8_t soundid)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_lights(hid_device* device_handle, uint8_t on)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_send_equalizer_preset(hid_device* device_handle, uint8_t num)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_send_parametric_equalizer(hid_device* device_handle, struct parametric_equalizer_settings* settings)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_send_microphone_mute_led_brightness(hid_device* device_handle, uint8_t num)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_send_microphone_volume(hid_device* device_handle, uint8_t num)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_switch_voice_prompts(hid_device* device_handle, uint8_t on)
{
    if (test_profile == 1) {
        return -1;
    }
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_switch_rotate_to_mute(hid_device* device_handle, uint8_t on)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_request_chatmix(hid_device* device_handle)
{
    if (test_profile == 1) {
        return -1;
    } else if (test_profile == 2) {
        return -1;
    }

    return 42;
}

static int headsetcontrol_test_set_inactive_time(hid_device* device_handle, uint8_t minutes)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_volume_limiter(hid_device* device_handle, uint8_t num)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_bluetooth_when_powered_on(hid_device* device_handle, uint8_t num)
{
    return TESTBYTES_SEND;
}

static int headsetcontrol_test_bluetooth_call_volume(hid_device* device_handle, uint8_t num)
{
    return TESTBYTES_SEND;
}

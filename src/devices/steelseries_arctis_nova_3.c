#include "../device.h"

#include <hidapi.h>
#include <stdio.h>
#include <string.h>

#define MSG_SIZE 64

static struct device device_arctis;

#define ID_ARCTIS_NOVA_3 0x12ec

#define EQUALIZER_BANDS_SIZE 6
#define EQUALIZER_BASELINE   0x14
#define EQUALIZER_BAND_MIN   -6
#define EQUALIZER_BAND_MAX   +6

static const uint16_t PRODUCT_IDS[]      = { ID_ARCTIS_NOVA_3 };
static const uint8_t SAVE_DATA[MSG_SIZE] = { 0x06, 0x09 }; // Command to save settings to headset
static EqualizerInfo EQUALIZER           = { EQUALIZER_BANDS_SIZE, 0, 0.5, EQUALIZER_BAND_MIN, EQUALIZER_BAND_MAX };

static int arctis_nova_3_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_nova_3_send_equalizer_preset(hid_device* device_handle, uint8_t num);
static int arctis_nova_3_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings);
static int arctis_nova_3_send_microphone_mute_led_brightness(hid_device* device_handle, uint8_t num);
static int arctis_nova_3_send_microphone_volume(hid_device* device_handle, uint8_t num);

void arctis_nova_3_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_arctis.equalizer           = &EQUALIZER;

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 3", sizeof(device_arctis.device_name));

    device_arctis.capabilities = B(CAP_SIDETONE) | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME);
    // 0xc (3), 0xffc0 (4), 0xff00 (4)
    device_arctis.capability_details[CAP_SIDETONE]                       = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET]               = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    device_arctis.capability_details[CAP_EQUALIZER]                      = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    device_arctis.capability_details[CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    device_arctis.capability_details[CAP_MICROPHONE_VOLUME]              = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };

    device_arctis.send_sidetone                       = &arctis_nova_3_send_sidetone;
    device_arctis.send_equalizer_preset               = &arctis_nova_3_send_equalizer_preset;
    device_arctis.send_equalizer                      = &arctis_nova_3_send_equalizer;
    device_arctis.send_microphone_mute_led_brightness = &arctis_nova_3_send_microphone_mute_led_brightness;
    device_arctis.send_microphone_volume              = &arctis_nova_3_send_microphone_volume;

    *device = &device_arctis;
}

static int arctis_nova_3_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // This headset only supports 4 levels of sidetone volume, but we allow a full range of 0-128 for it. Map the volume to the correct numbers.
    if (num < 26) {
        num = 0x0;
    } else if (num < 51) {
        num = 0x1;
    } else if (num < 76) {
        num = 0x2;
    } else {
        num = 0x3;
    }

    uint8_t data[MSG_SIZE] = { 0x06, 0x39, num };
    hid_send_feature_report(device_handle, data, MSG_SIZE);

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

static int arctis_nova_3_send_equalizer_preset(hid_device* device_handle, uint8_t num)
{
    // This headset supports only 4 presets:
    // Flat (default), Bass Boost, Smiley, Focus

    switch (num) {
    case 0: {
        uint8_t flat[MSG_SIZE] = { 0x06, 0x33, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14 };
        hid_send_feature_report(device_handle, flat, MSG_SIZE);

        return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
    }
    case 1: {
        uint8_t bass[MSG_SIZE] = { 0x06, 0x33, 0x1c, 0x19, 0x11, 0x14, 0x14, 0x14 };
        hid_send_feature_report(device_handle, bass, MSG_SIZE);

        return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
    }
    case 2: {
        uint8_t smiley[MSG_SIZE] = { 0x06, 0x33, 0x1a, 0x17, 0x0f, 0x12, 0x17, 0x1a };
        hid_send_feature_report(device_handle, smiley, MSG_SIZE);

        return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
    }
    case 3: {
        uint8_t focus[MSG_SIZE] = { 0x06, 0x33, 0x0c, 0x0d, 0x11, 0x18, 0x1c, 0x14 };
        hid_send_feature_report(device_handle, focus, MSG_SIZE);

        return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
    }
    default: {
        printf("Device only supports 0-3 range for presets.\n");
        return HSC_OUT_OF_BOUNDS;
    }
    }
}

static int arctis_nova_3_send_equalizer(hid_device* device_handle, struct equalizer_settings* settings)
{
    if (settings->size != EQUALIZER_BANDS_SIZE) {
        printf("Device only supports %d bands.\n", EQUALIZER_BANDS_SIZE);
        return HSC_OUT_OF_BOUNDS;
    }

    uint8_t data[MSG_SIZE] = { 0x06, 0x33 };
    for (int i = 0; i < settings->size; i++) {
        float band_value = settings->bands_values[i];
        if (band_value < EQUALIZER_BAND_MIN || band_value > EQUALIZER_BAND_MAX) {
            printf("Device only supports bands ranging from %d to %d.\n", EQUALIZER_BAND_MIN, EQUALIZER_BAND_MAX);
            return HSC_OUT_OF_BOUNDS;
        }

        data[i + 2] = (uint8_t)(EQUALIZER_BASELINE + 2 * band_value);
    }

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

static int arctis_nova_3_send_microphone_mute_led_brightness(hid_device* device_handle, uint8_t num)
{
    // This headset only supports 3 levels:
    // Off, low, medium (default), high

    uint8_t brightness[MSG_SIZE] = { 0x06, 0xae, num };
    hid_send_feature_report(device_handle, brightness, MSG_SIZE);

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

static int arctis_nova_3_send_microphone_volume(hid_device* device_handle, uint8_t num)
{
    // This headset only supports 10 levels of microphone volume, but we allow a full range of 0-128 for it. Map the volume to the correct numbers.
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

    uint8_t volume[MSG_SIZE] = { 0x06, 0x37, num };
    hid_send_feature_report(device_handle, volume, MSG_SIZE);

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

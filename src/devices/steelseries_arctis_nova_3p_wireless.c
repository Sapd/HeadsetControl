#include "../device.h"

#include <hidapi.h>
#include <stdio.h>
#include <string.h>

#define MSG_SIZE 64

#define ID_ARCTIS_NOVA_3P_WIRELESS 0x2269

static struct device device_arctis;

static const uint16_t PRODUCT_IDS[]      = { ID_ARCTIS_NOVA_3P_WIRELESS };
static const uint8_t SAVE_DATA[MSG_SIZE] = { 0x09 }; // Command to save settings to headset

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num);

void arctis_nova_3p_wireless_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 3P Wireless", sizeof(device_arctis.device_name));

    device_arctis.capabilities = B(CAP_SIDETONE) | /* B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | */ B(CAP_MICROPHONE_VOLUME);
    // 0xc (3), 0xffc0 (4), 0xff00 (4)
    device_arctis.capability_details[CAP_SIDETONE] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    // device_arctis.capability_details[CAP_EQUALIZER_PRESET]               = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    // device_arctis.capability_details[CAP_EQUALIZER]                      = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    // device_arctis.capability_details[CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };
    device_arctis.capability_details[CAP_MICROPHONE_VOLUME] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 4 };

    device_arctis.send_sidetone = &arctis_nova_3p_wireless_send_sidetone;
    // device_arctis.send_equalizer_preset               = &arctis_nova_3_send_equalizer_preset;
    // device_arctis.send_equalizer                      = &arctis_nova_3_send_equalizer;
    // device_arctis.send_microphone_mute_led_brightness = &arctis_nova_3_send_microphone_mute_led_brightness;
    device_arctis.send_microphone_volume = &arctis_nova_3p_wireless_send_microphone_volume;

    *device = &device_arctis;
}

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // This headset supports 11 levels of sidetone volume (0x0 to 0xa),
    // mapping a full range of 0-128 input values.
    if (num < 12) {
        num = 0x0;
    } else if (num < 24) {
        num = 0x1;
    } else if (num < 36) {
        num = 0x2;
    } else if (num < 48) {
        num = 0x3;
    } else if (num < 60) {
        num = 0x4;
    } else if (num < 72) {
        num = 0x5;
    } else if (num < 84) {
        num = 0x6;
    } else if (num < 96) {
        num = 0x7;
    } else if (num < 107) {
        num = 0x8;
    } else if (num < 118) {
        num = 0x9;
    } else { // num is 118 or greater (up to 128)
        num = 0xa;
    }

    uint8_t data[MSG_SIZE] = { 0x39, num };
    hid_send_feature_report(device_handle, data, MSG_SIZE);

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num)
{
    // This headset only supports 10 levels of microphone volume, but we allow a full range of 0-128 for it. Map the volume to the correct numbers.
    if (num < 9) {
        num = 0x00;
    } else if (num < 18) {
        num = 0x01;
    } else if (num < 27) {
        num = 0x02;
    } else if (num < 36) {
        num = 0x03;
    } else if (num < 45) {
        num = 0x04;
    } else if (num < 54) {
        num = 0x05;
    } else if (num < 63) {
        num = 0x06;
    } else if (num < 72) {
        num = 0x07;
    } else if (num < 81) {
        num = 0x08;
    } else if (num < 90) {
        num = 0x09;
    } else if (num < 99) {
        num = 0x0a;
    } else if (num < 108) {
        num = 0x0b;
    } else if (num < 117) {
        num = 0x0c;
    } else if (num < 126) {
        num = 0x0d;
    } else {
        num = 0x0e;
    }

    uint8_t volume[MSG_SIZE] = { 0x37, num };
    hid_send_feature_report(device_handle, volume, MSG_SIZE);

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

#include "../device.h"

#include <hidapi.h>
#include <stdio.h>
#include <string.h>

enum { MSG_SIZE = 64 };

enum { ID_ARCTIS_NOVA_3P_WIRELESS = 0x2269 };

static struct device device_arctis;

static const uint16_t PRODUCT_IDS[]      = { ID_ARCTIS_NOVA_3P_WIRELESS };
static const uint8_t SAVE_DATA[MSG_SIZE] = { 0x09 }; // Command to save settings to headset

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_inactive_time(hid_device* device_handle, uint8_t num);

void arctis_nova_3p_wireless_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 3P Wireless", sizeof(device_arctis.device_name));

    device_arctis.capabilities           = B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_MICROPHONE_VOLUME);
    device_arctis.send_sidetone          = &arctis_nova_3p_wireless_send_sidetone;
    device_arctis.send_inactive_time     = &arctis_nova_3p_wireless_send_inactive_time;
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

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

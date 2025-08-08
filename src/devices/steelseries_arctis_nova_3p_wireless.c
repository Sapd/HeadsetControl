#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdio.h>
#include <string.h>

enum { MSG_SIZE = 64 };

enum { ID_ARCTIS_NOVA_3P_WIRELESS = 0x2269 };

enum {
    HEADSET_ONLINE  = 0x03,
    HEADSET_OFFLINE = 0x02
};

enum {
    BATTERY_MAX = 0x64,
    BATTERY_MIN = 0x00
};

static struct device device_arctis;

static const uint16_t PRODUCT_IDS[]      = { ID_ARCTIS_NOVA_3P_WIRELESS };
static const uint8_t SAVE_DATA[MSG_SIZE] = { 0x09 }; // Command to save settings to headset

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num);
static int arctis_nova_3p_wireless_send_inactive_time(hid_device* device_handle, uint8_t num);
static BatteryInfo arctis_nova_3p_wireless_request_battery(hid_device* device_handle);

void arctis_nova_3p_wireless_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova 3P Wireless", sizeof(device_arctis.device_name));

    device_arctis.capabilities           = B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_MICROPHONE_VOLUME) | B(CAP_BATTERY_STATUS);
    device_arctis.send_sidetone          = &arctis_nova_3p_wireless_send_sidetone;
    device_arctis.send_inactive_time     = &arctis_nova_3p_wireless_send_inactive_time;
    device_arctis.send_microphone_volume = &arctis_nova_3p_wireless_send_microphone_volume;
    device_arctis.request_battery        = &arctis_nova_3p_wireless_request_battery;

    *device = &device_arctis;
}

static int arctis_nova_3p_wireless_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 0xa);

    uint8_t data[MSG_SIZE] = { 0x39, num };
    hid_send_feature_report(device_handle, data, MSG_SIZE);

    return hid_send_feature_report(device_handle, SAVE_DATA, MSG_SIZE);
}

static int arctis_nova_3p_wireless_send_microphone_volume(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 0x0e);

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

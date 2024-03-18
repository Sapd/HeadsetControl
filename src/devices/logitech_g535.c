#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static struct device device_g535;

static const uint16_t PRODUCT_ID = 0x0ac4;

// Based on manual measurements so the discharge curve used to generate these values aren't always
// right, but it's good enough.
// Based on the following measured values on a brand new headset (voltage, percentage) :
// - 4175, 100
// - 4135, 98
// - 4124, 97
// - 4109, 96
// - 4106, 95
// - 4066, 90
// - 4055, 87
// - 4047, 86
// - 4036, 85
// - 4025, 84
// - 4000, 83
// - 3985, 81
// - 3974, 80
// - 3971, 79
// - 3963, 78
// - 3945, 72
// - 3934, 71
// - 3916, 67
// - 3894, 64
// - 3887, 63
// - 3872, 61
// - 3839, 56
// - 3817, 50
// - 3806, 48
// - 3788, 39
// - 3774, 34
// - 3766, 30
// - 3752, 26
// - 3741, 22
// - 3730, 20
// - 3719, 17
// - 3701, 13
// - 3688, 10
// - 3679, 8
// - 3675, 6
// - 3664, 5
// - 3640, 4
// - 3600, 3
// - 3540, 2
// - 3485, 1
// - 3445, 1
// - 3405, 1
// - 3339, 0
// - 3325, 0
// - 3310, 0
static const int battery_estimate_percentages[] = { 100, 50, 30, 20, 5, 0 };
static const int battery_estimate_voltages[]    = { 4175, 3817, 3766, 3730, 3664, 3310 };
static const size_t battery_estimate_size       = 6;

static int g535_send_sidetone(hid_device* device_handle, uint8_t num);
static BatteryInfo g535_request_battery(hid_device* device_handle);
static int g535_send_inactive_time(hid_device* device_handle, uint8_t num);

void g535_init(struct device** device)
{
    device_g535.idVendor            = VENDOR_LOGITECH;
    device_g535.idProductsSupported = &PRODUCT_ID;
    device_g535.numIdProducts       = 1;

    strncpy(device_g535.device_name, "Logitech G535", sizeof(device_g535.device_name));

    device_g535.capabilities                     = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    device_g535.capability_details[CAP_SIDETONE] = (struct capability_detail) { .usagepage = 0xc, .usageid = 0x1, .interface = 3 };
    /// TODO: usagepage and id may not be correct for battery status and inactive timer
    device_g535.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xc, .usageid = 0x1, .interface = 3 };
    device_g535.capability_details[CAP_INACTIVE_TIME]  = (struct capability_detail) { .usagepage = 0xc, .usageid = 0x1, .interface = 3 };

    device_g535.send_sidetone      = &g535_send_sidetone;
    device_g535.request_battery    = &g535_request_battery;
    device_g535.send_inactive_time = &g535_send_inactive_time;

    *device = &device_g535;
}

static int g535_send_sidetone(hid_device* device_handle, uint8_t num)
{
    int ret = 0;

    num = map(num, 0, 128, 0, 100);

    uint8_t buf[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x1d, num, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    ret = hid_send_feature_report(device_handle, buf, sizeof(buf) / sizeof(buf[0]));
    if (ret < 0) {
        return ret;
    }

    ret = hid_read_timeout(device_handle, buf, HIDPP_LONG_MESSAGE_LENGTH, hsc_device_timeout);
    if (ret < 0) {
        return ret;
    }

    if (ret == 0) {
        return HSC_READ_TIMEOUT;
    }

    // Headset offline
    if (buf[2] == 0xFF) {
        return BATTERY_UNAVAILABLE;
    }

    if (buf[4] != num) {
        return HSC_ERROR;
    }

    return ret;
}

// inspired by logitech_g533.c
static BatteryInfo g535_request_battery(hid_device* device_handle)
{
    int ret = 0;

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    // request battery voltage
    uint8_t buf[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    ret = hid_send_feature_report(device_handle, buf, sizeof(buf) / sizeof(buf[0]));
    if (ret < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    ret = hid_read_timeout(device_handle, buf, HIDPP_LONG_MESSAGE_LENGTH, hsc_device_timeout);
    if (ret < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (ret == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    // Headset offline
    if (buf[2] == 0xFF) {
        return info;
    }

    // 7th byte is state; 0x01 for idle, 0x03 for charging
    uint8_t state = buf[6];
    if (state == 0x03) {
        info.status = BATTERY_CHARGING;
    } else {
        info.status = BATTERY_AVAILABLE;
    }

    // actual voltage is byte 4 and byte 5 combined together
    const uint16_t voltage = (buf[4] << 8) | buf[5];

    info.level = spline_battery_level(battery_estimate_percentages, battery_estimate_voltages, battery_estimate_size, voltage);
    return info;
}

static int g535_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // Accepted values are 0 (never), 1, 2, 5, 10, 15, 30
    if (num > 30) {
        printf("Device only accepts 0 (never) and numbers up to 30 for inactive time\n");
        return HSC_OUT_OF_BOUNDS;
    } else if (num > 2 && num < 5) { // let numbers smaller-inclusive 2 through, set numbers smaller than 5 to 5, and round the rest up to 30
        num = 5;
    } else if (num > 5) {
        num = round_to_multiples(num, 5);
    }

    int ret = 0;

    uint8_t buf[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x2d, num, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    ret = hid_send_feature_report(device_handle, buf, sizeof(buf) / sizeof(buf[0]));
    if (ret < 0) {
        return ret;
    }

    ret = hid_read_timeout(device_handle, buf, HIDPP_LONG_MESSAGE_LENGTH, hsc_device_timeout);
    if (ret < 0) {
        return ret;
    }

    if (ret == 0) {
        return HSC_READ_TIMEOUT;
    }

    // Headset offline
    if (buf[2] == 0xFF) {
        return BATTERY_UNAVAILABLE;
    }

    if (buf[4] != num) {
        return HSC_ERROR;
    }

    return ret;
}

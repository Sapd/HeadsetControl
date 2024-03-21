#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <math.h>
#include <string.h>

#define MSG_SIZE 64

static struct device device_g533;

static const uint16_t PRODUCT_ID = 0x0a66;

static int g533_send_sidetone(hid_device* device_handle, uint8_t num);
static BatteryInfo g533_request_battery(hid_device* device_handle);
static int g533_send_inactive_time(hid_device* device_handle, uint8_t num);

void g533_init(struct device** device)
{
    device_g533.idVendor            = VENDOR_LOGITECH;
    device_g533.idProductsSupported = &PRODUCT_ID;
    device_g533.numIdProducts       = 1;

    strncpy(device_g533.device_name, "Logitech G533", sizeof(device_g533.device_name));

    device_g533.capabilities                           = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    device_g533.capability_details[CAP_SIDETONE]       = (struct capability_detail) { .usagepage = 0xff00, .usageid = 0x1, .interface = 3 };
    device_g533.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xff43, .usageid = 0x202, .interface = 3 };
    device_g533.capability_details[CAP_INACTIVE_TIME]  = (struct capability_detail) { .usagepage = 0xff43, .usageid = 0x0202 };
    device_g533.request_battery                        = &g533_request_battery;
    device_g533.send_sidetone                          = &g533_send_sidetone;
    device_g533.send_inactive_time                     = &g533_send_inactive_time;

    *device = &device_g533;
}

static int g533_send_sidetone(hid_device* device_handle, uint8_t num)
{

    num = map(num, 0, 128, 200, 255);

    uint8_t data[MSG_SIZE] = { 0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num, 0, 0, 0, 0 };

    for (int i = 16; i < MSG_SIZE; i++)
        data[i] = 0;

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

// mostly copied from logitech_g933_935.c
static int estimate_battery_level(uint16_t voltage)
{
    if (voltage <= 3618)
        return (int)((0.017547 * voltage) - 53.258578);
    if (voltage > 4011)
        return 100;

    // Interpolated from https://github.com/ashkitten/g933-utils/blob/master/libg933/src/maps/0A66/discharging.csv
    // Errors: Min 0.0 Max 14.56	AVG: 3.9485493530204
    return map((int)(round(-0.0000010876 * pow(voltage, 3) + 0.0122392434 * pow(voltage, 2) - 45.6420832787 * voltage + 56445.8517589238)),
        25, 100, 20, 100);
}

static BatteryInfo g533_request_battery(hid_device* device_handle)
{
    /*
        CREDIT GOES TO https://github.com/ashkitten/ for the project
        https://github.com/ashkitten/g933-utils/
        I've simply ported that implementation to this project!
    */

    int r = 0;

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    // request battery voltage
    uint8_t data_request[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x07, 0x01 };

    r = hid_write(device_handle, data_request, HIDPP_LONG_MESSAGE_LENGTH);
    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    uint8_t data_read[7];
    r = hid_read_timeout(device_handle, data_read, 7, hsc_device_timeout);
    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (r == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    // Headset offline
    if (data_read[2] == 0xFF)
        return info;

    // 6th byte is state; 0x1 for idle, 0x3 for charging
    uint8_t state = data_read[6];
    if (state == 0x03) {
        info.status = BATTERY_CHARGING;
    } else {
        info.status = BATTERY_AVAILABLE;
    }

#ifdef DEBUG
    printf("G33 - g533_request_battery - b1: 0x%08x b2: 0x%08x\n", data_read[4], data_read[5]);
#endif

    // actual voltage is byte 4 and byte 5 combined together
    const uint16_t voltage = (data_read[4] << 8) | data_read[5];

#ifdef DEBUG
    printf("G33 - g533_request_battery - Reported Voltage: %2d\n", voltage);
#endif

    info.level = estimate_battery_level(voltage);
    return info;
}

static int g533_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    /*
        CREDIT GOES TO https://github.com/ashkitten/ for the project
        https://github.com/ashkitten/g933-utils/
        I've simply ported that implementation to this project!
    */

    int r = 0;
    // request battery voltage
    uint8_t data_request[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x07, 0x21, num };

    r = hid_write(device_handle, data_request, HIDPP_LONG_MESSAGE_LENGTH);
    if (r < 0)
        return r;

    uint8_t data_read[7];
    r = hid_read_timeout(device_handle, data_read, 7, hsc_device_timeout);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    // Headset offline
    if (data_read[2] == 0xFF)
        return HSC_ERROR;

#ifdef DEBUG
    printf("G33 - g533_send_inactive_time - b1: 0x%08x\n", data_read[4]);
#endif

    return r;
}

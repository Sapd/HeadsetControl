#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <math.h>
#include <string.h>

#define MSG_SIZE 20

static struct device device_g535;

static const uint16_t PRODUCT_ID = 0x0ac4;

static int g535_send_sidetone(hid_device* device_handle, uint8_t num);
// static int g535_request_battery(hid_device* device_handle);

void g535_init(struct device** device)
{
    device_g535.idVendor            = VENDOR_LOGITECH;
    device_g535.idProductsSupported = &PRODUCT_ID;
    device_g535.numIdProducts       = 1;

    strncpy(device_g535.device_name, "Logitech G535", sizeof(device_g535.device_name));

    device_g535.capabilities                           = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    device_g535.capability_details[CAP_SIDETONE]       = (struct capability_detail) { .usagepage = 0xc, .usageid = 0x1, .interface = 3 };
    device_g535.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xff43, .usageid = 0x202, .interface = 3 };
    device_g535.send_sidetone                          = &g535_send_sidetone;
    device_g535.request_battery                        = &g535_request_battery;

    *device = &device_g535;
}

static int g535_send_sidetone(hid_device* device_handle, uint8_t num)
{
    uint8_t turn_sidetone_data[MSG_SIZE] = { 0x11, 0xff, 0x04, 0x0e };

    for (int i = 16; i < MSG_SIZE; i++)
        turn_sidetone_data[i] = 0;

    hid_send_feature_report(device_handle, turn_sidetone_data, MSG_SIZE);

    num = map(num, 0, 128, 0, 100);

    uint8_t set_sidetone_data[MSG_SIZE] = { 0x11, 0xff, 0x04, 0x1e, num };

    for (int i = 16; i < MSG_SIZE; i++)
        set_sidetone_data[i] = 0;

    return hid_send_feature_report(device_handle, set_sidetone_data, MSG_SIZE);
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

static int g535_request_battery(hid_device* device_handle)
{
    /*
        CREDIT GOES TO https://github.com/ashkitten/ for the project
        https://github.com/ashkitten/g933-utils/
        I've simply ported that implementation to this project!
    */

    int r = 0;
    // request battery voltage
    uint8_t data_request[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x07, 0x01 };

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

    // 6th byte is state; 0x1 for idle, 0x3 for charging
    uint8_t state = data_read[6];
    if (state == 0x03)
        return BATTERY_CHARGING;

    // actual voltage is byte 4 and byte 5 combined together
    const uint16_t voltage = (data_read[4] << 8) | data_read[5];

    return estimate_battery_level(voltage);
}

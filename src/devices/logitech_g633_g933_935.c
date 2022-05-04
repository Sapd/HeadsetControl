#include "../device.h"
#include "logitech.h"

#include <hidapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct device device_g933_935;

#define ID_LOGITECH_G633 0x0a5c
#define ID_LOGITECH_G635 0x0a89
#define ID_LOGITECH_G933 0x0a5b
#define ID_LOGITECH_G935 0x0a87
#define ID_LOGITECH_G733 0x0ab5

static const uint16_t PRODUCT_IDS[] = { ID_LOGITECH_G633, ID_LOGITECH_G635, ID_LOGITECH_G933, ID_LOGITECH_G935, ID_LOGITECH_G733 };

static int g933_935_send_sidetone(hid_device* device_handle, uint8_t num);
static int g933_935_request_battery(hid_device* device_handle);
static int g933_935_lights(hid_device* device_handle, uint8_t on);

void g933_935_init(struct device** device)
{
    device_g933_935.idVendor            = VENDOR_LOGITECH;
    device_g933_935.idProductsSupported = PRODUCT_IDS;
    device_g933_935.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    strncpy(device_g933_935.device_name, "Logitech G633/G635/G933/G935", sizeof(device_g933_935.device_name));

    device_g933_935.capabilities = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS);
    /// TODO: usagepages and ids may not be correct for all features
    device_g933_935.capability_details[CAP_SIDETONE]       = (struct capability_detail) { .usagepage = 0xff43, .usageid = 0x0202 };
    device_g933_935.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xff43, .usageid = 0x0202 };
    device_g933_935.capability_details[CAP_LIGHTS]         = (struct capability_detail) { .usagepage = 0xff43, .usageid = 0x0202 };

    device_g933_935.send_sidetone   = &g933_935_send_sidetone;
    device_g933_935.request_battery = &g933_935_request_battery;
    device_g933_935.switch_lights   = &g933_935_lights;

    *device = &device_g933_935;
}

static float estimate_battery_level(uint16_t voltage)
{
    if (voltage <= 3525)
        return (float)((0.03 * voltage) - 101);
    if (voltage > 4030)
        return (float)100.0;
    // f(x)=3.7268473047*10^(-9)x^(4)-0.00005605626214573775*x^(3)+0.3156051902814949*x^(2)-788.0937250298629*x+736315.3077118985
    return (float)(0.0000000037268473047 * pow(voltage, 4) - 0.00005605626214573775 * pow(voltage, 3) + 0.3156051902814949 * pow(voltage, 2) - 788.0937250298629 * voltage + 736315.3077118985);
}

static int g933_935_request_battery(hid_device* device_handle)
{
    /*
        CREDIT GOES TO https://github.com/ashkitten/ for the project
        https://github.com/ashkitten/g933-utils/
        I've simply ported that implementation to this project!
    */

    int r = 0;
    // request battery voltage
    uint8_t data_request[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x08, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    r = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    if (r < 0)
        return r;

    uint8_t data_read[7];
    r = hid_read_timeout(device_handle, data_read, 7, hsc_device_timeout);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    //6th byte is state; 0x1 for idle, 0x3 for charging
    uint8_t state = data_read[6];
    if (state == 0x03)
        return BATTERY_CHARGING;

#ifdef DEBUG
    printf("G33 - g933_request_battery - b1: 0x%08x b2: 0x%08x\n", data_read[4], data_read[5]);
#endif

    // actual voltage is byte 4 and byte 5 combined together
    const uint16_t voltage = (data_read[4] << 8) | data_read[5];

#ifdef DEBUG
    printf("G33 - g933_request_battery - Reported Voltage: %2f\n", (float)voltage);
#endif

    return (int)(roundf(estimate_battery_level(voltage)));
}

static int g933_935_send_sidetone(hid_device* device_handle, uint8_t num)
{
    if (num > 0x64)
        num = 0x64;

    uint8_t data_send[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x07, 0x1a, num, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#ifdef DEBUG
    printf("G33 - setting sidetone to: %2x", num);
#endif

    return hid_write(device_handle, data_send, sizeof(data_send) / sizeof(data_send[0]));
}

static int g933_935_lights(hid_device* device_handle, uint8_t on)
{
    // on, breathing  11 ff 04 3c 01 (0 for logo) 02 00 b6 ff 0f a0 00 64 00 00 00
    // off            11 ff 04 3c 01 (0 for logo) 00
    // logo and strips can be controlled individually

    uint8_t data_on[HIDPP_LONG_MESSAGE_LENGTH]  = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x01, 0x02, 0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00 };
    uint8_t data_off[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int res;
    res = hid_write(device_handle, on ? data_on : data_off, HIDPP_LONG_MESSAGE_LENGTH);
    if (res < 0)
        return res;

    // TODO Investigate further.
    usleep(1 * 1000); // wait before next request, otherwise device ignores one of them, on windows at least.
    // turn logo lights on/off
    uint8_t data_logo_on[HIDPP_LONG_MESSAGE_LENGTH]  = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x00, 0x02, 0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00 };
    uint8_t data_logo_off[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    res                                              = hid_write(device_handle, on ? data_logo_on : data_logo_off, HIDPP_LONG_MESSAGE_LENGTH);

    return res;
}

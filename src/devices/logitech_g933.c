#include "../device.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <hidapi.h>
//#define DEBUG

static struct device device_g933;

static const uint16_t PRODUCT_ID = 0x0a5b;

static int g933_request_battery(hid_device *device_handle);

static int g933_send_sidetone(hid_device *device_hadle, uint8_t num);
static int g933_lights(hid_device *device_handle, uint8_t on);


void g933_init(struct device ** device)
{ 
    device_g933.idVendor = VENDOR_LOGITECH;
    device_g933.idProductsSupported = &PRODUCT_ID;
    device_g933.numIdProducts = 1;

    strcpy(device_g933.device_name, "Logitech G933 Wireless");

    device_g933.capabilities = CAP_BATTERY_STATUS | CAP_SIDETONE | CAP_LIGHTS;

    device_g933.request_battery = &g933_request_battery;
    device_g933.send_sidetone = &g933_send_sidetone;
    device_g933.switch_lights = &g933_lights;

    *device = &device_g933;
}

static float estimate_battery_level(uint16_t voltage)
{
    if (voltage <= 3525) return (0.03 * voltage) - 101;
    if (voltage > 4030) return 100.0;
    // f(x)=3.7268473047*10^(-9)x^(4)-0.00005605626214573775*x^(3)+0.3156051902814949*x^(2)-788.0937250298629*x+736315.3077118985
    return 0.0000000037268473047 * pow(voltage, 4) - 0.00005605626214573775
        * pow(voltage, 3) + 0.3156051902814949 * pow(voltage, 2) - 788.0937250298629 * voltage + 736315.3077118985;
}

static int g933_request_battery(hid_device *device_handle)
{
    /*
        CREDIT GOES TO https://github.com/ashkitten/ for the project
        https://github.com/ashkitten/g933-utils/
        I've simply ported that implementation to this project!
    */

    int r = 0;
    // request battery voltage
    unsigned char data_request[20] = {0x11, 0xFF, 0x08, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    
    r = hid_write(device_handle, data_request, sizeof(data_request)/sizeof(data_request[0]));
    if (r < 0) return r;

    unsigned char data_read[7];
    r = hid_read(device_handle, data_read, 7);
    if (r < 0) return r;

    //6th byte is state; 0x1 for idle, 0x3 for charging
    unsigned char state = data_read[6];
    if (state == 0x03) return BATTERY_LOADING;

    #ifdef DEBUG
    printf("b1: 0x%08x b2: 0x%08x\n", data_read[4], data_read[5]);
    #endif
    // actual voltage is byte 4 and byte 5 combined together
    const uint16_t voltage = (data_read[4] << 8) | data_read[5];
    
    #ifdef DEBUG
    printf("Reported Voltage: %2f\n", (float)voltage);
    #endif

    return round(estimate_battery_level(voltage));

    }

    static int g933_send_sidetone(hid_device *device_handle, uint8_t num)
    {
    if (num > 0x64) num = 0x64;
    unsigned char data_send[5] = {0x11, 0xff, 0x07, 0x1a, num};

    #ifdef DEBUG
    printf("setting to: %2x", num);
    #endif
    return hid_write(device_handle, data_send, 5);
}

static g933_lights(hid_device *device_handle, uint8_t on)
{
    //on, breathing   11 ff 04 3c 01 02 00 b6 ff 0f a0 00 64 00 00 00
    // off            11 ff 04 3c 01 00 
    if (on) 
    {
        unsigned char data[16] = {0x11, 0xff, 0x04, 0x3c, 0x01, 0x02, 0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00};
        return hid_write(device_handle, data, 16);
    }
    else
    {
        unsigned char data[6] = {0x11, 0xff, 0x04, 0x3c, 0x01, 0x00};
        return hid_write(device_handle, data, 6);
    }
}

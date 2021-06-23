#include "../device.h"

#define DEBUG

#include <hidapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef DEBUG
#include <stdio.h>
#endif

static struct device device_cflight;

// timeout for hidapi requests in milliseconds

#define TIMEOUT 2000
#define VENDOR_HYPERX 0x0951
#define ID_CFLIGHT_OLD 0x16C4
#define ID_CFLIGHT_NEW 0x1723

static const uint16_t PRODUCT_IDS[] = { ID_CFLIGHT_OLD, ID_CFLIGHT_NEW };

//static int cflight_send_sidetone(hid_device* device_handle, uint8_t num);
static int cflight_request_battery(hid_device* device_handle);
//static int cflight_lights(hid_device* device_handle, uint8_t on);

void cflight_init(struct device** device)
{
    device_cflight.idVendor = VENDOR_HYPERX;
    device_cflight.idProductsSupported = PRODUCT_IDS;
    device_cflight.numIdProducts = sizeof(PRODUCT_IDS)/sizeof(PRODUCT_IDS[0]);
    //device_cflight.idUsagePage = 0xff90; //TODO: the old one uses 0xff53
    //device_cflight.idUsagePage = 0xff53;
    //device_cflight.idUsage = 0x0303;
    strncpy(device_cflight.device_name, "HyperX Cloud Flight Wireless", 
            sizeof(device_cflight.device_name));

    //device_cflight.capabilities = CAP_SIDETONE | CAP_BATTERY_STATUS | CAP_LIGHTS;
    device_cflight.capabilities = CAP_BATTERY_STATUS;
    //device_cflight.send_sidetone = &cflight_send_sidetone;
    device_cflight.request_battery = &cflight_request_battery;
    //device_cflight.switch_lights = &cflight_lights;

    *device = &device_cflight;
}

/*static float estimate_battery_level(uint16_t voltage)
{
    if (voltage <= 3525)
        return (float)((0.03 * voltage) - 101);
    if (voltage > 4030)
        return (float)100.0;
    // f(x)=3.7268473047*10^(-9)x^(4)-0.00005605626214573775*x^(3)+0.3156051902814949*x^(2)-788.0937250298629*x+736315.3077118985
    return (float)(0.0000000037268473047 * pow(voltage, 4) - 0.00005605626214573775 * pow(voltage, 3) + 0.3156051902814949 * pow(voltage, 2) - 788.0937250298629 * voltage + 736315.3077118985);
}*/

uint8_t calculatePercentage(uint8_t magicValue, uint8_t chargeState) {
    // this is more or less from 
    // https://github.com/srn/hyperx-cloud-flight-wireless/blob/main/index.js
    // Copyright (c) 2020 Søren Brokær
    // MIT License (see LICENSE file)
    // TODO: review this copyrigh notice

    if (chargeState == 0x10) {
      //emitter.emit('charging', magicValue >= 20)

      if (magicValue <= 11) {
        return 200; // full?
      }
      return 199; // charging
    }


    if (chargeState == 0xf) {
      if (magicValue >= 130) {
        return 100;
      }

      if (magicValue < 130 && magicValue >= 120) {
        return 95;
      }

      if (magicValue < 120 && magicValue >= 100) {
        return 90;
      }

      if (magicValue < 100 && magicValue >= 70) {
        return 85;
      }

      if (magicValue < 70 && magicValue >= 50) {
        return 80;
      }

      if (magicValue < 50 && magicValue >= 20) {
        return 75;
      }

      if (magicValue < 20 && magicValue > 0) {
        return 70;
      }
    }
    if (chargeState == 0xe) {
      if (magicValue < 250 && magicValue > 240) {
        return 65;
      }

      if (magicValue < 240 && magicValue >= 220) {
        return 60;
      }

      if (magicValue < 220 && magicValue >= 208) {
        return 55;
      }

      if (magicValue < 208 && magicValue >= 200) {
        return 50;
      }

      if (magicValue < 200 && magicValue >= 190) {
        return 45;
      }

      if (magicValue < 190 && magicValue >= 180) {
        return 40;
      }

      if (magicValue < 179 && magicValue >= 169) {
        return 35;
      }

      if (magicValue < 169 && magicValue >= 159) {
        return 30;
      }

      if (magicValue < 159 && magicValue >= 148) {
        return 25;
      }

      if (magicValue < 148 && magicValue >= 119) {
        return 20;
      }

      if (magicValue < 119 && magicValue >= 90) {
        return 15;
      }

      if (magicValue < 90) {
        return 10;
      }
    }
    return 255; //error
}

static int cflight_request_battery(hid_device* device_handle)
{
    // this is more or less from 
    // https://github.com/srn/hyperx-cloud-flight-wireless/blob/main/index.js
    // Copyright (c) 2020 Søren Brokær
    // MIT License (see LICENSE file)
    // TODO: review this copyrigh notice

    int r = 0;
    // request battery voltage
    uint8_t data_request[] = {0x21, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    r = hid_write(device_handle, data_request, sizeof(data_request) / sizeof(data_request[0]));
    if (r < 0)
        return r;

    uint8_t data_read[20];
    r = hid_read_timeout(device_handle, data_read, 20, TIMEOUT);
    if (r < 0)
        return r;
    if (r == 0) // timeout
        return HSC_ERROR;

    if (r == 0xf || r == 0x14 ) {
#ifdef DEBUG
        printf("cflight_request_battery data_read 3: 0x%08x 4: 0x%08x\n", data_read[3], data_read[4]);
#endif
    
        uint8_t chargeState = data_read[3];
        uint8_t magicValue = data_read[4]; // || chargeState;
        if (magicValue == 0) magicValue = chargeState;
    
#ifdef DEBUG
        printf("cflight_request_battery chargeState: 0x%08x magicValue: 0x%08x\n", chargeState, magicValue);
#endif
    
        uint8_t batteryLevel = calculatePercentage(magicValue, chargeState);
    
#ifdef DEBUG
        printf("cflight_request_battery calculated: %i\n", batteryLevel );
#endif
    
        if (batteryLevel <= 100)
          return batteryLevel;
    
        if (batteryLevel == 199)
          return BATTERY_CHARGING;
    
        if (batteryLevel == 200)
          return 100;
    
        // either if batteryLevel == 255 or ... 
        return HSC_ERROR;
    }
    
    // we've read other functionality (other read length) 
    // which is currently not supported
    return HSC_ERROR;
}

/*
static int cflight_send_sidetone(hid_device* device_handle, uint8_t num)
{
    if (num > 0x64)
        num = 0x64;

    uint8_t data_send[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x07, 0x1a, num, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#ifdef DEBUG
    printf("G33 - setting sidetone to: %2x", num);
#endif

    return hid_write(device_handle, data_send, sizeof(data_send) / sizeof(data_send[0]));
}

static int cflight_lights(hid_device* device_handle, uint8_t on)
{
    // on, breathing  11 ff 04 3c 01 (0 for logo) 02 00 b6 ff 0f a0 00 64 00 00 00
    // off            11 ff 04 3c 01 (0 for logo) 00
    // logo and strips can be controlled individually

    uint8_t data_on[HIDPP_LONG_MESSAGE_LENGTH] = { :IDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x01, 0x02, 0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00 };
    uint8_t data_off[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int res;
    res = hid_write(device_handle, on ? data_on : data_off, HIDPP_LONG_MESSAGE_LENGTH);
    if (res < 0)
        return res;

    // TODO Investigate further.
    usleep(1 * 1000); // wait before next request, otherwise device ignores one of them, on windows at least.
    // turn logo lights on/off
    uint8_t data_logo_on[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x00, 0x02, 0x00, 0xb6, 0xff, 0x0f, 0xa0, 0x00, 0x64, 0x00, 0x00, 0x00 };
    uint8_t data_logo_off[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x04, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    res = hid_write(device_handle, on ? data_logo_on : data_logo_off, HIDPP_LONG_MESSAGE_LENGTH);

    return res;
}*/

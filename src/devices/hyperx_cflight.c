// written by hede <github5562@der-he.de>
// This file is part of HeadsetControl.

#include "../device.h"

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

static int cflight_request_battery(hid_device* device_handle);

void cflight_init(struct device** device)
{
    device_cflight.idVendor = VENDOR_HYPERX;
    device_cflight.idProductsSupported = PRODUCT_IDS;
    device_cflight.numIdProducts = sizeof(PRODUCT_IDS)/sizeof(PRODUCT_IDS[0]);
    strncpy(device_cflight.device_name, "HyperX Cloud Flight Wireless", 
            sizeof(device_cflight.device_name));

    device_cflight.capabilities = CAP_BATTERY_STATUS;
    device_cflight.request_battery = &cflight_request_battery;

    *device = &device_cflight;
}

uint8_t calculatePercentage(uint8_t magicValue, uint8_t chargeState) {
    // this part is more or less derived from 
    // https://github.com/srn/hyperx-cloud-flight-wireless/blob/main/index.js
    // Copyright (c) 2020 Søren Brokær
    // MIT License 

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
    
    // we've read other functionality here (other read length) 
    // which is currently not supported
    return HSC_ERROR;
}


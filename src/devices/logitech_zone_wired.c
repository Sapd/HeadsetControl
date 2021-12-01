#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <hidapi.h>
#include <string.h>

#define MSG_SIZE 20

static struct device device_zone_wired;

#define ID_LOGITECH_ZONE_WIRED 0x0aad
#define ID_LOGITECH_ZONE_750   0x0ade

static const uint16_t PRODUCT_IDS[] = {
    ID_LOGITECH_ZONE_WIRED,
    ID_LOGITECH_ZONE_750,
};

static int zone_wired_send_sidetone(hid_device* device_handle, uint8_t num);
static int zone_wired_switch_voice_prompts(hid_device* device_handle, uint8_t on);
static int zone_wired_switch_rotate_to_mute(hid_device* device_handle, uint8_t on);

void zone_wired_init(struct device** device)
{
    device_zone_wired.idVendor            = VENDOR_LOGITECH;
    device_zone_wired.idProductsSupported = PRODUCT_IDS;
    device_zone_wired.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_zone_wired.device_name, "Logitech Zone Wired/Zone 750", sizeof(device_zone_wired.device_name));

    device_zone_wired.capabilities          = B(CAP_SIDETONE) | B(CAP_VOICE_PROMPTS) | B(CAP_ROTATE_TO_MUTE);
    device_zone_wired.send_sidetone         = &zone_wired_send_sidetone;
    device_zone_wired.switch_voice_prompts  = &zone_wired_switch_voice_prompts;
    device_zone_wired.switch_rotate_to_mute = &zone_wired_switch_rotate_to_mute;

    *device = &device_zone_wired;
}

static int zone_wired_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // The sidetone volume of the Zone Wired is configured in steps of 10%, with 0x00 = 0% and 0x0A = 100%
    uint8_t raw_volume     = map(num, 0, 128, 0, 10);
    uint8_t data[MSG_SIZE] = { 0x22, 0xF1, 0x04, 0x00, 0x04, 0x3d, raw_volume, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

static int zone_wired_switch_voice_prompts(hid_device* device_handle, uint8_t on)
{
    uint8_t data[MSG_SIZE] = { 0x22, 0xF1, 0x04, 0x00, 0x05, 0x3d, on, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

static int zone_wired_switch_rotate_to_mute(hid_device* device_handle, uint8_t on)
{
    uint8_t data[MSG_SIZE] = { 0x22, 0xF1, 0x04, 0x00, 0x05, 0x6d, on, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

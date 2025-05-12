#include "../device.h"

#include <stdio.h>
#include <string.h>

static struct device device_maxwell;

#define ID_MAXWELL 0x4b19

static const uint16_t PRODUCT_IDS[] = { ID_MAXWELL };

static int audeze_maxwell_send_sidetone(hid_device* device_handle, uint8_t num);
// static BatteryInfo audeze_maxwell_get_battery(hid_device* device_handle);

#define MSG_SIZE 62

void audeze_maxwell_init(struct device** device)
{
    device_maxwell.idVendor            = VENDOR_AUDEZE;
    device_maxwell.idProductsSupported = PRODUCT_IDS;
    device_maxwell.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_maxwell.device_name, "Audeze Maxwell", sizeof(device_maxwell.device_name));

    device_maxwell.capabilities = B(CAP_SIDETONE);
    device_maxwell.capability_details[CAP_SIDETONE] = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };
    // device_audeze_maxwell.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xff13, .usageid = 0x1, .interface = 1 };

    device_maxwell.send_sidetone = &audeze_maxwell_send_sidetone;
    // device_audeze_maxwell.request_battery = &audeze_maxwell_get_battery;

    *device = &device_maxwell;
}

// Battery reading couldn’t be implemented because GET_REPORT(Input) isn’t exposed by HIDAPI and requires a custom control transfer.
// static BatteryInfo audeze_maxwell_get_battery(hid_device* device_handle)
// {
//
//     BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };
//
//     unsigned char outbuf[MSG_SIZE] = {0};
//
//     // request battery status
//     unsigned char data_request[MSG_SIZE] = {0x6, 0x7, 0x80, 0x5, 0x5a, 0x3, 0x0, 0xd6, 0xc, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
//     memcpy(outbuf, data_request, MSG_SIZE);
//
//     int response = hid_write(device_handle, outbuf, MSG_SIZE);
//
//     if (response < 0) {
//         info.status = BATTERY_HIDERROR;
//         return info;
//     }
//
//     return info;
// }

static int audeze_maxwell_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // Audeze HQ changes the byte at index 11, but it has no effect, it’s always toggleable regardless of what’s sent.
    uint8_t on_off = num == 0 ? 0x0 : 0x1;

    unsigned char data_request[MSG_SIZE] = {0x6, 0x9, 0x80, 0x5, 0x5a, 0x5, 0x0, 0x82, 0x2c, 0x7, 0x0, on_off, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

    return hid_write(device_handle, data_request, MSG_SIZE);
}

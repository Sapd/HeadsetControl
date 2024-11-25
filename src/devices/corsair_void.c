#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <string.h>

static struct device device_void;

enum void_wireless_battery_flags {
    VOID_BATTERY_MICUP = 128
};

#define ID_CORSAIR_VOID_WIRELESS                   0x1b27
#define ID_CORSAIR_VOID_PRO                        0x0a14
#define ID_CORSAIR_VOID_PRO_R2                     0x0a16
#define ID_CORSAIR_VOID_PRO_USB                    0x0a17
#define ID_CORSAIR_VOID_PRO_USB_2                  0x0a1d
#define ID_CORSAIR_VOID_PRO_WIRELESS               0x0a1a
#define ID_CORSAIR_VOID_RGB_USB                    0x1b2a
#define ID_CORSAIR_VOID_RGB_USB_2                  0x1b23
#define ID_CORSAIR_VOID_RGB_USB_3                  0x1b29
#define ID_CORSAIR_VOID_RGB_WIRED                  0x1b1c
#define ID_CORSAIR_VOID_ELITE_WIRELESS             0x0a55
#define ID_CORSAIR_VOID_RGB_ELITE_WIRELESS         0x0a51
#define ID_CORSAIR_VOID_ELITE_USB                  0x0a52
#define ID_CORSAIR_VOID_ELITE_SURROUND_USB_ADAPTER 0x0a53
#define ID_CORSAIR_HS70_WIRELESS                   0x0a38
#define ID_CORSAIR_HS70_PRO                        0x0a4f
#define ID_CORSAIR_VOID_RGB_WIRELESS               0x0a2b
#define ID_CORSAIR_VOID_ELITE_WIRELESS_PREMIUM_RGB 0x0a75
#define ID_CORSAIR_VOID_ELITE_USB_RGB              0x0a56

static const uint16_t PRODUCT_IDS[] = {
    ID_CORSAIR_VOID_RGB_WIRED,
    ID_CORSAIR_VOID_WIRELESS,
    ID_CORSAIR_VOID_PRO,
    ID_CORSAIR_VOID_PRO_R2,
    ID_CORSAIR_VOID_PRO_USB,
    ID_CORSAIR_VOID_PRO_USB_2,
    ID_CORSAIR_VOID_PRO_WIRELESS,
    ID_CORSAIR_VOID_RGB_USB,
    ID_CORSAIR_VOID_RGB_USB_2,
    ID_CORSAIR_VOID_RGB_USB_3,
    ID_CORSAIR_VOID_ELITE_WIRELESS,
    ID_CORSAIR_VOID_RGB_ELITE_WIRELESS,
    ID_CORSAIR_VOID_ELITE_USB,
    ID_CORSAIR_VOID_ELITE_SURROUND_USB_ADAPTER,
    ID_CORSAIR_HS70_WIRELESS,
    ID_CORSAIR_HS70_PRO,
    ID_CORSAIR_VOID_RGB_WIRELESS,
    ID_CORSAIR_VOID_ELITE_WIRELESS_PREMIUM_RGB,
    ID_CORSAIR_VOID_ELITE_USB_RGB,
};

static int void_send_sidetone(hid_device* device_handle, uint8_t num);
static BatteryInfo void_request_battery(hid_device* device_handle);
static int void_notification_sound(hid_device* device_handle, uint8_t soundid);
static int void_lights(hid_device* device_handle, uint8_t on);

void void_init(struct device** device)
{
    device_void.idVendor            = VENDOR_CORSAIR;
    device_void.idProductsSupported = PRODUCT_IDS;
    device_void.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_void.device_name, "Corsair Headset Device", sizeof(device_void.device_name));

    device_void.capabilities                               = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_NOTIFICATION_SOUND) | B(CAP_LIGHTS);
    device_void.capability_details[CAP_SIDETONE]           = (struct capability_detail) { .usagepage = 0xff00, .usageid = 0x1, .interface = 0 };
    device_void.capability_details[CAP_BATTERY_STATUS]     = (struct capability_detail) { .usagepage = 0xffc5, .usageid = 0x1, .interface = 3 };
    device_void.capability_details[CAP_NOTIFICATION_SOUND] = (struct capability_detail) { .usagepage = 0xffc5, .usageid = 0x1, .interface = 3 };
    device_void.capability_details[CAP_LIGHTS]             = (struct capability_detail) { .usagepage = 0xffc5, .usageid = 0x1, .interface = 3 };

    device_void.send_sidetone     = &void_send_sidetone;
    device_void.request_battery   = &void_request_battery;
    device_void.notifcation_sound = &void_notification_sound;
    device_void.switch_lights     = &void_lights;

    *device = &device_void;
}

static int void_send_sidetone(hid_device* device_handle, uint8_t num)
{
#define MSG_SIZE 64
    // the range of the void seems to be from 200 to 255
    num = map(num, 0, 128, 200, 255);

    unsigned char data[MSG_SIZE] = { 0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num, 0, 0, 0, 0 };

    for (int i = 16; i < MSG_SIZE; i++)
        data[i] = 0;

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

static BatteryInfo void_request_battery(hid_device* device_handle)
{
    // Packet Description
    // Answer of battery status
    // Index    0   1   2       3       4
    // Data     100 0   Loaded% 177     5 when loading, 0 when headset is not connected, 2 low battery, 1 otherwise

    int r;

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    // request battery status
    unsigned char data_request[2] = { 0xC9, 0x64 };

    r = hid_write(device_handle, data_request, 2);

    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    // read battery status
    unsigned char data_read[5];

    r = hid_read_timeout(device_handle, data_read, 5, hsc_device_timeout);

    if (r < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (r == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    if (data_read[4] == 0) {
        return info;
    }

    bool charging = false;
    if (data_read[4] == 4 || data_read[4] == 5) {
        charging = true;
    }

    if (data_read[4] == 1 || data_read[4] == 2 || charging) {
        info.status = BATTERY_AVAILABLE;

        if (charging) {
            info.status = BATTERY_CHARGING;
        }

        // Discard VOIDPRO_BATTERY_MICUP when it's set
        // see https://github.com/Sapd/HeadsetControl/issues/13
        if (data_read[2] & VOID_BATTERY_MICUP) {
            info.microphone_status = MICROPHONE_UP;
            info.level             = data_read[2] & ~VOID_BATTERY_MICUP;
            return info;
        }

        info.level = (int)data_read[2]; // battery status from 0 - 100
        return info;
    }

    return info;
}

static int void_notification_sound(hid_device* device_handle, uint8_t soundid)
{
    // soundid can be 0 or 1
    unsigned char data[5] = { 0xCA, 0x02, soundid };

    return hid_write(device_handle, data, 3);
}

static int void_lights(hid_device* device_handle, uint8_t on)
{
    unsigned char data[3] = { 0xC8, on ? 0x00 : 0x01, 0x00 };
    return hid_write(device_handle, data, 3);
}

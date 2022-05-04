#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <stdlib.h>
#include <string.h>

#define MSG_SIZE 64

static struct device device_arctis;

#define ID_ARCTIS_7_PLUS 0x220e

#define BATTERY_MAX 0x04
#define BATTERY_MIN 0x00

#define HEADSET_OFFLINE 0x01

static const uint16_t PRODUCT_IDS[] = { ID_ARCTIS_7_PLUS };

static int arctis_7_plus_send_sidetone(hid_device* device_handle, uint8_t num);
static int arctis_7_plus_send_inactive_time(hid_device* device_handle, uint8_t num);
static int arctis_7_plus_request_battery(hid_device* device_handle);

int arctis_7_plus_read_device_status(hid_device* device_handle, unsigned char* data_read);

void arctis_7_plus_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_arctis.device_name, "SteelSeries Arctis 7+", sizeof(device_arctis.device_name));

    device_arctis.capabilities                           = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    device_arctis.capability_details[CAP_SIDETONE]       = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_BATTERY_STATUS] = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]  = (struct capability_detail) { .usagepage = 0xffc0, .usageid = 0x1, .interface = 3 };

    device_arctis.send_sidetone      = &arctis_7_plus_send_sidetone;
    device_arctis.request_battery    = &arctis_7_plus_request_battery;
    device_arctis.send_inactive_time = &arctis_7_plus_send_inactive_time;

    *device = &device_arctis;
}

static int arctis_7_plus_send_sidetone(hid_device* device_handle, uint8_t num)
{
    // num, will be from 0 to 128, we need to map it to the correct value
    // the range of the Arctis 7+ is from 0 to 3
    num = map(num, 0, 128, 0, 3);

    uint8_t data[MSG_SIZE] = { 0x00, 0x39, num };

    return hid_write(device_handle, data, MSG_SIZE);
}

static int arctis_7_plus_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    // as the value is in minutes, mapping to a different range does not make too much sense here
    // the range of the Arctis 7+ is from 0 to 0x5A (90)

    uint8_t data[MSG_SIZE] = { 0x00, 0xa3, num };

    return hid_write(device_handle, data, MSG_SIZE);
}

static int arctis_7_plus_request_battery(hid_device* device_handle)
{
    // read device info
    unsigned char data_read[6];
    int r = arctis_7_plus_read_device_status(device_handle, data_read);

    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    if (data_read[1] == HEADSET_OFFLINE)
        return BATTERY_UNAVAILABLE;

    if (data_read[3] == 0x01)
        return BATTERY_CHARGING;

    int bat = data_read[2];

    if (bat > BATTERY_MAX)
        return 100;

    return map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
}

int arctis_7_plus_read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    unsigned char data_request[2] = { 0x00, 0xb0 };
    int r                         = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    return hid_read_timeout(device_handle, data_read, 6, hsc_device_timeout);
}

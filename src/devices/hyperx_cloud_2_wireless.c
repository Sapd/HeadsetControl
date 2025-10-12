#include "../device.h"
#include "../utility.h"
#include <hidapi.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VENDOR_HYPERX             0x03f0
#define ID_HYPERX_CLOUD2_WIRELESS 0x0696

#define REQUEST_SIZE  52
#define RESPONSE_SIZE 20
#define TIMEOUT       1000

static struct device device_hyperx_cloud2_wireless;

static const uint16_t PRODUCT_IDS[] = { ID_HYPERX_CLOUD2_WIRELESS };

static BatteryInfo hyperx_cloud2_wireless_request_battery(hid_device* device_handle);

void hyperx_cloud_2_wireless_init(struct device** device)
{
    device_hyperx_cloud2_wireless.idVendor            = VENDOR_HYPERX;
    device_hyperx_cloud2_wireless.idProductsSupported = PRODUCT_IDS;
    device_hyperx_cloud2_wireless.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    strncpy(device_hyperx_cloud2_wireless.device_name, "HyperX Cloud II Wireless", sizeof(device_hyperx_cloud2_wireless.device_name));

    device_hyperx_cloud2_wireless.capabilities                                     = B(CAP_BATTERY_STATUS);
    device_hyperx_cloud2_wireless.capability_details[CAP_BATTERY_STATUS].usagepage = 0xFF90;
    device_hyperx_cloud2_wireless.capability_details[CAP_BATTERY_STATUS].usageid   = 0x0303;
    device_hyperx_cloud2_wireless.request_battery                                  = &hyperx_cloud2_wireless_request_battery;

    *device = &device_hyperx_cloud2_wireless;
}

static BatteryInfo hyperx_cloud2_wireless_request_battery(hid_device* device_handle)
{

    int r                              = 0;
    uint8_t data_request[REQUEST_SIZE] = { 0x06, 0xff, 0xbb, 0x02, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000,
        0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000,
        0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000, 0000 };

    r = hid_write(device_handle, data_request, REQUEST_SIZE);
    if (r < 0) {
        BatteryInfo info = { .status = BATTERY_HIDERROR, .level = -1 };
        return info;
    }

    uint8_t data_read[RESPONSE_SIZE];
    r = hid_read_timeout(device_handle, data_read, RESPONSE_SIZE, TIMEOUT);
    if (r < 0) // read failed
    {
        BatteryInfo info = { .status = BATTERY_HIDERROR, .level = -1 };
        return info;
    } else if (r == 0) // read timeout
    {
        BatteryInfo info = { .status = BATTERY_TIMEOUT, .level = -1 };
        return info;
    }

    int batteryLevel = data_read[7];
    if (data_read[5] == 0x10) // charging
    {
        BatteryInfo info = { .status = BATTERY_CHARGING, .level = batteryLevel };
        return info;
    } else if (data_read[5] == 0x0f) // available
    {
        BatteryInfo info = { .status = BATTERY_AVAILABLE, .level = batteryLevel };
        return info;
    }

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };
    return info;
}

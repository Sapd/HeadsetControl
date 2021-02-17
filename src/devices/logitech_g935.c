#include "../device.h"
#include "logitech_g633_635_733_933_935.h"
#include <hidapi.h>

static struct device device_g935;

static const uint16_t PRODUCT_IDS[] = { ID_LOGITECH_G935 };

static int g935_send_sidetone(hid_device* device_handle, uint8_t num);
static int g935_request_battery(hid_device* device_handle);
static int g935_lights(hid_device* device_handle, uint8_t on);

void g935_init(struct device** device)
{
    g633_635_733_933_935_init(&device_g935, "Logitech G935 Gaming Headset");
    device_g935.idProductsSupported = PRODUCT_IDS;
    device_g935.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_g935.send_sidetone = &g935_send_sidetone;
    device_g935.request_battery = &g935_request_battery;
    device_g935.switch_lights = &g935_lights;

    *device = &device_g935;
}

static int g935_request_battery(hid_device* device_handle)
{
    return g633_635_733_933_935_request_battery(device_handle);
}

static int g935_send_sidetone(hid_device* device_handle, uint8_t num)
{
    return g633_635_733_933_935_send_sidetone(device_handle, num);
}

static int g935_lights(hid_device* device_handle, uint8_t on)
{
    return g633_635_733_933_935_lights(device_handle, on);
}

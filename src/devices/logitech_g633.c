#include "../device.h"
#include "logitech_g633_635_733_933_935.h"
#include <hidapi.h>

static struct device device_g633;

static const uint16_t PRODUCT_IDS[] = { ID_LOGITECH_G633 };

static int g633_send_sidetone(hid_device* device_handle, uint8_t num);
static int g633_request_battery(hid_device* device_handle);
static int g633_lights(hid_device* device_handle, uint8_t on);

void g633_init(struct device** device)
{
    g633_635_733_933_935_init(&device_g633, "Logitech G633 Gaming Headset");
    device_g633.idProductsSupported = PRODUCT_IDS;
    device_g633.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_g633.send_sidetone = &g633_send_sidetone;
    device_g633.request_battery = &g633_request_battery;
    device_g633.switch_lights = &g633_lights;

    *device = &device_g633;
}

static int g633_request_battery(hid_device* device_handle)
{
    return g633_635_733_933_935_request_battery(device_handle);
}

static int g633_send_sidetone(hid_device* device_handle, uint8_t num)
{
    return g633_635_733_933_935_send_sidetone(device_handle, num);
}

static int g633_lights(hid_device* device_handle, uint8_t on)
{
    return g633_635_733_933_935_lights(device_handle, on);
}

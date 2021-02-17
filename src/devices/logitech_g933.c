#include "../device.h"
#include "logitech_g633_635_733_933_935.h"
#include <hidapi.h>

static struct device device_g933;

static const uint16_t PRODUCT_IDS[] = { ID_LOGITECH_G933 };

static int g933_send_sidetone(hid_device* device_handle, uint8_t num);
static int g933_request_battery(hid_device* device_handle);
static int g933_lights(hid_device* device_handle, uint8_t on);

void g933_init(struct device** device)
{
    g633_635_733_933_935_init(&device_g933, "Logitech G933 Gaming Headset");
    device_g933.idProductsSupported = PRODUCT_IDS;
    device_g933.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_g933.send_sidetone = &g933_send_sidetone;
    device_g933.request_battery = &g933_request_battery;
    device_g933.switch_lights = &g933_lights;

    *device = &device_g933;
}

static int g933_request_battery(hid_device* device_handle)
{
    return g633_635_733_933_935_request_battery(device_handle);
}

static int g933_send_sidetone(hid_device* device_handle, uint8_t num)
{
    return g633_635_733_933_935_send_sidetone(device_handle, num);
}

static int g933_lights(hid_device* device_handle, uint8_t on)
{
    return g633_635_733_933_935_lights(device_handle, on);
}

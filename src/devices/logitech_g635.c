#include "../device.h"
#include "logitech_g633_635_733_933_935.h"
#include <hidapi.h>

static struct device device_g635;

static const uint16_t PRODUCT_IDS[] = { ID_LOGITECH_G635 };

static int g635_send_sidetone(hid_device* device_handle, uint8_t num);
static int g635_request_battery(hid_device* device_handle);
static int g635_lights(hid_device* device_handle, uint8_t on);

void g635_init(struct device** device)
{
    g633_635_733_933_935_init(&device_g635, "Logitech G635 Gaming Headset");
    device_g635.idProductsSupported = PRODUCT_IDS;
    device_g635.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_g635.send_sidetone = &g635_send_sidetone;
    device_g635.request_battery = &g635_request_battery;
    device_g635.switch_lights = &g635_lights;

    *device = &device_g635;
}

static int g635_request_battery(hid_device* device_handle)
{
    return g633_635_733_933_935_request_battery(device_handle);
}

static int g635_send_sidetone(hid_device* device_handle, uint8_t num)
{
    return g633_635_733_933_935_send_sidetone(device_handle, num);
}

static int g635_lights(hid_device* device_handle, uint8_t on)
{
    return g633_635_733_933_935_lights(device_handle, on);
}

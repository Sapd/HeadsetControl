#include "../device.h"
#include "logitech_g633_635_733_933_935.h"
#include <hidapi.h>

static struct device device_g733;

static const uint16_t PRODUCT_IDS[] = { ID_LOGITECH_G733 };

static int g733_send_sidetone(hid_device* device_handle, uint8_t num);
static int g733_request_battery(hid_device* device_handle);
static int g733_lights(hid_device* device_handle, uint8_t on);

void g733_init(struct device** device)
{
    g633_635_733_933_935_init(&device_g733, "Logitech G733 Gaming Headset");
    device_g733.idProductsSupported = PRODUCT_IDS;
    device_g733.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_g733.send_sidetone = &g733_send_sidetone;
    device_g733.request_battery = &g733_request_battery;
    device_g733.switch_lights = &g733_lights;

    *device = &device_g733;
}

static int g733_request_battery(hid_device* device_handle)
{
    return g633_635_733_933_935_request_battery(device_handle);
}

static int g733_send_sidetone(hid_device* device_handle, uint8_t num)
{
    return g633_635_733_933_935_send_sidetone(device_handle, num);
}

static int g733_lights(hid_device* device_handle, uint8_t on)
{
    return g633_635_733_933_935_lights(device_handle, on);
}

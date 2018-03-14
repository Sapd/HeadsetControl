#include "device_registry.h"

#include "devices/corsair_void.h"
#include "devices/logitech_g430.h"
#include "devices/logitech_g533.h"
#include "devices/logitech_g930.h"

#include <string.h>


#define NUMDEVICES 4
// array of pointers to device
static struct device *(devicelist[NUMDEVICES]);

void init_devices()
{
    void_init(&devicelist[0]);
    g430_init(&devicelist[1]);
    g533_init(&devicelist[2]);
    g930_init(&devicelist[3]);
}

int get_device(struct device* device_found, uint16_t idVendor, uint16_t idProduct)
{
    for (int i = 0; i < NUMDEVICES; i++)
    {
        if (devicelist[i]->idVendor == idVendor && devicelist[i]->idProduct == idProduct)
        {
            // copy struct, compiler won't do it for us
            device_found->capabilities = devicelist[i]->capabilities;
            strcpy(device_found->device_name, devicelist[i]->device_name);
            device_found->idProduct = devicelist[i]->idProduct;
            device_found->idVendor = devicelist[i]->idVendor;
            device_found->send_sidetone = devicelist[i]->send_sidetone;
            device_found->request_battery = devicelist[i]->request_battery;
            return 0;
        }
    }
    return 1;
}

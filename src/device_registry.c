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
            // copy struct to the destination in device_found
            memcpy(device_found, devicelist[i], sizeof(struct device));
            return 0;
        }
    }
    return 1;
}

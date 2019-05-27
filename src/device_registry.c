#include "device_registry.h"

#include "devices/corsair_void.h"
#include "devices/corsair_voidpro.h"
#include "devices/corsair_voidpro_wireless.h"
#include "devices/logitech_g430.h"
#include "devices/logitech_g533.h"
#include "devices/logitech_g633.h"
#include "devices/logitech_g930.h"
#include "devices/steelseries_arctis7.h"
#include "devices/steelseries_arctispro_2019.h"

#include <string.h>


#define NUMDEVICES 9
// array of pointers to device
static struct device *(devicelist[NUMDEVICES]);

void init_devices()
{
    void_init(&devicelist[0]);
    voidpro_init(&devicelist[1]);
    voidpro_wireless_init(&devicelist[2]);
    g430_init(&devicelist[3]);
    g533_init(&devicelist[4]);
    g633_init(&devicelist[5]);
    g930_init(&devicelist[6]);
    arctis7_init(&devicelist[7]);
    arctispro_2019_init(&devicelist[8]);
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

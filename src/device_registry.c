#include "device_registry.h"

#include "devices/corsair_void.h"
#include "devices/logitech_g430.h"
#include "devices/logitech_g533.h"
#include "devices/logitech_g633_g933_935.h"
#include "devices/logitech_g930.h"
#include "devices/logitech_gpro.h"
#include "devices/steelseries_arctis_1.h"
#include "devices/steelseries_arctis_1_xbox.h"
#include "devices/steelseries_arctis_7.h"
#include "devices/steelseries_arctis_9.h"

#include <string.h>

#define NUMDEVICES 10
// array of pointers to device
static struct device*(devicelist[NUMDEVICES]);

void init_devices()
{
    void_init(&devicelist[0]);
    g430_init(&devicelist[1]);
    g533_init(&devicelist[2]);
    g930_init(&devicelist[3]);
    g933_935_init(&devicelist[4]);
    arctis_1_init(&devicelist[5]);
    arctis_7_init(&devicelist[6]);
    arctis_9_init(&devicelist[7]);
    gpro_init(&devicelist[8]);
    arctis_1_xbox_init(&devicelist[9]);
}

int get_device(struct device* device_found, uint16_t idVendor, uint16_t idProduct)
{
    // search for an implementation supporting one of the vendor+productid combination
    for (int i = 0; i < NUMDEVICES; i++) {
        if (devicelist[i]->idVendor == idVendor) {
            // one device file can contain multiple product ids, iterate them
            for (int y = 0; y < devicelist[i]->numIdProducts; y++) {
                if (devicelist[i]->idProductsSupported[y] == idProduct) {
                    // Set the actual found productid (of the set of available ones for this device file/struct)
                    devicelist[i]->idProduct = idProduct;
                    // copy struct to the destination in device_found
                    memcpy(device_found, devicelist[i], sizeof(struct device));
                    return 0;
                }
            }
        }
    }
    return 1;
}

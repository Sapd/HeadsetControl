#include "device_registry.h"

#include "devices/corsair_void.h"
#include "devices/headsetcontrol_test.h"
#include "devices/hyperx_calphaw.h"
#include "devices/hyperx_cflight.h"
#include "devices/hyperx_cloud_3.h"
#include "devices/logitech_g430.h"
#include "devices/logitech_g432.h"
#include "devices/logitech_g533.h"
#include "devices/logitech_g535.h"
#include "devices/logitech_g633_g933_935.h"
#include "devices/logitech_g930.h"
#include "devices/logitech_gpro.h"
#include "devices/logitech_gpro_x2.h"
#include "devices/logitech_zone_wired.h"
#include "devices/roccat_elo_7_1_air.h"
#include "devices/roccat_elo_7_1_usb.h"
#include "devices/steelseries_arctis_1.h"
#include "devices/steelseries_arctis_7.h"
#include "devices/steelseries_arctis_7_plus.h"
#include "devices/steelseries_arctis_9.h"
#include "devices/steelseries_arctis_nova_3.h"
#include "devices/steelseries_arctis_nova_5.h"
#include "devices/steelseries_arctis_nova_7.h"
#include "devices/steelseries_arctis_nova_pro_wireless.h"
#include "devices/steelseries_arctis_pro_wireless.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pointer to an array of pointers to device
static struct device** devicelist = NULL;
int num_devices                   = 0;

void add_device(void (*init_func)(struct device**));

void init_devices()
{
    // Corsair
    add_device(void_init);
    // HyperX
    add_device(calphaw_init);
    add_device(cflight_init);
    add_device(hyperx_cloud3_init);
    // Logitech
    add_device(g430_init);
    add_device(g432_init);
    add_device(g533_init);
    add_device(g535_init);
    add_device(g930_init);
    add_device(g933_935_init);
    add_device(gpro_init);
    add_device(gpro_x2_init);
    add_device(zone_wired_init);
    // SteelSeries
    add_device(arctis_1_init);
    add_device(arctis_7_init);
    add_device(arctis_9_init);
    add_device(arctis_pro_wireless_init);
    // Roccat
    add_device(elo71Air_init);
    add_device(elo71USB_init);
    // SteelSeries
    add_device(arctis_nova_3_init);
    add_device(arctis_nova_5_init);
    add_device(arctis_nova_7_init);
    add_device(arctis_7_plus_init);
    add_device(arctis_nova_pro_wireless_init);

    add_device(headsetcontrol_test_init);
}

void add_device(void (*init_func)(struct device**))
{
    // Reallocate memory to accommodate the new device
    struct device** temp = realloc(devicelist, (num_devices + 1) * sizeof(struct device*));
    if (temp == NULL) {
        fprintf(stderr, "Failed to allocate memory for device list.\n");
        abort();
    }
    devicelist = temp;

    // Initialize the new device
    init_func(&devicelist[num_devices]);
    num_devices++;
}

int get_device(struct device* device_found, uint16_t idVendor, uint16_t idProduct)
{
    assert(num_devices > 0);

    // search for an implementation supporting one of the vendor+productid combination
    for (int i = 0; i < num_devices; i++) {
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

int iterate_devices(int index, struct device** device_found)
{
    assert(num_devices > 0);

    if (index < num_devices) {
        *device_found = devicelist[index];
        return 0;
    } else {
        return -1;
    }
}

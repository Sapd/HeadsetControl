#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <string.h>

enum voidpro_battery_flags {
     VOIDPRO_BATTERY_MICUP = 128
};

static struct device device_voidpro;

static int voidpro_send_sidetone(hid_device *device_handle, uint8_t num);
static int voidpro_request_battery(hid_device *device_handle);
static int voidpro_notification_sound(hid_device *hid_device, uint8_t soundid);
static int voidpro_lights(hid_device* device_handle, uint8_t on);

void voidpro_init(struct device** device)
{
    device_voidpro.idVendor = VENDOR_CORSAIR;
    device_voidpro.idProduct = 0x0a14;
    
    strcpy(device_voidpro.device_name, "Corsair Void Pro");
    
    device_voidpro.capabilities = CAP_SIDETONE | CAP_BATTERY_STATUS | CAP_NOTIFICATION_SOUND | CAP_LIGHTS;
    device_voidpro.send_sidetone = &voidpro_send_sidetone;
    device_voidpro.request_battery = &voidpro_request_battery;
    device_voidpro.notifcation_sound = &voidpro_notification_sound;
    device_voidpro.switch_lights = &voidpro_lights;
    
    *device = &device_voidpro;
}

static int voidpro_send_sidetone(hid_device *device_handle, uint8_t num)
{
    // the range of the voidpro seems to be from 200 to 255
    num = map(num, 0, 128, 200, 255);

    unsigned char data[12] = {0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num};

    return hid_send_feature_report(device_handle, data, 12);
}

static int voidpro_request_battery(hid_device *device_handle)
{
    // Packet Description
    // Answer of battery status
    // Index    0   1   2       3       4
    // Data     100 0   Loaded% 177     5 when loading, 0 when loading and off, 1 otherwise
    //
    // Loaded% has bitflag VOIDPRO_BATTERY_MICUP set when mic is in upper position
    
    int r = 0;
    
    // request battery status
    unsigned char data_request[2] = {0xC9, 0x64};
    
    r = hid_write(device_handle, data_request, 2);
    
    if (r < 0) return r;
    
    // read battery status
    unsigned char data_read[5];
    
    r = hid_read(device_handle, data_read, 5);
 
    if (r < 0) return r;

    if (data_read[4] == 0 || data_read[4] == 4 || data_read[4] == 5)
    {
        return BATTERY_LOADING;
    }
    else if (data_read[4] == 1)
    {
        // Discard VOIDPRO_BATTERY_MICUP when it's set
        // see https://github.com/Sapd/HeadsetControl/issues/13
        if (data_read[2] & VOIDPRO_BATTERY_MICUP)
            return data_read[2] &~ VOIDPRO_BATTERY_MICUP;
        else
            return (int)data_read[2]; // battery status from 0 - 100
    }
    else
    {
        return -100;
    }
}

static int voidpro_notification_sound(hid_device* device_handle, uint8_t soundid)
{
    // soundid can be 0 or 1
    unsigned char data[5] = {0xCA, 0x02, soundid};
    
    return hid_write(device_handle, data, 3);
}

static int voidpro_lights(hid_device* device_handle, uint8_t on)
{
    unsigned char data[3] = {0xC8, on ? 0x00 : 0x01, 0x00};
    return hid_write(device_handle, data, 3);
}

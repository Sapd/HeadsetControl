#include "../device.h"
#include "../utility.h"

#include <hidapi.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct device device_void;

// import sideetone loudness, so we can set it if user set it; for the daemon
extern long sidetone_loudness;

enum void_wireless_battery_flags {
    VOID_BATTERY_MICUP = 128
};

#define ID_CORSAIR_VOID_WIRELESS     0x1b27
#define ID_CORSAIR_VOID_PRO          0x0a14
#define ID_CORSAIR_VOID_PRO_R2       0x0a16
#define ID_CORSAIR_VOID_PRO_USB      0x0a17
#define ID_CORSAIR_VOID_PRO_WIRELESS 0x0a1a
#define ID_CORSAIR_VOID_RGB_USB      0x1b2a
#define ID_CORSAIR_VOID_RGB_USB_2    0x1b23
#define ID_CORSAIR_VOID_RGB_WIRED    0x1b1c

static const uint16_t PRODUCT_IDS[] = { ID_CORSAIR_VOID_RGB_WIRED, ID_CORSAIR_VOID_WIRELESS, ID_CORSAIR_VOID_PRO,
    ID_CORSAIR_VOID_PRO_R2, ID_CORSAIR_VOID_PRO_USB, ID_CORSAIR_VOID_PRO_WIRELESS,
    ID_CORSAIR_VOID_RGB_USB, ID_CORSAIR_VOID_RGB_USB_2 };

static int void_send_sidetone(hid_device* device_handle, uint8_t num);
static int void_request_battery(hid_device* device_handle);
static int void_notification_sound(hid_device* device_handle, uint8_t soundid);
static int void_lights(hid_device* device_handle, uint8_t on);
static int void_daemonize(hid_device* device_handle);

void void_init(struct device** device)
{
    device_void.idVendor = VENDOR_CORSAIR;
    device_void.idProductsSupported = PRODUCT_IDS;
    device_void.numIdProducts = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_void.idUsagePage = 0xff00;
    device_void.idUsage = 0x1;

    strncpy(device_void.device_name, "Corsair Void (Pro)", sizeof(device_void.device_name));

    device_void.capabilities = CAP_SIDETONE | CAP_BATTERY_STATUS | CAP_NOTIFICATION_SOUND | CAP_LIGHTS | CAP_DAEMONIZE;
    device_void.send_sidetone = &void_send_sidetone;
    device_void.request_battery = &void_request_battery;
    device_void.notifcation_sound = &void_notification_sound;
    device_void.switch_lights = &void_lights;
    device_void.daemonize = &void_daemonize;

    *device = &device_void;
}

static int void_send_sidetone(hid_device* device_handle, uint8_t num)
{
#define MSG_SIZE 64
    // the range of the void seems to be from 200 to 255
    num = map(num, 0, 128, 200, 255);

    unsigned char data[MSG_SIZE] = { 0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num, 0, 0, 0, 0 };

    for (int i = 16; i < MSG_SIZE; i++)
        data[i] = 0;

    return hid_send_feature_report(device_handle, data, MSG_SIZE);
}

static int void_request_battery(hid_device* device_handle)
{
    // Notice: For Windows it has a different usage page (0xffc5), multiple usage pages are not supported currently by headsetcontrol

    // Packet Description (see also the new description below at the daemon section)
    // Answer of battery status
    // Index    0   1   2       3       4
    // Data     100 0   Loaded% 177     5 when loading, 1 otherwise

    int r = 0;

    // request battery status
    unsigned char data_request[2] = { 0xC9, 0x64 };

    r = hid_write(device_handle, data_request, 2);

    if (r < 0)
        return r;

    // read battery status
    unsigned char data_read[5];

    r = hid_read(device_handle, data_read, 5);

    if (r < 0)
        return r;

    if (data_read[4] == 0 || data_read[4] == 4 || data_read[4] == 5) {
        return BATTERY_CHARGING;
    }

    if (data_read[4] == 1) {
        // Discard VOIDPRO_BATTERY_MICUP when it's set
        // see https://github.com/Sapd/HeadsetControl/issues/13
        if (data_read[2] & VOID_BATTERY_MICUP) {
            return data_read[2] & ~VOID_BATTERY_MICUP;
        }

        return (int)data_read[2]; // battery status from 0 - 100
    }

    return HSC_ERROR;
}

static int void_notification_sound(hid_device* device_handle, uint8_t soundid)
{
    // soundid can be 0 or 1
    unsigned char data[5] = { 0xCA, 0x02, soundid };

    return hid_write(device_handle, data, 3);
}

static int void_lights(hid_device* device_handle, uint8_t on)
{
    printf("We support only turning off light via the -d option right now\n");
    return 0;
}

// --------------------------------------------------------------------------
// Daemon functionality for corsair void
// --------------------------------------------------------------------------
// on dann off
static int void_mute(hid_device* device_handle, bool mute)
{
    // Packet similar to sidetone
    unsigned char init[] = "\xff\x0b\x00\xff\x04\x0e\x01\x05\x01\x04\x00\x00\x00\x00\x00\x00"
                           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
                           "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

    if (mute) {
        // For some reason - when muting iCue writes the sidetone value into the data - but not when unmuting
        int sidetone_number = (sidetone_loudness == -1) ? 0 : sidetone_loudness;
        sidetone_number = map(sidetone_number, 0, 128, 200, 255);
        init[11] = sidetone_number;

        hid_send_feature_report(device_handle, init, sizeof(init));

        unsigned char init2[] = "\xca\x04\x00\x00\x00";
        hid_write(device_handle, init2, sizeof(init2));

        // This will make an additional notification sound
        /*unsigned char init3[] = "\xca\x05\x01\x00\x00";
        hid_write(device_handle, init3, sizeof(init3));*/

        unsigned char init4[] = "\xca\x04\x01\x00\x00";
        hid_write(device_handle, init4, sizeof(init4));

        // This does the muting
        unsigned char mutedata[] = "\xca\x03\x01\x00\x00";
        hid_write(device_handle, mutedata, sizeof(mutedata));
    } else {
        hid_send_feature_report(device_handle, init, sizeof(init));

        unsigned char init2[] = "\xca\x04\x00\x00\x00";
        hid_write(device_handle, init2, sizeof(init2));

        unsigned char init3[] = "\xca\x05\x00\x00\x00";
        hid_write(device_handle, init3, sizeof(init2));

        unsigned char init4[] = "\xca\x04\x01\x00\x00";
        hid_write(device_handle, init4, sizeof(init4));

        // unmuting
        unsigned char mutedata[] = "\xca\x03\x00\x00\x00";
        hid_write(device_handle, mutedata, sizeof(mutedata));

        // Not sure why we need to wait a bit and send sidetone again - but otherwise it seems to bug
        // iCue also sends sidetone again after some time
        usleep(1000 * 150);
        if (void_send_sidetone(device_handle, (sidetone_loudness == -1) ? 0 : sidetone_loudness) < 0)
            printf("error\n");
    }

    return 0;
}

// 0 index of data
enum VOID_PACKET_REPORTS {
    // E.g. Triggers when a Button is pressed, when in hardware mode
    VOID_HARDWRE_UPDATE = 0x01,
    // Different status updates, like battery, and buttons (buttons when in software mode)
    VOID_STATUS_UPDATE = 0x64
};

// 1 index
enum VOID_PACKET_BUTTONS {
    VOID_NO_BUTTON = 0,
    VOID_BUTTON_MUTE = 0x02,
    VOID_BUTTON_ONOFF = 0x80
};

// 2 index seems to Loading percentage or 0

// 3 index; Seems to be Status
enum VOID_STATUS_TYPE {
    VOID_STATUS_BATTERY_INFO = 0xb1, // Connected, Battery info potentially shown in 2nd index
    VOID_STATUS_UNKNOWN = 0xb3,
    VOID_STATUS_DISCONNECTED = 0x33 // Either by powering off or out of range
};

// 4 index some kind of flag
enum VOID_STATUS_FLAG {
    VOID_FLAG_ON_BATTERY = 0x01,
    VOID_FLAG_LOADING = 0x05
};

enum VOID_HEADSETCONTROL_STATUS {
    VOID_HSC_CONNECTED = 0,
    VOID_HSC_DISCONNECTED = 1
};

static int connection_status = VOID_HSC_CONNECTED;
static int last_connection_status = VOID_HSC_CONNECTED;

static int current_flag = 0;

static int last_battery = 0;

static int void_handle_status(hid_device* device_handle, int void_status_type, int batteryprcnt, int flag)
{
    // flag changed
    if (current_flag != flag) {
        current_flag = flag;
        switch (flag) {
        case VOID_FLAG_ON_BATTERY:
            printf("On Battery\n");
            break;
        case VOID_FLAG_LOADING:
            printf("Loading\n");
            break;
        default:
            if (debug_info)
                printf("Unknown status flag %d", flag);
        }
    }

    switch (void_status_type) {
    case VOID_STATUS_BATTERY_INFO: {
        connection_status = VOID_HSC_CONNECTED;
        if (batteryprcnt == 0)
            break;

        // Battery changed
        if (batteryprcnt != last_battery) {
            last_battery = batteryprcnt;
            printf("Battery: %d\n", batteryprcnt);
        }
        break;
    }
    case VOID_STATUS_DISCONNECTED:
        connection_status = VOID_HSC_DISCONNECTED;
        break;
    default:
        break;
    }

    if (last_connection_status != connection_status) {
        last_connection_status = connection_status;

        switch (connection_status) {
        case VOID_HSC_CONNECTED:
            printf("Connected!\n");
            break;
        case VOID_HSC_DISCONNECTED:
            printf("Disconnected or connection lost\n");
            break;
        }
    }

    return 0;
}

static bool mic_muted = true;

static int void_handle_button(hid_device* device_handle, int buttonid)
{
    switch (buttonid) {
    case VOID_BUTTON_MUTE:
        if (debug_info)
            printf("Button mute\n");

        void_mute(device_handle, !mic_muted);
        mic_muted = !mic_muted;

        if (mic_muted)
            printf("muted\n");
        else
            printf("unmuted\n");
        break;

    case VOID_BUTTON_ONOFF:
        printf("Button onoff\n");
        break;
    default:
        if (debug_info)
            printf("Button unknown id %d\n", buttonid);
    }

    return 0;
}

static int void_handle_packet(hid_device* device_handle, unsigned char* data, int length)
{
    if (data[0] == VOID_STATUS_UPDATE) {
        // check if button pressed
        if (data[1] != VOID_NO_BUTTON) {
            return void_handle_button(device_handle, data[1]);
        }

        return void_handle_status(device_handle, data[3], data[2], data[4]);
    }

    return 0;
}

static int void_software_mode(hid_device* device_handle, bool on)
{
    unsigned char activate[3] = { 0xC8, on ? 0x01 : 0x00, 0x00 };

    return hid_write(device_handle, activate, 3);
}

// 64 00 x b0 00 Connection problem

volatile sig_atomic_t keepRunning = true;
static hid_device* device_pointer = NULL;

void interruptHandler(int signal_number)
{
    keepRunning = false;

    // We need to close HID here - or we can't exit the loop
    // as hid_read will block
    if (void_software_mode(device_pointer, false) == -1) {
        fprintf(stderr, "Failed deactivating void software mode\n");
        exit(1);
    }

    hid_close(device_pointer);
    exit(0);
}

static int void_daemonize(hid_device* device_handle)
{
    if (void_software_mode(device_handle, true) == -1) {
        fprintf(stderr, "Failed activating void software mode\n");
        return -1;
    }

    device_pointer = device_handle;

    // Initialize signal handler for CTRL + C
    struct sigaction act;
    act.sa_handler = interruptHandler;
    sigaction(SIGINT, &act, NULL);

    const size_t readlength = 16;

    unsigned char* readbuffer = calloc(readlength, sizeof(char));

    // Initialize mute function
    void_mute(device_handle, mic_muted);

    while (keepRunning) {
        hid_read(device_handle, readbuffer, readlength);

        if (debug_info) {
            for (int i = 0; i < readlength; i++) {
                printf("\\%02hhx, ", readbuffer[i]);
            }
            printf("\n");
        }

        void_handle_packet(device_handle, readbuffer, readlength);
    }

    free(readbuffer);

    if (void_software_mode(device_handle, false) == -1) {
        fprintf(stderr, "Failed deactivating void software mode\n");
        return -1;
    }

    return 0;
}

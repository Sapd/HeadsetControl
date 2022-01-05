#include "../device.h"
#include "../utility.h"
#include "logitech.h"

#include <stdio.h>
#include <unistd.h>

#include <math.h>
#include <string.h>

#define MSG_SIZE 20

static struct device device_gpro;

#define ID_LOGITECH_PRO     0x0aa7
#define ID_LOGITECH_PRO_X_0 0x0aaa
#define ID_LOGITECH_PRO_X_1 0x0aba

static const uint16_t PRODUCT_IDS[] = {
    ID_LOGITECH_PRO,
    ID_LOGITECH_PRO_X_0,
    ID_LOGITECH_PRO_X_1,
};

static int gpro_send_sidetone(hid_device* device_handle, uint8_t num);
static int gpro_request_battery(hid_device* device_handle);
static int gpro_send_inactive_time(hid_device* device_handle, uint8_t num);

void gpro_init(struct device** device)
{
    device_gpro.idVendor            = VENDOR_LOGITECH;
    device_gpro.idProductsSupported = PRODUCT_IDS;
    device_gpro.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);

    strncpy(device_gpro.device_name, "Logitech G PRO Series", sizeof(device_gpro.device_name));

    device_gpro.capabilities    = B(CAP_SIDETONE | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME));
    device_gpro.send_sidetone   = &gpro_send_sidetone;
    device_gpro.request_battery = &gpro_request_battery;
    device_gpro.send_inactive_time = &gpro_send_inactive_time;

    *device = &device_gpro;
}

static int gpro_send_sidetone(hid_device* device_handle, uint8_t num)
{
    num = map(num, 0, 128, 0, 100);

    uint8_t sidetone_data[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x05, 0x1a, num };

    return hid_write(device_handle, sidetone_data, sizeof(sidetone_data) / sizeof(sidetone_data[0]));
}

#ifdef DEBUG
static void printhexdump(uint8_t* buf, int len)
{
    printf("┌────────┬─────────────────────────────────────────────────┬──────────────────┐\n");
    for (int offset = 0; offset < MSG_SIZE; offset += 16) {
        printf("│ \e[1m%06X\e[m │", offset);
        printf("\e[36m");
        for (int i = offset; i < offset + 16; i++) {
            if (i < MSG_SIZE) {
                printf(" %02X", buf[i]);
            } else {
                printf("   ");
            }
        }
        printf("\e[m");
        printf(" │ ");
        printf("\e[33m");
        for (int i = offset; i < offset + 16; i++) {
            if (buf[i] > 31 && buf[i] < 127) {
                printf("%c", buf[i]);
            } else {
                printf(".");
            }
        }
        printf("\e[m");
        printf(" │\n");
    }
    printf("└────────┴─────────────────────────────────────────────────┴──────────────────┘\n");
}
#endif /* DEBUG */

static float estimate_battery_level(uint16_t voltage)
{
    return voltage * 100.0 / 0x0fff;
}

static int gpro_request_battery(hid_device* device_handle)
{
    /*
        CREDIT GOES TO https://github.com/ashkitten/ for the project
        https://github.com/ashkitten/g933-utils/
        I've simply ported that implementation to this project!
    */

    int r = 0;
    // request battery voltage
    uint8_t buf[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x06, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    r = hid_write(device_handle, buf, sizeof(buf) / sizeof(buf[0]));
    if (r < 0)
        return r;

    r = hid_read_timeout(device_handle, buf, HIDPP_LONG_MESSAGE_LENGTH, hsc_device_timeout);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

#ifdef DEBUG
    printhexdump(buf, HIDPP_LONG_MESSAGE_LENGTH);
#endif /* DEBUG */

    const uint16_t voltage = (buf[4] << 8) | buf[5];
    return (int)(roundf(estimate_battery_level(voltage)));
}

static int gpro_send_inactive_time(hid_device* device_handle, uint8_t num)
{
    int r = 0;
    // request new inactivity timeout
    uint8_t buf[HIDPP_LONG_MESSAGE_LENGTH] = { HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x06, 0x2d, num, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    r = hid_write(device_handle, buf, sizeof(buf) / sizeof(buf[0]));
    if (r < 0)
        return r;

    r = hid_read_timeout(device_handle, buf, HIDPP_LONG_MESSAGE_LENGTH, hsc_device_timeout);
    if (r < 0)
        return r;

    if (r == 0)
        return HSC_READ_TIMEOUT;

    return r;
}

#include "../device.h"
#include "../utility.h"

#include <stdio.h>
#include <string.h>

static struct device device_arctis;

enum { ID_ARCTIS_NOVA_PRO_WIRELESS_BASE_STATION = 0x12e0,
    ID_ARCTIS_NOVA_PRO_WIRELESS_X_BASE_STATION  = 0x12e5 };

enum {
    MSG_SIZE        = 31,
    STATUS_BUF_SIZE = 128,
};

enum {
    BATTERY_MIN = 0x00,
    BATTERY_MAX = 0x08,
};

enum headset_status {
    HEADSET_OFFLINE        = 0x01,
    HEADSET_CABLE_CHARGING = 0x02,
    HEADSET_ONLINE         = 0x08,
};

enum inactive_time {
    INACTIVE_TIME_DISABLED   = 0,
    INACTIVE_TIME_1_MINUTES  = 1,
    INACTIVE_TIME_5_MINUTES  = 2,
    INACTIVE_TIME_10_MINUTES = 3,
    INACTIVE_TIME_15_MINUTES = 4,
    INACTIVE_TIME_30_MINUTES = 5,
    INACTIVE_TIME_60_MINUTES = 6,
};

enum mic_mute_led_brightness {
    LED_MIN = 1,
    LED_MAX = 10,
};

enum {
    EQUALIZER_PRESET_CUSTOM = 4,
    EQUALIZER_BANDS_SIZE    = 10,
    EQUALIZER_BASELINE      = 0x14,
    EQUALIZER_BAND_MIN      = -10,
    EQUALIZER_BAND_MAX      = +10,
};

static const uint16_t PRODUCT_IDS[] = { ID_ARCTIS_NOVA_PRO_WIRELESS_BASE_STATION, ID_ARCTIS_NOVA_PRO_WIRELESS_X_BASE_STATION };
static EqualizerInfo EQUALIZER      = { EQUALIZER_BANDS_SIZE, 0, 0.5, EQUALIZER_BAND_MIN, EQUALIZER_BAND_MAX };

static int set_sidetone(hid_device* device_handle, uint8_t num);
static BatteryInfo get_battery(hid_device* device_handle);
static int set_lights(hid_device* device_handle, uint8_t on);
static int set_inactive_time(hid_device* device_handle, uint8_t minutes);
static int set_equalizer_preset(hid_device* device_handle, uint8_t num);
static int set_equalizer(hid_device* device_handle, struct equalizer_settings* settings);

static int read_device_status(hid_device* device_handle, unsigned char* data_read);
static int save_state(hid_device* device_handle);

void arctis_nova_pro_wireless_init(struct device** device)
{
    device_arctis.idVendor            = VENDOR_STEELSERIES;
    device_arctis.idProductsSupported = PRODUCT_IDS;
    device_arctis.numIdProducts       = sizeof(PRODUCT_IDS) / sizeof(PRODUCT_IDS[0]);
    device_arctis.equalizer           = &EQUALIZER;

    strncpy(device_arctis.device_name, "SteelSeries Arctis Nova Pro Wireless", sizeof(device_arctis.device_name));

    device_arctis.capabilities = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS)
        | B(CAP_INACTIVE_TIME) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET);

    device_arctis.capability_details[CAP_SIDETONE]         = (struct capability_detail) { .interface = 4 };
    device_arctis.capability_details[CAP_BATTERY_STATUS]   = (struct capability_detail) { .interface = 4 };
    device_arctis.capability_details[CAP_LIGHTS]           = (struct capability_detail) { .interface = 4 };
    device_arctis.capability_details[CAP_INACTIVE_TIME]    = (struct capability_detail) { .interface = 4 };
    device_arctis.capability_details[CAP_EQUALIZER]        = (struct capability_detail) { .interface = 4 };
    device_arctis.capability_details[CAP_EQUALIZER_PRESET] = (struct capability_detail) { .interface = 4 };

    device_arctis.send_sidetone         = &set_sidetone;
    device_arctis.request_battery       = &get_battery;
    device_arctis.switch_lights         = &set_lights;
    device_arctis.send_inactive_time    = &set_inactive_time;
    device_arctis.send_equalizer_preset = &set_equalizer_preset;
    device_arctis.send_equalizer        = &set_equalizer;

    *device = &device_arctis;
}

static int set_sidetone(hid_device* device_handle, uint8_t num)
{
    if (num > 3) {
        fprintf(stderr, "Device only supports 0-3 range for sidetone (off, low, med, high).\n");
        return HSC_OUT_OF_BOUNDS;
    }

    const unsigned char data_request[MSG_SIZE] = { 0x06, 0x39, num };

    int res = hid_write(device_handle, data_request, MSG_SIZE);
    if (res < 0)
        return res;

    return save_state(device_handle);
}

static BatteryInfo get_battery(hid_device* device_handle)
{
    // read device info
    unsigned char data_read[STATUS_BUF_SIZE];
    int res = read_device_status(device_handle, data_read);

    BatteryInfo info = { .status = BATTERY_UNAVAILABLE, .level = -1 };

    if (res < 0) {
        info.status = BATTERY_HIDERROR;
        return info;
    }

    if (res == 0) {
        info.status = BATTERY_TIMEOUT;
        return info;
    }

    if (res < 16)
        return info;

    uint8_t status = data_read[15];
    switch (status) {
    case HEADSET_OFFLINE:
        return info;
    case HEADSET_CABLE_CHARGING:
        info.status = BATTERY_CHARGING;
        break;
    case HEADSET_ONLINE:
        info.status = BATTERY_AVAILABLE;
        break;
    default:
        fprintf(stderr, "Unknown headset status 0x%.2x.\n", status);
        return info;
    }

    int bat = data_read[6];

    info.level = map(bat, BATTERY_MIN, BATTERY_MAX, 0, 100);
    return info;
}

static int set_lights(hid_device* device_handle, uint8_t on)
{
    uint8_t led_strength   = map(on, 0, 1, LED_MIN, LED_MAX);
    uint8_t data[MSG_SIZE] = { 0x06, 0xbf, led_strength };

    int res = hid_write(device_handle, data, MSG_SIZE);
    if (res < 0)
        return res;

    return save_state(device_handle);
}

static int set_inactive_time(hid_device* device_handle, uint8_t minutes)
{
    // Cannot set minutes directly, round to nearest
    uint8_t num = INACTIVE_TIME_DISABLED;
    if (minutes >= 45) {
        num = INACTIVE_TIME_60_MINUTES;
    } else if (minutes >= 23) {
        num = INACTIVE_TIME_30_MINUTES;
    } else if (minutes >= 13) {
        num = INACTIVE_TIME_15_MINUTES;
    } else if (minutes >= 8) {
        num = INACTIVE_TIME_10_MINUTES;
    } else if (minutes >= 3) {
        num = INACTIVE_TIME_5_MINUTES;
    } else if (minutes > 0) {
        num = INACTIVE_TIME_1_MINUTES;
    }

    uint8_t data[MSG_SIZE] = { 0x06, 0xc1, num };

    int res = hid_write(device_handle, data, MSG_SIZE);
    if (res < 0)
        return res;

    return save_state(device_handle);
}

static int set_equalizer_preset(hid_device* device_handle, uint8_t num)
{
    uint8_t data[MSG_SIZE] = { 0x06, 0x2e, num };

    int res = hid_write(device_handle, data, MSG_SIZE);
    if (res < 0)
        return res;

    return save_state(device_handle);
}

static int set_equalizer(hid_device* device_handle, struct equalizer_settings* settings)
{
    if (settings->size != EQUALIZER_BANDS_SIZE) {
        fprintf(stderr, "Device only supports %d bands.\n", EQUALIZER_BANDS_SIZE);
        return HSC_OUT_OF_BOUNDS;
    }

    set_equalizer_preset(device_handle, EQUALIZER_PRESET_CUSTOM);

    uint8_t data[MSG_SIZE] = { 0x06, 0x33 };
    for (int i = 0; i < settings->size; i++) {
        float band_value = settings->bands_values[i];
        if (band_value < EQUALIZER_BAND_MIN || band_value > EQUALIZER_BAND_MAX) {
            printf("Device only supports bands ranging from %d to %d.\n", EQUALIZER_BAND_MIN, EQUALIZER_BAND_MAX);
            return HSC_OUT_OF_BOUNDS;
        }

        data[i + 2] = (uint8_t)(EQUALIZER_BASELINE + 2 * band_value);
    }

    return hid_write(device_handle, data, MSG_SIZE);
}

/**
 * Device status:
 * 0-1: packet id (0x06 0xb0)
 * 2: BT Default / Bluetooth powerup state: 0 (Off), 1 (On)
 * 3: Bluetooth auto mute: 0 (Off), 1 (-12db), 2 (On),
 * 4: Bluetooth power status: 1 (Off), 4 (On),
 * 5: Bluetooth connection: 0 (Off), 1 (Connected), 2 (Not connected)
 * 6: Headset battery charge: 0-8 (0%-100%)
 * 7: Charge slot battery charge: 0-8 (0%-100%)
 * 8: Transparent noise cancelling level: 0-10
 * 9: Mic status: 0 (Unmuted), 1 (Muted)
 * 10: Noise cancelling: 0 (Off), 1 (Transparent on), 2 (ANC on)
 * 11: Mic led brightness: 0-10
 * 12: Auto off / Inactive time: See `enum inactive_time`
 * 13: 2.4ghz wireless mode: 0 (Speed), 1 (Range)
 * 14: Headset pairing: 1 (Not paired), 4 (Paired but offline), 8 (Connected)
 * 15: Headset power status: See `enum headset_status`
 */
static int read_device_status(hid_device* device_handle, unsigned char* data_read)
{
    unsigned char data_request[MSG_SIZE] = { 0x06, 0xb0 };

    int res = hid_write(device_handle, data_request, MSG_SIZE);
    if (res < 0)
        return res;

    // read device info
    res = hid_read_timeout(device_handle, data_read, STATUS_BUF_SIZE, hsc_device_timeout);

    if (res < 0)
        return res;

    if (res < 2 || !(data_read[0] == 0x06 && data_read[1] == 0xb0)) {
        fprintf(stderr, "Wrong id for device status packet\n");
        return HSC_ERROR;
    }

    return res;
}

static int save_state(hid_device* device_handle)
{
    uint8_t data[MSG_SIZE] = { 0x06, 0x09 };

    return hid_write(device_handle, data, MSG_SIZE);
}

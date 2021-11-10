#pragma once

#include <hidapi.h>
#include <stdint.h>

#define VENDOR_CORSAIR     0x1b1c
#define VENDOR_LOGITECH    0x046d
#define VENDOR_STEELSERIES 0x1038
#define VENDOR_ROCCAT      0x1e7d

/** @brief A list of all features settable/queryable
 *         for headsets
 */
enum capabilities {
    CAP_SIDETONE = 1,
    CAP_BATTERY_STATUS = 2,
    CAP_NOTIFICATION_SOUND = 4,
    CAP_LIGHTS = 8,
    CAP_INACTIVE_TIME = 16,
    CAP_CHATMIX_STATUS = 32,
    CAP_VOICE_PROMPTS = 64,
    CAP_ROTATE_TO_MUTE = 128,
};

static const char* const capabilities_str[] = {
    [CAP_SIDETONE] = "sidetone",
    [CAP_BATTERY_STATUS] = "battery status",
    [CAP_NOTIFICATION_SOUND] = "notification sound",
    [CAP_LIGHTS] = "lights",
    [CAP_INACTIVE_TIME] = "inactive time",
    [CAP_CHATMIX_STATUS] = "chatmix",
    [CAP_VOICE_PROMPTS] = "voice prompts",
    [CAP_ROTATE_TO_MUTE] = "rotate to mute",
};

/** @brief Flags for battery status
 */
enum battery_status {
    BATTERY_UNAVAILABLE = 65534,
    BATTERY_CHARGING = 65535
};

enum headsetcontrol_errors {
    HSC_ERROR = -100
};

/** @brief Defines the basic data of a device
 *
 *  Also defines function pointers for using supported features
 */
struct device {
    /// USB Vendor id
    uint16_t idVendor;
    /// USB Product id used (and found as connected), set by device_registry.c
    uint16_t idProduct;
    /// The USB Product ids this instance of "struct device" supports
    const uint16_t* idProductsSupported;
    /// Size of idProducts
    int numIdProducts;
    /// Interface ID - zero means first enumerated interface!
    int idInterface;
    /// Usage page, only used when idUsage is not 0; HID Protocol specific
    uint16_t idUsagePage;
    /// Used instead of idInterface when not 0, and only used on Windows currently; HID Protocol specific
    uint16_t idUsage;

    /// Name of device, used as information for the user
    char device_name[64];

    /// Bitmask of currently supported features the software can currently handle
    int capabilities;

    /** @brief Function pointer for setting headset sidetone
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  num             Level of the sidetone, between 0 - 128
     *
     *  @returns    > 0         on success
     *              HSC_ERROR   on error specific to this software
     *              -1          HIDAPI error
     */
    int (*send_sidetone)(hid_device* hid_device, uint8_t num);

    /** @brief Function pointer for retrieving the headsets battery status
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *
     *  @returns    >= 0            battery level (in %)
     *              BATTERY_LOADING when the battery is currently being loaded
     *              -1              HIDAPI error
     */
    int (*request_battery)(hid_device* hid_device);

    /** @brief Function pointer for sending a notification sound to the headset
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  soundid         The soundid which should be used
     *
     *  @returns    > 0         success
     *              -1          HIDAPI error
     */
    int (*notifcation_sound)(hid_device* hid_device, uint8_t soundid);

    /** @brief Function pointer for turning light on or off of the headset
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  on              1 if it should be turned on; 0 otherwise
     *
     *  @returns    > 0         success
     *              -1          HIDAPI error
     */
    int (*switch_lights)(hid_device* hid_device, uint8_t on);

    /** @brief Function pointer for setting headset inactive time
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  num             Number of minutes after which the device
     *                          is turned off, between 0 - 90,
     *                          0 disables the automatic shutdown feature
     *
     *  @returns    > 0         on success
     *              HSC_ERROR   on error specific to this software
     *              -1          HIDAPI error
     */
    int (*send_inactive_time)(hid_device* hid_device, uint8_t num);

    /** @brief Function pointer for retrieving the headsets chatmix level
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *
     *  @returns    >= 0            chatmix level
     *              -1              HIDAPI error
     */
    int (*request_chatmix)(hid_device* hid_device);

    /** @brief Function pointer for enabling or disabling voice
     *  prompts on the headset
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  on              1 if it should be turned on; 0 otherwise
     *
     *  @returns    > 0         success
     *              -1          HIDAPI error
     */
    int (*switch_voice_prompts)(hid_device* hid_device, uint8_t on);

    /** @brief Function pointer for enabling or disabling auto-muting
     *  when rotating the headset microphone
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  on              1 if it should be turned on; 0 otherwise
     *
     *  @returns    > 0         success
     *              -1          HIDAPI error
     */
    int (*switch_rotate_to_mute)(hid_device* hid_device, uint8_t on);
};

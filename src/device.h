#pragma once

#include <hidapi.h>
#include <stdint.h>

#define VENDOR_CORSAIR     0x1b1c
#define VENDOR_LOGITECH    0x046d
#define VENDOR_STEELSERIES 0x1038
#define VENDOR_ROCCAT      0x1e7d

/// Convert given number to bitmask
#define B(X) (1 << X)

/// global read timeout in millisecounds
extern int hsc_device_timeout;

/** @brief A list of all features settable/queryable
 *         for headsets
 *
 *  B(CAP_X) converts them to a bitmask value
 */
enum capabilities {
    CAP_SIDETONE = 0,
    CAP_BATTERY_STATUS,
    CAP_NOTIFICATION_SOUND,
    CAP_LIGHTS,
    CAP_INACTIVE_TIME,
    CAP_CHATMIX_STATUS,
    CAP_VOICE_PROMPTS,
    CAP_ROTATE_TO_MUTE,
    NUM_CAPABILITIES
};

/// Long name of every capability
extern const char* const capabilities_str[NUM_CAPABILITIES];
/// Short name of every capability
extern const char capabilities_str_short[NUM_CAPABILITIES];

struct capability_detail {
    // Usage page, only used when usageid is not 0; HID Protocol specific
    int usagepage;
    // Used instead of interface when not 0, and only used on Windows currently; HID Protocol specific
    int usageid;
    /// Interface ID - zero means first enumerated interface!
    int interface;
};

/** @brief Flags for battery status
 */
enum battery_status {
    BATTERY_UNAVAILABLE = 65534,
    BATTERY_CHARGING    = 65535
};

enum headsetcontrol_errors {
    HSC_ERROR        = -100,
    HSC_READ_TIMEOUT = -101,
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

    /// Name of device, used as information for the user
    char device_name[64];

    /// Bitmask of currently supported features the software can currently handle
    int capabilities;
    /// Details of all capabilities (e.g. to which interface to connect)
    struct capability_detail capability_details[NUM_CAPABILITIES];

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

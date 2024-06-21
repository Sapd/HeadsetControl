#pragma once

#include <hidapi.h>
#include <stdbool.h>
#include <stdint.h>

#define VENDOR_CORSAIR     0x1b1c
#define VENDOR_LOGITECH    0x046d
#define VENDOR_STEELSERIES 0x1038
#define VENDOR_ROCCAT      0x1e7d

#define VENDOR_TESTDEVICE  0xF00B
#define PRODUCT_TESTDEVICE 0xA00C

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
    CAP_EQUALIZER_PRESET,
    CAP_EQUALIZER,
    CAP_MICROPHONE_MUTE_LED_BRIGHTNESS,
    CAP_MICROPHONE_VOLUME,
    CAP_VOLUME_LIMITER,
    CAP_BT_WHEN_POWERED_ON,
    CAP_BT_CALL_VOLUME,
    NUM_CAPABILITIES
};

enum capabilitytype {
    CAPABILITYTYPE_ACTION,
    CAPABILITYTYPE_INFO
};

/// Long name of every capability
extern const char* const capabilities_str[NUM_CAPABILITIES];
/// Short name of every capability (deprecated)
extern const char capabilities_str_short[NUM_CAPABILITIES];
/// Enum name of every capability
extern const char* const capabilities_str_enum[NUM_CAPABILITIES];

static inline bool has_capability(int device_capabilities, enum capabilities cap)
{
    return (device_capabilities & B(cap)) == B(cap);
}

struct capability_detail {
    // Usage page, only used when usageid is not 0; HID Protocol specific
    uint16_t usagepage;
    // Used instead of interface when not 0, and only used on Windows currently; HID Protocol specific
    uint16_t usageid;
    /// Interface ID - zero means first enumerated interface!
    int interface;
};

/** @brief Flags for battery status
 */
enum battery_status {
    BATTERY_UNAVAILABLE,
    BATTERY_CHARGING,
    BATTERY_AVAILABLE,
    BATTERY_HIDERROR,
    BATTERY_TIMEOUT,
};

enum microphone_status {
    MICROPHONE_UNKNOWN = 0,
    MICROPHONE_UP,
};

typedef struct {
    int level;
    enum battery_status status;
    // Often in battery status reports, the microphone status is also included
    enum microphone_status microphone_status;
} BatteryInfo;

typedef struct {
    int bands_count;
    int bands_baseline;
    float bands_step;
    int bands_min;
    int bands_max;
} EqualizerInfo;

typedef struct {
    char* name;
    float* values;
} EqualizerPreset;

typedef struct {
    int count;
    EqualizerPreset presets[];
} EqualizerPresets;

enum headsetcontrol_errors {
    HSC_ERROR         = -100,
    HSC_READ_TIMEOUT  = -101,
    HSC_OUT_OF_BOUNDS = -102,
};

typedef enum {
    FEATURE_SUCCESS,
    FEATURE_ERROR,
    FEATURE_DEVICE_FAILED_OPEN,
    FEATURE_INFO, // For non-error, informational states like "charging"
    FEATURE_NOT_PROCESSED
} FeatureStatus;

typedef struct {
    FeatureStatus status;
    /// Can hold battery level, error codes, or special status codes
    int value;
    /// Status depending on the feature (not used by all)
    int status2;
    /// For error messages, "Charging", "Unavailable", etc. Should be free()d after use
    char* message;
} FeatureResult;

typedef struct {
    enum capabilities cap;
    enum capabilitytype type;
    void* param;
    bool should_process;
    FeatureResult result;
} FeatureRequest;

/** @brief Defines equalizer custom setings
 */
struct equalizer_settings {
    /// The size of the bands array
    int size;
    /// The equalizer frequency bands values
    float* bands_values;
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

    // Equalizer Infos
    EqualizerInfo* equalizer;
    EqualizerPresets* eqaulizer_presets;

    wchar_t device_hid_vendorname[64];
    wchar_t device_hid_productname[64];

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
    BatteryInfo (*request_battery)(hid_device* hid_device);

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

    /** @brief Function pointer for setting headset equalizer preset
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  num             The preset number, between 0 - 3
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on preset parameter out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_equalizer_preset)(hid_device* hid_device, uint8_t num);

    /** @brief Function pointer for setting headset equalizer
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  settings        The equalizer settings containing
     *                          the frequency bands values
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on equalizer settings size out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_equalizer)(hid_device* hid_device, struct equalizer_settings* settings);

    /** @brief Function pointer for setting headset microphone mute LED brightness
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  num             The level number, between 0 - 3
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on level parameter out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_microphone_mute_led_brightness)(hid_device* hid_device, uint8_t num);

    /** @brief Function pointer for setting headset microphone volume
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  num             The volume number, between 0 - 128
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on volume parameter out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_microphone_volume)(hid_device* hid_device, uint8_t num);

    /** @brief Function pointer for setting headset volume limiter
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  on              1 if it should be turned on; 0 otherwise
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on preset parameter out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_volume_limiter)(hid_device* hid_device, uint8_t on);

    /** @brief Function pointer for setting headset bluetooth when powered on
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  on              1 if it should be turned on; 0 otherwise
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on preset parameter out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_bluetooth_when_powered_on)(hid_device* hid_device, uint8_t num);

    /** @brief Function pointer for setting headset bluetooth call volume
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  num             The volume number during a bluetooth
     *                          call, between 0 - 2
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on volume parameter out of range
     *                                 specific to this hardware
     *              -1                 HIDAPI error
     */
    int (*send_bluetooth_call_volume)(hid_device* hid_device, uint8_t num);
};

#pragma once

#include <cstdint>
#include <hidapi.h>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#define VENDOR_CORSAIR     0x1b1c
#define VENDOR_LOGITECH    0x046d
#define VENDOR_STEELSERIES 0x1038
#define VENDOR_ROCCAT      0x1e7d
#define VENDOR_AUDEZE      0x3329

#define VENDOR_TESTDEVICE  0xF00B
#define PRODUCT_TESTDEVICE 0xA00C

/// Convert given number to bitmask
#define B(X) (1 << X)

/// Platform support flags
#define PLATFORM_LINUX   (1 << 0)
#define PLATFORM_MACOS   (1 << 1)
#define PLATFORM_WINDOWS (1 << 2)
#define PLATFORM_ALL     (PLATFORM_LINUX | PLATFORM_MACOS | PLATFORM_WINDOWS)

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
    CAP_PARAMETRIC_EQUALIZER,
    CAP_MICROPHONE_MUTE_LED_BRIGHTNESS,
    CAP_MICROPHONE_VOLUME,
    CAP_VOLUME_LIMITER,
    CAP_BT_WHEN_POWERED_ON,
    CAP_BT_CALL_VOLUME,
    NUM_CAPABILITIES
};

enum capabilitytype { CAPABILITYTYPE_ACTION,
    CAPABILITYTYPE_INFO };

/// Long name of every capability
extern const char* const capabilities_str[NUM_CAPABILITIES];
/// Short name of every capability (deprecated)
extern const char capabilities_str_short[NUM_CAPABILITIES];
/// Enum name of every capability
extern const char* const capabilities_str_enum[NUM_CAPABILITIES];

struct capability_detail {
    // Usage page, only used when usageid is not 0; HID Protocol specific
    uint16_t usagepage;
    // Used instead of interface when not 0, and only used on Windows currently;
    // HID Protocol specific
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

struct BatteryInfo {
    int level             = -1;
    battery_status status = BATTERY_UNAVAILABLE;
    // Often in battery status reports, the microphone status is also included
    microphone_status mic_status = MICROPHONE_UNKNOWN;
};

/** @brief Information about device equalizer capabilities
 */
struct EqualizerInfo {
    int bands_count    = 0;
    int bands_baseline = 0;
    float bands_step   = 0.0f;
    int bands_min      = 0;
    int bands_max      = 0;
};

/** @brief A named equalizer preset with band values
 */
struct EqualizerPreset {
    std::string name;
    std::vector<float> values;

    EqualizerPreset() = default;
    EqualizerPreset(std::string n, std::vector<float> v)
        : name(std::move(n))
        , values(std::move(v))
    {
    }
};

/** @brief Information about device parametric equalizer capabilities
 */
struct ParametricEqualizerInfo {
    int bands_count    = 0;
    float gain_base    = 0.0f; // default/base gain
    float gain_step    = 0.0f;
    float gain_min     = 0.0f;
    float gain_max     = 0.0f;
    float q_factor_min = 0.0f; // q factor
    float q_factor_max = 0.0f;
    int freq_min       = 0; // frequency
    int freq_max       = 0;
    int filter_types   = 0; // bitmap containing available filter types
};

/** @brief Collection of equalizer presets
 */
struct EqualizerPresets {
    std::vector<EqualizerPreset> presets;

    [[nodiscard]] int count() const { return static_cast<int>(presets.size()); }
    [[nodiscard]] bool empty() const { return presets.empty(); }
};

enum headsetcontrol_errors {
    HSC_ERROR         = -100,
    HSC_READ_TIMEOUT  = -101,
    HSC_OUT_OF_BOUNDS = -102,
    HSC_INVALID_ARG   = -103,
};

enum class FeatureStatus {
    Success,
    Error,
    DeviceFailedOpen,
    Info, // For non-error, informational states like "charging"
    NotProcessed
};

// Legacy constants for backward compatibility
constexpr auto FEATURE_SUCCESS            = FeatureStatus::Success;
constexpr auto FEATURE_ERROR              = FeatureStatus::Error;
constexpr auto FEATURE_DEVICE_FAILED_OPEN = FeatureStatus::DeviceFailedOpen;
constexpr auto FEATURE_INFO               = FeatureStatus::Info;
constexpr auto FEATURE_NOT_PROCESSED      = FeatureStatus::NotProcessed;

struct FeatureResult {
    FeatureStatus status = FeatureStatus::NotProcessed;
    /// Can hold battery level, error codes, or special status codes
    int value = 0;
    /// Status depending on the feature (not used by all)
    int status2 = 0;
    /// For error messages, "Charging", "Unavailable", etc.
    std::string message;

    // Extended battery info (only populated for CAP_BATTERY_STATUS)
    std::optional<int> battery_voltage_mv;
    std::optional<int> battery_time_to_full_min;
    std::optional<int> battery_time_to_empty_min;
};

// FeatureRequest is defined after EqualizerSettings and ParametricEqualizerSettings
// to allow the param variant to use complete types.

/** @brief Equalizer filter types for parametric EQ
 */
enum class EqualizerFilterType {
    LowShelf,
    LowPass,
    Peaking,
    HighPass,
    HighShelf,
    Notch,
    Count
};

/// Number of filter types
constexpr int NUM_EQ_FILTER_TYPES = static_cast<int>(EqualizerFilterType::Count);

/// Enum name of every parametric equalizer filter type
extern const char* const equalizer_filter_type_str[NUM_EQ_FILTER_TYPES];

/** @brief Defines a single parametric equalizer band
 */
struct ParametricEqualizerBand {
    float frequency          = 0.0f;
    float gain               = 0.0f;
    float q_factor           = 1.0f;
    EqualizerFilterType type = EqualizerFilterType::Peaking;
};

/** @brief Defines equalizer custom settings
 */
struct EqualizerSettings {
    std::vector<float> bands;

    EqualizerSettings() = default;
    explicit EqualizerSettings(std::vector<float> b)
        : bands(std::move(b))
    {
    }

    [[nodiscard]] int size() const { return static_cast<int>(bands.size()); }
    [[nodiscard]] bool empty() const { return bands.empty(); }
    [[nodiscard]] const float* data() const { return bands.data(); }
};

/** @brief Defines parametric equalizer custom settings
 */
struct ParametricEqualizerSettings {
    std::vector<ParametricEqualizerBand> bands;

    ParametricEqualizerSettings() = default;
    explicit ParametricEqualizerSettings(std::vector<ParametricEqualizerBand> b)
        : bands(std::move(b))
    {
    }

    [[nodiscard]] int size() const { return static_cast<int>(bands.size()); }
    [[nodiscard]] bool empty() const { return bands.empty(); }
};

// Legacy type aliases for backward compatibility during transition
using equalizer_settings            = EqualizerSettings;
using parametric_equalizer_settings = ParametricEqualizerSettings;
using parametric_equalizer_band     = ParametricEqualizerBand;

/** @brief Type-safe parameter for feature requests
 *
 * This variant replaces the old void* param with proper type safety.
 * - std::monostate: No parameter (for info requests like battery, chatmix)
 * - int: Simple integer parameters (sidetone level, lights on/off, etc.)
 * - EqualizerSettings: For CAP_EQUALIZER
 * - ParametricEqualizerSettings: For CAP_PARAMETRIC_EQUALIZER
 */
using FeatureParam = std::variant<std::monostate, int, EqualizerSettings, ParametricEqualizerSettings>;

/** @brief Represents a pending feature request
 */
struct FeatureRequest {
    capabilities cap    = CAP_SIDETONE;
    capabilitytype type = CAPABILITYTYPE_ACTION;
    FeatureParam param; // Type-safe parameter (was void*)
    bool should_process = false;
    FeatureResult result;
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

    /// Bitmask of supported platforms (PLATFORM_LINUX | PLATFORM_MACOS | PLATFORM_WINDOWS)
    uint8_t supported_platforms;

    // Equalizer Infos
    std::optional<EqualizerInfo> equalizer;
    uint8_t equalizer_presets_count = 0;
    std::optional<EqualizerPresets> equalizer_presets;
    std::optional<ParametricEqualizerInfo> parametric_equalizer;

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
    int (*notification_sound)(hid_device* hid_device, uint8_t soundid);

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
    int (*send_equalizer)(hid_device* hid_device,
        equalizer_settings* settings);

    /** @brief Function pointer for setting headset parametric equalizer
     *
     *  Forwards the request to the device specific implementation
     *
     *  @param  device_handle   The hidapi handle. Must be the same
     *                          device as defined here (same ids)
     *  @param  settings        The parametric equalizer settings containing
     *                          the filter values
     *
     *  @returns    > 0                on success
     *              HSC_OUT_OF_BOUNDS  on equalizer settings size out of range
     *                                 specific to this hardware
     *              HSC_INVALID_ARG    on equalizer filter type
     * invalid/unsupported specific to this hardware -1                 HIDAPI
     * error
     */
    int (*send_parametric_equalizer)(
        hid_device* hid_device, parametric_equalizer_settings* settings);

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
    int (*send_microphone_mute_led_brightness)(hid_device* hid_device,
        uint8_t num);

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

// Forward declaration
namespace headsetcontrol {
class HIDDevice;
}

/**
 * @brief Represents a discovered device with its HID implementation
 */
struct DeviceEntry {
    headsetcontrol::HIDDevice* device = nullptr;
    uint16_t product_id               = 0; // Actual product ID being used
};

bool has_capability(int capability_mask, capabilities cap);

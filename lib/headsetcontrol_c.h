/**
 * @file headsetcontrol_c.h
 * @brief C API for HeadsetControl library
 *
 * This header provides a pure C interface for controlling USB headsets.
 * It wraps the C++ library with an opaque handle-based API.
 *
 * Example usage:
 * @code
 * #include <headsetcontrol_c.h>
 * #include <stdio.h>
 *
 * int main() {
 *     // Discover headsets
 *     hsc_headset_t* headsets = NULL;
 *     int count = hsc_discover(&headsets);
 *
 *     for (int i = 0; i < count; i++) {
 *         printf("Found: %s\n", hsc_get_name(headsets[i]));
 *
 *         // Check battery
 *         if (hsc_supports(headsets[i], HSC_CAP_BATTERY_STATUS)) {
 *             hsc_battery_t battery;
 *             if (hsc_get_battery(headsets[i], &battery) == HSC_RESULT_OK) {
 *                 printf("Battery: %d%%\n", battery.level_percent);
 *             }
 *         }
 *     }
 *
 *     // Cleanup
 *     hsc_free_headsets(headsets, count);
 *     return 0;
 * }
 * @endcode
 */

#ifndef HEADSETCONTROL_C_H
#define HEADSETCONTROL_C_H

#include <stdbool.h>
#include <stdint.h>

/* DLL export/import macros for Windows */
#ifdef _WIN32
#ifdef HSC_BUILDING_DLL
#define HSC_API __declspec(dllexport)
#elif defined(HSC_DLL)
#define HSC_API __declspec(dllimport)
#else
#define HSC_API
#endif
#else
#define HSC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Types
 * ============================================================================ */

/** Opaque headset handle */
typedef void* hsc_headset_t;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    HSC_RESULT_OK             = 0, /**< Success */
    HSC_RESULT_ERROR          = -1, /**< Generic error */
    HSC_RESULT_NOT_SUPPORTED  = -2, /**< Feature not supported */
    HSC_RESULT_DEVICE_OFFLINE = -3, /**< Device is offline */
    HSC_RESULT_TIMEOUT        = -4, /**< Operation timed out */
    HSC_RESULT_HID_ERROR      = -5, /**< HID communication error */
    HSC_RESULT_INVALID_PARAM  = -6, /**< Invalid parameter */
} hsc_result_t;

/* ============================================================================
 * Capabilities (matches C++ enum)
 * ============================================================================ */

typedef enum {
    HSC_CAP_SIDETONE                       = 0,
    HSC_CAP_BATTERY_STATUS                 = 1,
    HSC_CAP_NOTIFICATION_SOUND             = 2,
    HSC_CAP_LIGHTS                         = 3,
    HSC_CAP_INACTIVE_TIME                  = 4,
    HSC_CAP_CHATMIX_STATUS                 = 5,
    HSC_CAP_VOICE_PROMPTS                  = 6,
    HSC_CAP_ROTATE_TO_MUTE                 = 7,
    HSC_CAP_EQUALIZER_PRESET               = 8,
    HSC_CAP_EQUALIZER                      = 9,
    HSC_CAP_PARAMETRIC_EQUALIZER           = 10,
    HSC_CAP_MICROPHONE_MUTE_LED_BRIGHTNESS = 11,
    HSC_CAP_MICROPHONE_VOLUME              = 12,
    HSC_CAP_VOLUME_LIMITER                 = 13,
    HSC_CAP_BT_WHEN_POWERED_ON             = 14,
    HSC_CAP_BT_CALL_VOLUME                 = 15,
    HSC_NUM_CAPABILITIES                   = 16,
} hsc_capability_t;

/* ============================================================================
 * Battery Status
 * ============================================================================ */

typedef enum {
    HSC_BATTERY_UNAVAILABLE = -1,
    HSC_BATTERY_CHARGING    = -2,
    HSC_BATTERY_AVAILABLE   = 0,
    HSC_BATTERY_ERROR       = -100,
    HSC_BATTERY_TIMEOUT     = -101,
} hsc_battery_status_t;

typedef struct {
    int level_percent; /**< Battery level 0-100, or negative for status */
    hsc_battery_status_t status; /**< Battery status */
    int voltage_mv; /**< Voltage in mV, or -1 if not available */
    int time_to_full_min; /**< Minutes to full, or -1 if not available */
    int time_to_empty_min; /**< Minutes to empty, or -1 if not available */
} hsc_battery_t;

/* ============================================================================
 * Other Result Types
 * ============================================================================ */

typedef struct {
    uint8_t current_level;
    uint8_t min_level;
    uint8_t max_level;
} hsc_sidetone_t;

typedef struct {
    int level; /**< Chat-mix level (0-128) */
    int game_volume_percent; /**< Game audio percentage */
    int chat_volume_percent; /**< Chat audio percentage */
} hsc_chatmix_t;

typedef struct {
    uint8_t minutes;
    uint8_t min_minutes;
    uint8_t max_minutes;
} hsc_inactive_time_t;

/* ============================================================================
 * Discovery Functions
 * ============================================================================ */

/**
 * @brief Discover connected headsets
 *
 * Allocates an array of headset handles. Caller must free with hsc_free_headsets().
 *
 * @param[out] headsets Pointer to receive array of headset handles
 * @return Number of headsets found, or negative error code
 */
HSC_API int hsc_discover(hsc_headset_t** headsets);

/**
 * @brief Free headset array returned by hsc_discover()
 *
 * @param headsets Array of headset handles
 * @param count Number of headsets in array
 */
HSC_API void hsc_free_headsets(hsc_headset_t* headsets, int count);

/**
 * @brief Get library version string
 *
 * @return Version string (do not free)
 */
HSC_API const char* hsc_version(void);

/**
 * @brief Get number of supported device types
 *
 * @return Number of device types the library supports
 */
HSC_API int hsc_supported_device_count(void);

/**
 * @brief Get name of supported device by index
 *
 * @param index Device index (0 to hsc_supported_device_count()-1)
 * @return Device name (do not free), or NULL if index out of range
 */
HSC_API const char* hsc_supported_device_name(int index);

/* ============================================================================
 * Headset Information
 * ============================================================================ */

/**
 * @brief Get headset name
 *
 * @param headset Headset handle
 * @return Device name (do not free)
 */
HSC_API const char* hsc_get_name(hsc_headset_t headset);

/**
 * @brief Get USB vendor ID
 *
 * @param headset Headset handle
 * @return Vendor ID
 */
HSC_API uint16_t hsc_get_vendor_id(hsc_headset_t headset);

/**
 * @brief Get USB product ID
 *
 * @param headset Headset handle
 * @return Product ID
 */
HSC_API uint16_t hsc_get_product_id(hsc_headset_t headset);

/**
 * @brief Check if headset supports a capability
 *
 * @param headset Headset handle
 * @param cap Capability to check
 * @return true if supported, false otherwise
 */
HSC_API bool hsc_supports(hsc_headset_t headset, hsc_capability_t cap);

/**
 * @brief Get capabilities bitmask
 *
 * @param headset Headset handle
 * @return Bitmask of supported capabilities
 */
HSC_API int hsc_get_capabilities(hsc_headset_t headset);

/* ============================================================================
 * Battery & Status
 * ============================================================================ */

/**
 * @brief Get battery status
 *
 * @param headset Headset handle
 * @param[out] battery Battery info structure to fill
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_get_battery(hsc_headset_t headset, hsc_battery_t* battery);

/**
 * @brief Get chat-mix level
 *
 * @param headset Headset handle
 * @param[out] chatmix Chat-mix info structure to fill
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_get_chatmix(hsc_headset_t headset, hsc_chatmix_t* chatmix);

/* ============================================================================
 * Audio Controls
 * ============================================================================ */

/**
 * @brief Set sidetone level
 *
 * @param headset Headset handle
 * @param level Sidetone level (0-128, 0 = off)
 * @param[out] result Optional result info (can be NULL)
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_sidetone(hsc_headset_t headset, uint8_t level, hsc_sidetone_t* result);

/**
 * @brief Set volume limiter
 *
 * @param headset Headset handle
 * @param enabled true to enable, false to disable
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_volume_limiter(hsc_headset_t headset, bool enabled);

/* ============================================================================
 * Equalizer
 * ============================================================================ */

/**
 * @brief Set equalizer preset
 *
 * @param headset Headset handle
 * @param preset Preset ID
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_equalizer_preset(hsc_headset_t headset, uint8_t preset);

/**
 * @brief Set custom equalizer curve
 *
 * @param headset Headset handle
 * @param bands Array of band values
 * @param num_bands Number of bands in array
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_equalizer(hsc_headset_t headset, const float* bands, int num_bands);

/**
 * @brief Get number of equalizer presets
 *
 * @param headset Headset handle
 * @return Number of presets, or 0 if not supported
 */
HSC_API int hsc_get_equalizer_presets_count(hsc_headset_t headset);

/* ============================================================================
 * Microphone
 * ============================================================================ */

/**
 * @brief Set microphone volume
 *
 * @param headset Headset handle
 * @param volume Volume level (0-128)
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_mic_volume(hsc_headset_t headset, uint8_t volume);

/**
 * @brief Set microphone mute LED brightness
 *
 * @param headset Headset handle
 * @param brightness Brightness level (0-3)
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_mic_mute_led_brightness(hsc_headset_t headset, uint8_t brightness);

/**
 * @brief Set rotate-to-mute feature
 *
 * @param headset Headset handle
 * @param enabled true to enable, false to disable
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_rotate_to_mute(hsc_headset_t headset, bool enabled);

/* ============================================================================
 * Lights & Audio Cues
 * ============================================================================ */

/**
 * @brief Set lights on/off
 *
 * @param headset Headset handle
 * @param enabled true for on, false for off
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_lights(hsc_headset_t headset, bool enabled);

/**
 * @brief Set voice prompts on/off
 *
 * @param headset Headset handle
 * @param enabled true to enable, false to disable
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_voice_prompts(hsc_headset_t headset, bool enabled);

/**
 * @brief Play notification sound
 *
 * @param headset Headset handle
 * @param sound_id Sound ID to play
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_play_notification_sound(hsc_headset_t headset, uint8_t sound_id);

/* ============================================================================
 * Power & Bluetooth
 * ============================================================================ */

/**
 * @brief Set inactive time (auto power-off)
 *
 * @param headset Headset handle
 * @param minutes Minutes until power-off (0 = disabled)
 * @param[out] result Optional result info (can be NULL)
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_inactive_time(hsc_headset_t headset, uint8_t minutes, hsc_inactive_time_t* result);

/**
 * @brief Set Bluetooth when powered on
 *
 * @param headset Headset handle
 * @param enabled true to enable, false to disable
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_bluetooth_when_powered_on(hsc_headset_t headset, bool enabled);

/**
 * @brief Set Bluetooth call volume
 *
 * @param headset Headset handle
 * @param volume Volume level
 * @return HSC_RESULT_OK on success, negative error code on failure
 */
HSC_API hsc_result_t hsc_set_bluetooth_call_volume(hsc_headset_t headset, uint8_t volume);

/* ============================================================================
 * Test Device Mode
 * ============================================================================ */

/**
 * @brief Enable or disable test device mode
 *
 * When enabled, hsc_discover() will include the HeadsetControl Test device
 * (0xF00B:0xA00C) even when no physical device is connected.
 *
 * @param enabled true to enable, false to disable
 */
HSC_API void hsc_enable_test_device(bool enabled);

/**
 * @brief Check if test device mode is enabled
 *
 * @return true if test device mode is enabled
 */
HSC_API bool hsc_is_test_device_enabled(void);

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Set the HID device timeout
 *
 * Controls how long HID read operations wait for a response.
 * Default is 5000ms (5 seconds).
 *
 * @param timeout_ms Timeout in milliseconds
 */
HSC_API void hsc_set_device_timeout(int timeout_ms);

/**
 * @brief Get the current HID device timeout
 *
 * @return Timeout in milliseconds
 */
HSC_API int hsc_get_device_timeout(void);

/**
 * @brief Set the test profile for the test device
 *
 * Controls the behavior of the HeadsetControl Test device.
 * See documentation for profile numbers.
 *
 * @param profile Profile number (0 = normal)
 */
HSC_API void hsc_set_test_profile(int profile);

/**
 * @brief Get the current test profile
 *
 * @return Current test profile number
 */
HSC_API int hsc_get_test_profile(void);

#ifdef __cplusplus
}
#endif

#endif /* HEADSETCONTROL_C_H */

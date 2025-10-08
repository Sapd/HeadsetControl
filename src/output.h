#pragma once

#include "device.h"

typedef enum OutputType {
    OUTPUT_JSON,
    OUTPUT_YAML,
    OUTPUT_ENV,
    OUTPUT_STANDARD,
    OUTPUT_SHORT
} OutputType;

/**
 * @brief status of the application
 */
typedef struct {
    const char* name;
    char* version;
    const char* api_version;
    const char* hid_version;
    int device_count;
} HeadsetControlStatus;

typedef enum {
    STATUS_SUCCESS,
    STATUS_FAILURE,
    STATUS_PARTIAL
} Status;

/**
 * @brief Action is the result of sending (writing) commands to the device
 *
 * For converting it to JSON/YAML etc.
 */
typedef struct {
    const char* capability;
    const char* capability_str;
    char* device;
    Status status;
    int value;
    char* error_message;
} Action;

#define MAX_ERRORS  10
#define MAX_ACTIONS 16

typedef struct {
    char* source; // For example, "battery"
    char* message; // Error message related to the source
} ErrorInfo;

/**
 * @brief Struct to hold information of a device for the output implementations
 *
 * For converting it to JSON/YAML etc.
 */
typedef struct {
    Status status;
    char* idVendor;
    char* idProduct;
    char* device_name;
    wchar_t* vendor_name;
    wchar_t* product_name;
    enum capabilities capabilities_enum[NUM_CAPABILITIES];
    const char* capabilities[NUM_CAPABILITIES];
    const char* capabilities_str[NUM_CAPABILITIES];
    int capabilities_amount;

    bool has_battery_info;
    enum battery_status battery_status;
    int battery_level;

    bool has_chatmix_info;
    int chatmix;

    bool has_equalizer_info;
    EqualizerInfo* equalizer;
    bool has_equalizer_presets_info;
    EqualizerPresets* equalizer_presets;
    bool has_equalizer_presets_count;
    uint8_t equalizer_presets_count;
    bool has_parametric_equalizer_info;
    ParametricEqualizerInfo* parametric_equalizer;

    Action actions[MAX_ACTIONS];
    int action_count;

    ErrorInfo errors[MAX_ERRORS];
    int error_count;
} HeadsetInfo;

/**
 * @brief Struct to hold information of a feature request for the output implementations
 *
 */
typedef struct {
    /// List of feature requests (commands which the user wants to send to the device or features to request)
    FeatureRequest* featureRequests;
    /// Size of the feature request list
    int size;
    /// List of devices
    struct device* device;
    /// Size of devices
    int num_devices;
} DeviceList;

/**
 * @brief Main output function
 *
 * @param deviceList List of devices
 * @param print_capabilities If the user wanted to print capabilities
 * @param output Output type
 */
void output(DeviceList* deviceList, bool print_capabilities, OutputType output);

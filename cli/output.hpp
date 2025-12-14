#pragma once

#include "device.hpp"

#include <optional>
#include <string>

enum OutputType {
    OUTPUT_JSON,
    OUTPUT_YAML,
    OUTPUT_ENV,
    OUTPUT_STANDARD,
    OUTPUT_SHORT
};

/**
 * @brief status of the application
 */
struct HeadsetControlStatus {
    const char* name;
    const char* version;
    const char* api_version;
    const char* hid_version;
    int device_count;
};

enum Status {
    STATUS_SUCCESS,
    STATUS_FAILURE,
    STATUS_PARTIAL
};

/**
 * @brief Action is the result of sending (writing) commands to the device
 *
 * For converting it to JSON/YAML etc.
 */
struct Action {
    const char* capability;
    const char* capability_str;
    std::string device;
    Status status;
    int value;
    std::string error_message;
};

constexpr int MAX_ERRORS  = 10;
constexpr int MAX_ACTIONS = 16;

struct ErrorInfo {
    std::string source; // For example, "battery"
    std::string message; // Error message related to the source
};

/**
 * @brief Struct to hold information of a device for the output implementations
 *
 * For converting it to JSON/YAML etc.
 */
struct HeadsetInfo {
    Status status = STATUS_SUCCESS;
    std::string idVendor;
    std::string idProduct;
    std::string device_name;
    std::wstring vendor_name;
    std::wstring product_name;
    std::vector<capabilities> capabilities_enum;
    std::vector<std::string> capabilities_list;
    std::vector<std::string> capabilities_str;

    std::optional<BatteryInfo> battery;

    std::optional<int> chatmix;

    std::optional<EqualizerInfo> equalizer;
    std::optional<EqualizerPresets> equalizer_presets;
    std::optional<uint8_t> equalizer_presets_count;
    std::optional<ParametricEqualizerInfo> parametric_equalizer;

    std::vector<Action> actions;
    std::vector<ErrorInfo> errors;
};

// Forward declaration
namespace headsetcontrol {
class HIDDevice;
}

/**
 * @brief Struct to hold information of a feature request for the output implementations
 */
struct DeviceList {
    /// List of feature requests (commands which the user wants to send to the device or features to request)
    FeatureRequest* featureRequests;
    /// Size of the feature request list
    int size;
    /// C++ device pointer
    headsetcontrol::HIDDevice* device;
    /// Actual product ID being used
    uint16_t product_id;
    /// Size of devices
    int num_devices;
};

/**
 * @brief Main output function
 *
 * @param deviceList List of devices
 * @param print_capabilities If the user wanted to print capabilities
 * @param output Output type
 */
void output(DeviceList* deviceList, bool print_capabilities, OutputType output);

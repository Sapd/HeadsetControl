#pragma once

#include "device.hpp"
#include "result_types.hpp"
#include "string_utils.hpp"
#include <format>
#include <string>

namespace headsetcontrol {

/**
 * @brief Create a success FeatureResult
 */
[[nodiscard]] inline FeatureResult make_success(int value = 0, std::string message = "")
{
    return FeatureResult {
        .status  = FEATURE_SUCCESS,
        .value   = value,
        .status2 = 0,
        .message = std::move(message),
    };
}

/**
 * @brief Create an info FeatureResult (non-error informational state)
 */
[[nodiscard]] inline FeatureResult make_info(int value, std::string message)
{
    return FeatureResult {
        .status  = FEATURE_INFO,
        .value   = value,
        .status2 = 0,
        .message = std::move(message),
    };
}

/**
 * @brief Create an error FeatureResult
 */
[[nodiscard]] inline FeatureResult make_error(int value, std::string message)
{
    return FeatureResult {
        .status  = FEATURE_ERROR,
        .value   = value,
        .status2 = 0,
        .message = std::move(message),
    };
}

/**
 * @brief Create error message with HID error details
 */
[[nodiscard]] inline std::string format_hid_error(
    hid_device* device_handle, bool is_test_device, const std::string& base_message)
{
    if (is_test_device) {
        return base_message;
    }
    return std::format("{}. Error: {}", base_message,
        wstring_to_string(hid_error(device_handle)));
}

/**
 * @brief Create result from battery status
 */
[[nodiscard]] inline FeatureResult make_battery_result(
    const BatteryInfo& battery,
    const BatteryResult& battery_result,
    hid_device* device_handle,
    bool is_test_device)
{
    FeatureResult result {
        .status2                   = battery.status,
        .battery_voltage_mv        = battery_result.voltage_mv,
        .battery_time_to_full_min  = battery_result.time_to_full_min,
        .battery_time_to_empty_min = battery_result.time_to_empty_min,
    };

    switch (battery.status) {
    case BATTERY_AVAILABLE:
        result.status  = FEATURE_SUCCESS;
        result.value   = battery.level;
        result.message = std::format("Battery: {}%", battery.level);
        break;

    case BATTERY_CHARGING:
        result.status  = FEATURE_INFO;
        result.value   = battery.level;
        result.message = "Charging";
        break;

    case BATTERY_UNAVAILABLE:
        result.status  = FEATURE_INFO;
        result.value   = BATTERY_UNAVAILABLE;
        result.message = "Battery status unavailable";
        break;

    case BATTERY_TIMEOUT:
        result.status  = FEATURE_ERROR;
        result.value   = BATTERY_TIMEOUT;
        result.message = "Battery status request timed out";
        break;

    default:
        result.status  = FEATURE_ERROR;
        result.value   = static_cast<int>(battery.status);
        result.message = format_hid_error(device_handle, is_test_device,
            "Error retrieving battery status");
        break;
    }

    return result;
}

} // namespace headsetcontrol

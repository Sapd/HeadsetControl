#pragma once

#include "../../result_types.hpp"
#include "../../utility.hpp"
#include "../hid_device.hpp"
#include "../logitech.hpp"
#include "logitech_calibrations.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <span>
#include <vector>

namespace headsetcontrol::protocols {

/**
 * @brief CRTP base class for Logitech HID++ protocol devices
 *
 * This template class provides common functionality for all Logitech devices
 * using the HID++ protocol (versions 1.0 and 2.0). It eliminates the massive
 * boilerplate code that was duplicated across every Logitech device implementation.
 *
 * Features:
 * - Generic battery request with voltage-based estimation
 * - Generic HID++ command send/receive with error handling
 * - Automatic offline detection and timeout handling
 * - Rich result types with comprehensive error messages
 * - Support for both long and short HID++ messages
 *
 * Usage:
 * ```cpp
 * class MyLogitechDevice : public HIDPPDevice<MyLogitechDevice> {
 *     Result<BatteryResult> getBattery(hid_device* h) override {
 *         return requestBatteryHIDPP(h, {0x08, 0x0a}, MY_CALIBRATION);
 *     }
 * };
 * ```
 *
 * @tparam Derived The derived device class (CRTP pattern)
 */
template <typename Derived>
class HIDPPDevice : public HIDDevice {
public:
    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_LOGITECH;
    }

protected:
    /**
     * @brief Send HID++ command and receive response
     *
     * This is the core method that handles all HID++ communication.
     * Eliminates ~25 lines of boilerplate per function!
     *
     * @param device_handle HID device handle
     * @param command HID++ command bytes (feature + params)
     * @param response_length Expected response length (default: 7 for short message)
     * @return Result with response bytes or error
     */
    Result<std::vector<uint8_t>> sendHIDPPCommand(
        hid_device* device_handle,
        std::span<const uint8_t> command,
        size_t response_length = 7) const
    {
        auto start_time = std::chrono::steady_clock::now();

        // Build full HID++ packet
        std::vector<uint8_t> request(HIDPP_LONG_MESSAGE_LENGTH, 0);
        request[0] = HIDPP_LONG_MESSAGE;
        request[1] = HIDPP_DEVICE_RECEIVER;
        std::copy(command.begin(), command.end(), request.begin() + 2);

        // Send command
        if (auto write_result = this->writeHID(device_handle, request); !write_result) {
            return write_result.error();
        }

        // Read response
        std::vector<uint8_t> response(response_length);
        auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        if (!read_result) {
            return read_result.error();
        }

        // Check if device is offline (common HID++ error response)
        if (response.size() > 2 && response[2] == 0xFF) {
            return DeviceError::deviceOffline("Headset not connected or turned off");
        }

        return response;
    }

    /**
     * @brief Request battery status using HID++ voltage-based protocol
     *
     * This method handles the common Logitech battery request pattern:
     * - Send battery voltage request command
     * - Parse voltage from response (bytes 4-5)
     * - Parse charging status (byte 6)
     * - Estimate percentage using calibration curve
     *
     * Eliminates ~45 lines of boilerplate per device!
     *
     * @param device_handle HID device handle
     * @param feature_command Feature ID and params (e.g., {0x08, 0x0a})
     * @param calibration Voltage-to-percentage calibration data
     * @return Result with comprehensive battery information
     */
    Result<BatteryResult> requestBatteryHIDPP(
        hid_device* device_handle,
        std::span<const uint8_t> feature_command,
        const BatteryCalibration& calibration) const
    {
        auto start_time = std::chrono::steady_clock::now();

        // Send HID++ battery request
        auto response_result = sendHIDPPCommand(device_handle, feature_command, 7);
        if (!response_result) {
            return response_result.error();
        }

        const auto& response = *response_result;
        auto end_time  = std::chrono::steady_clock::now();
        auto duration  = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        // Parse charging status (byte 6: 0x01 = idle/discharging, 0x03 = charging)
        uint8_t state = response[6];
        auto status   = (state == 0x03) ? BATTERY_CHARGING : BATTERY_AVAILABLE;

        // Extract voltage from bytes 4 and 5 (big-endian)
        uint16_t voltage = (static_cast<uint16_t>(response[4]) << 8) | response[5];

        // Estimate battery percentage using spline interpolation
        int level = headsetcontrol::spline_battery_level(
            calibration.percentages,
            calibration.voltages,
            voltage);

        // Build rich result
        BatteryResult result {
            .level_percent  = level,
            .status         = status,
            .voltage_mv     = static_cast<int>(voltage),
            .raw_data       = response,
            .query_duration = duration
        };

        // Calculate time estimates
        if (status == BATTERY_CHARGING && level < 100) {
            // Assume ~2 hours to charge from 0 to 100%
            int remaining_percent   = 100 - level;
            result.time_to_full_min = (remaining_percent * 120) / 100;
        }

        if (status == BATTERY_AVAILABLE && level > 0) {
            // Typical Logitech wireless battery life: ~15 hours
            result.time_to_empty_min = (level * 900) / 100;
        }

        return result;
    }

    /**
     * @brief Send HID++ feature command (for sidetone, lights, etc.)
     *
     * @param device_handle HID device handle
     * @param command Feature bytes to send
     * @return Result with success/error
     */
    template <typename T>
    Result<void> sendHIDPPFeature(
        hid_device* device_handle,
        const T& command) const
    {
        std::vector<uint8_t> data(HIDPP_LONG_MESSAGE_LENGTH, 0);
        data[0] = HIDPP_LONG_MESSAGE;
        data[1] = HIDPP_DEVICE_RECEIVER;

        // Copy command data
        size_t copy_size = std::min(command.size(), static_cast<size_t>(HIDPP_LONG_MESSAGE_LENGTH - 2));
        std::copy_n(command.begin(), copy_size, data.begin() + 2);

        return this->writeHID(device_handle, data);
    }

    /**
     * @brief Send HID++ feature and validate response
     *
     * Used for commands that need confirmation (e.g., sidetone with echo)
     *
     * @param device_handle HID device handle
     * @param command Feature bytes
     * @param validate Validation function for response
     * @return Result with success/error
     */
    Result<void> sendHIDPPFeatureWithValidation(
        hid_device* device_handle,
        std::span<const uint8_t> command,
        std::function<Result<void>(std::span<const uint8_t>)> validate = nullptr) const
    {
        auto response = sendHIDPPCommand(device_handle, command, HIDPP_LONG_MESSAGE_LENGTH);
        if (!response) {
            return response.error();
        }

        if (validate) {
            return validate(*response);
        }

        return {};
    }

    /**
     * @brief Set inactive time using HID++ protocol
     *
     * @param device_handle HID device handle
     * @param minutes Inactive time in minutes (0 = never)
     * @return Result code
     */
    Result<InactiveTimeResult> setInactiveTimeHIDPP(hid_device* device_handle, uint8_t minutes) const
    {
        std::array<uint8_t, 3> cmd { 0x07, 0x21, minutes };
        auto result = sendHIDPPCommand(device_handle, cmd, 7);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }
};

} // namespace headsetcontrol::protocols

#pragma once

#include "../utility.hpp"
#include "hid_device.hpp"
#include "logitech.hpp"
#include <vector>

namespace headsetcontrol {

/**
 * @brief Base class for Logitech devices using HID++ protocol
 *
 * This class provides common functionality for Logitech headsets
 * that use the HID++ protocol (versions 1 and 2).
 */
class LogitechDevice : public HIDDevice {
public:
    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_LOGITECH;
    }

protected:
    /**
     * @brief Common battery request implementation for HID++ devices
     *
     * Many Logitech devices share similar battery request protocols.
     * This method handles the common case.
     *
     * @param device_handle HID device handle
     * @param request_command HID++ command bytes for battery request
     * @return Result<BatteryInfo> - battery info or error
     */
    [[nodiscard]] Result<BatteryInfo> requestBatteryHIDPP(hid_device* device_handle,
        std::span<const uint8_t> request_command)
    {
        // Send battery voltage request
        if (auto result = writeHID(device_handle, request_command); !result) {
            return result.error();
        }

        // Read response using std::array
        std::array<uint8_t, 7> data_read {};
        auto read_result = readHIDTimeout(device_handle, data_read, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }

        BatteryInfo info { .level = -1, .status = BATTERY_UNAVAILABLE };

        // Check if headset is offline (common response)
        if (data_read[2] == 0xFF) {
            return info; // BATTERY_UNAVAILABLE
        }

        // Parse battery status (byte 6) - using structured bindings would be nice here
        // 0x1 = idle/discharging, 0x3 = charging
        const auto state = data_read[6];
        info.status      = (state == 0x03) ? BATTERY_CHARGING : BATTERY_AVAILABLE;

        // Extract voltage from bytes 4 and 5
        const uint16_t voltage = (static_cast<uint16_t>(data_read[4]) << 8) | data_read[5];

        return info; // Subclass should set info.level based on voltage
    }

    /**
     * @brief Estimate battery level from voltage using spline interpolation
     *
     * @param percentages Array of battery percentages for calibration
     * @param voltages Array of corresponding voltages (in mV)
     * @param measured_voltage Measured voltage from device
     * @return Estimated battery percentage (0-100)
     */
    template <std::size_t N>
    int estimateBatteryFromVoltage(std::span<const int, N> percentages,
        std::span<const int, N> voltages,
        uint16_t measured_voltage) const
    {
        return headsetcontrol::spline_battery_level(percentages, voltages, measured_voltage);
    }

    /**
     * @brief Common sidetone implementation for HID++ devices
     *
     * @param device_handle HID device handle
     * @param command HID++ sidetone command template (will be modified)
     * @param level Sidetone level (0-128)
     * @param level_byte_index Index in command where level should be inserted
     * @return Result<void> - success or error
     */
    [[nodiscard]] Result<void> setSidetoneHIDPP(hid_device* device_handle,
        std::span<uint8_t> command,
        uint8_t level,
        size_t level_byte_index)
    {
        // Insert level into command
        command[level_byte_index] = level;

        // Send command
        return writeHID(device_handle, command);
    }

    /**
     * @brief Common inactive time implementation for HID++ devices
     *
     * @param device_handle HID device handle
     * @param minutes Inactive time in minutes
     * @return Result<void> - success or error
     */
    [[nodiscard]] Result<void> setInactiveTimeHIDPP(hid_device* device_handle, uint8_t minutes)
    {
        // Use std::array with C++20 designated initializers
        std::array<uint8_t, HIDPP_LONG_MESSAGE_LENGTH> data_request {
            HIDPP_LONG_MESSAGE, HIDPP_DEVICE_RECEIVER, 0x07, 0x21, minutes
        };

        if (auto result = writeHID(device_handle, data_request); !result) {
            return result.error();
        }

        std::array<uint8_t, 7> data_read {};
        auto read_result = readHIDTimeout(device_handle, data_read, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }

        // Check if headset offline
        if (data_read[2] == 0xFF) {
            return DeviceError::deviceOffline("Headset offline");
        }

        return {};
    }
};

} // namespace headsetcontrol

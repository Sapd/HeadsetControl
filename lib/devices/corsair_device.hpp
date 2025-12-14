#pragma once

#include "../utility.hpp"
#include "hid_device.hpp"
#include <array>
#include <cstdint>

namespace headsetcontrol {

/**
 * @brief Base class for Corsair devices
 *
 * This class provides common functionality for Corsair headsets.
 * Corsair uses a proprietary protocol different from Logitech's HID++.
 */
class CorsairDevice : public HIDDevice {
public:
    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_CORSAIR;
    }

protected:
    /**
     * @brief Common sidetone mapping for Corsair devices
     *
     * Many Corsair devices use the same sidetone range: 200-255
     * mapped from the standard 0-128 range.
     *
     * @param level Input level (0-128)
     * @return Mapped level (200-255)
     */
    [[nodiscard]] constexpr uint8_t mapSidetoneLevel(uint8_t level) const noexcept
    {
        return map(level, 0, 128, 200, 255);
    }

    /**
     * @brief Common sidetone command template for Corsair devices
     *
     * Many Corsair devices share this sidetone command structure.
     *
     * @param device_handle HID device handle
     * @param level Sidetone level (0-128, will be mapped to 200-255)
     * @return Result<void> - success or error
     */
    [[nodiscard]] Result<void> sendSidetoneCorsair(hid_device* device_handle, uint8_t level)
    {
        const uint8_t mapped_level = mapSidetoneLevel(level);

        // Standard Corsair sidetone command (64 bytes)
        std::array<uint8_t, 64> data {
            0xFF, 0x0B, 0x00, 0xFF, 0x04, 0x0E, 0xFF, 0x05,
            0x01, 0x04, 0x00, mapped_level
            // Remaining bytes are zero-initialized
        };

        return sendFeatureReport(device_handle, data);
    }

    /**
     * @brief Common battery request for Corsair devices
     *
     * Sends battery status request command.
     *
     * @param device_handle HID device handle
     * @return Result<BatteryInfo> - battery info or error
     */
    [[nodiscard]] Result<BatteryInfo> requestBatteryCorsair(hid_device* device_handle)
    {
        // Send battery status request
        std::array<uint8_t, 2> data_request { 0xC9, 0x64 };

        if (auto result = writeHID(device_handle, data_request); !result) {
            return result.error();
        }

        // Read battery status response (5 bytes)
        std::array<uint8_t, 5> data_read {};
        auto read_result = readHIDTimeout(device_handle, data_read, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }

        // Parse the response
        return parseBatteryResponse(data_read);
    }

    /**
     * @brief Parse Corsair battery response
     *
     * Packet format:
     * [0] = 100 (constant)
     * [1] = 0 (constant)
     * [2] = Battery level (0-100) with microphone flag
     * [3] = 177 (constant)
     * [4] = Status: 0=disconnected, 1=normal, 2=low, 4/5=charging
     *
     * @param data Battery response data
     * @return BatteryInfo structure
     */
    [[nodiscard]] BatteryInfo parseBatteryResponse(std::span<const uint8_t, 5> data) const
    {
        BatteryInfo info { .level = -1, .status = BATTERY_UNAVAILABLE };

        // Microphone status flag in battery byte
        static constexpr uint8_t BATTERY_MICUP_FLAG = 0x80; // bit 7

        const auto status_byte = data[4];

        // Check if headset is connected
        if (status_byte == 0) {
            return info; // BATTERY_UNAVAILABLE
        }

        // Determine charging state
        const bool is_charging = (status_byte == 4 || status_byte == 5);

        // Valid status values: 1 (normal), 2 (low), 4/5 (charging)
        if (status_byte == 1 || status_byte == 2 || is_charging) {
            info.status = is_charging ? BATTERY_CHARGING : BATTERY_AVAILABLE;

            // Extract battery level (0-100)
            auto battery_byte = data[2];

            // Check for microphone up flag (bit 7 of battery byte)
            if (battery_byte & BATTERY_MICUP_FLAG) {
                info.mic_status = MICROPHONE_UP;
                battery_byte &= ~BATTERY_MICUP_FLAG; // Clear flag to get actual level
            }

            info.level = static_cast<int>(battery_byte);
        }

        return info;
    }

    /**
     * @brief Common notification sound for Corsair devices
     *
     * @param device_handle HID device handle
     * @param sound_id Sound ID (typically 0 or 1)
     * @return Result<void> - success or error
     */
    [[nodiscard]] Result<void> sendNotificationSoundCorsair(hid_device* device_handle, uint8_t sound_id)
    {
        std::array<uint8_t, 5> data { 0xCA, 0x02, sound_id };
        return writeHID(device_handle, data, 3); // Only send 3 bytes
    }

    /**
     * @brief Common lights control for Corsair devices
     *
     * @param device_handle HID device handle
     * @param on true to turn lights on, false to turn off
     * @return Result<void> - success or error
     */
    [[nodiscard]] Result<void> switchLightsCorsair(hid_device* device_handle, bool on)
    {
        // Note: 0x00 = lights ON, 0x01 = lights OFF (inverted logic)
        std::array<uint8_t, 3> data { 0xC8, static_cast<uint8_t>(on ? 0x00 : 0x01), 0x00 };
        return writeHID(device_handle, data);
    }
};

} // namespace headsetcontrol

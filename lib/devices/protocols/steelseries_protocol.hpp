#pragma once

#include "../../result_types.hpp"
#include "../../utility.hpp"
#include "../hid_device.hpp"
#include <array>
#include <chrono>
#include <span>
#include <vector>

namespace headsetcontrol::protocols {

/**
 * @brief Base protocol for SteelSeries Legacy devices (Arctis 1, 7, 9, Pro Wireless)
 *
 * Common patterns:
 * - 31-byte HID packets
 * - Direct battery percentage reporting (NOT voltage-based like Logitech)
 * - save_state command after most operations (0x06, 0x09 or 0x90, 0xAA)
 * - Sidetone with level mapping (0-128 to device-specific range)
 * - Chatmix dial support
 *
 * @tparam Derived The derived device class (CRTP pattern)
 */
template <typename Derived>
class SteelSeriesLegacyDevice : public HIDDevice {
public:
    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_STEELSERIES;
    }

protected:
    static constexpr size_t PACKET_SIZE_31 = 31;

    /**
     * @brief Send save state command
     * Override in derived class if using different save command
     */
    virtual Result<void> saveState(hid_device* device_handle) const
    {
        std::array<uint8_t, PACKET_SIZE_31> data { 0x06, 0x09 };
        return this->writeHID(device_handle, data);
    }

    /**
     * @brief Send command with automatic save_state
     *
     * @param device_handle HID device handle
     * @param command Command bytes to send
     * @param save_after Whether to save state after command
     * @return Result with success/error
     */
    template <typename T>
    Result<void> sendCommandWithSave(
        hid_device* device_handle,
        const T& command,
        bool save_after = true) const
    {
        std::array<uint8_t, PACKET_SIZE_31> data {};
        size_t copy_size = std::min(command.size(), data.size());
        std::copy_n(command.begin(), copy_size, data.begin());

        if (auto result = this->writeHID(device_handle, data); !result) {
            return result.error();
        }

        if (save_after) {
            return saveState(device_handle);
        }

        return {};
    }

    /**
     * @brief Request battery using direct percentage protocol
     *
     * SteelSeries uses direct percentage reporting, NOT voltage-based estimation
     *
     * @param device_handle HID device handle
     * @param request_cmd Battery request command
     * @param response_size Expected response size
     * @param battery_byte Index of battery percentage byte
     * @param status_byte Index of status byte (optional, -1 if none)
     * @param offline_value Value that indicates headset is offline
     * @param charging_value Value that indicates charging
     * @param battery_min Minimum battery value from device
     * @param battery_max Maximum battery value from device
     * @return Result with comprehensive battery information
     */
    Result<BatteryResult> requestBatteryDirect(
        hid_device* device_handle,
        std::span<const uint8_t> request_cmd,
        size_t response_size,
        int battery_byte,
        int status_byte,
        uint8_t offline_value,
        uint8_t charging_value,
        uint8_t battery_min,
        uint8_t battery_max) const
    {
        auto start_time = std::chrono::steady_clock::now();

        // Send battery request
        if (auto result = this->writeHID(device_handle, request_cmd); !result) {
            return result.error();
        }

        // Read response
        std::vector<uint8_t> response(response_size);
        auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        if (!read_result) {
            return read_result.error();
        }

        // Check if device is offline (if status byte is provided)
        if (status_byte >= 0 && static_cast<size_t>(status_byte) < response.size()) {
            if (response[status_byte] == offline_value) {
                return DeviceError::deviceOffline("Headset not connected or turned off");
            }
        }

        // Determine charging status
        enum battery_status status = BATTERY_AVAILABLE;
        if (status_byte >= 0 && static_cast<size_t>(status_byte) < response.size()) {
            if (response[status_byte] == charging_value) {
                status = BATTERY_CHARGING;
            }
        }

        // Validate battery_byte is within bounds
        if (battery_byte < 0 || static_cast<size_t>(battery_byte) >= response.size()) {
            return DeviceError::protocolError("Battery byte index out of bounds");
        }

        // Extract battery level (direct percentage, not voltage!)
        int bat_raw = response[battery_byte];
        int level   = map(bat_raw, battery_min, battery_max, 0, 100);

        // Clamp to 0-100
        if (level > 100)
            level = 100;
        if (level < 0)
            level = 0;

        // Build rich result
        BatteryResult result {
            .level_percent  = level,
            .status         = status,
            .raw_data       = response,
            .query_duration = duration
        };

        // Estimate time to empty/full
        if (status == BATTERY_CHARGING && level < 100) {
            int remaining_percent   = 100 - level;
            result.time_to_full_min = (remaining_percent * 120) / 100; // ~2 hours typical
        }

        if (status == BATTERY_AVAILABLE && level > 0) {
            result.time_to_empty_min = (level * 720) / 100; // ~12 hours typical for SS
        }

        return result;
    }

    /**
     * @brief Request chatmix level
     *
     * SteelSeries chatmix uses separate game/chat values that must be combined
     *
     * @param device_handle HID device handle
     * @param request_cmd Chatmix request command (or nullptr to use status read)
     * @param response_size Expected response size
     * @param game_byte Index of game volume byte
     * @param chat_byte Index of chat volume byte
     * @param game_min Minimum game value
     * @param game_max Maximum game value
     * @param chat_min Minimum chat value
     * @param chat_max Maximum chat value
     * @return Chatmix level (0-128, 64 = center)
     */
    Result<int> requestChatmix(
        hid_device* device_handle,
        std::span<const uint8_t> request_cmd,
        size_t response_size,
        int game_byte,
        int chat_byte,
        int game_min, int game_max,
        int chat_min, int chat_max) const
    {
        // Send chatmix request
        if (request_cmd.size() > 0) {
            if (auto result = this->writeHID(device_handle, request_cmd); !result) {
                return result.error();
            }
        }

        // Read response
        std::vector<uint8_t> response(response_size);
        auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }

        // Validate indices are within bounds
        if (game_byte < 0 || static_cast<size_t>(game_byte) >= response.size() || chat_byte < 0 || static_cast<size_t>(chat_byte) >= response.size()) {
            return DeviceError::protocolError("Chatmix byte index out of bounds");
        }

        // Extract and combine game/chat values
        int game = map(response[game_byte], game_min, game_max, 0, 64);
        int chat = map(response[chat_byte], chat_min, chat_max, 0, -64);

        return 64 - (chat + game);
    }
};

/**
 * @brief Base protocol for SteelSeries Nova devices
 *
 * Common patterns:
 * - 64-byte HID packets or feature reports
 * - More sophisticated battery reporting with connection status
 * - Equalizer support (standard and parametric)
 * - Microphone mute LED control
 * - Volume limiter
 * - Bluetooth settings
 *
 * @tparam Derived The derived device class (CRTP pattern)
 */
template <typename Derived>
class SteelSeriesNovaDevice : public HIDDevice {
public:
    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_STEELSERIES;
    }

protected:
    static constexpr size_t MSG_SIZE        = 64;
    static constexpr size_t STATUS_BUF_SIZE = 128;

    /**
     * @brief Read device status
     * Used by battery, chatmix, and other query operations
     */
    Result<std::vector<uint8_t>> readDeviceStatus(hid_device* device_handle) const
    {
        std::array<uint8_t, 2> request { 0x00, 0xb0 };

        if (auto result = this->writeHID(device_handle, request); !result) {
            return result.error();
        }

        std::vector<uint8_t> response(STATUS_BUF_SIZE);
        auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }

        return response;
    }

    /**
     * @brief Send command using modern HID wrapper
     */
    template <typename T>
    Result<void> sendCommand(hid_device* device_handle, const T& command) const
    {
        std::array<uint8_t, MSG_SIZE> data {};
        size_t copy_size = std::min(command.size(), data.size());
        std::copy_n(command.begin(), copy_size, data.begin());

        return this->writeHID(device_handle, data);
    }
};

} // namespace headsetcontrol::protocols

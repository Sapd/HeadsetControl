#pragma once

#include "../result_types.hpp"
#include <cstdint>
#include <format>
#include <hidapi.h>
#include <span>
#include <vector>

namespace headsetcontrol {

/**
 * @brief Abstract HID communication interface
 *
 * Provides a C++ wrapper around HIDAPI with Result-based error handling.
 */
class HIDInterface {
public:
    virtual ~HIDInterface() = default;

    /**
     * @brief Write data to HID device
     *
     * @param device_handle Device handle
     * @param data Data to write
     * @return Result<void> - success or error
     */
    [[nodiscard]] virtual auto write(hid_device* device_handle, std::span<const uint8_t> data)
        -> Result<void>
        = 0;

    /**
     * @brief Write with explicit size
     */
    [[nodiscard]] virtual auto write(hid_device* device_handle, std::span<const uint8_t> data, size_t size)
        -> Result<void>
        = 0;

    /**
     * @brief Read with timeout
     */
    [[nodiscard]] virtual auto readTimeout(hid_device* device_handle, std::span<uint8_t> data, int timeout_ms)
        -> Result<size_t>
        = 0;

    /**
     * @brief Send feature report
     */
    [[nodiscard]] virtual auto sendFeatureReport(hid_device* device_handle, std::span<const uint8_t> data)
        -> Result<void>
        = 0;

    /**
     * @brief Send feature report with size
     */
    [[nodiscard]] virtual auto sendFeatureReport(hid_device* device_handle, std::span<const uint8_t> data, size_t size)
        -> Result<void>
        = 0;

    /**
     * @brief Get feature report
     */
    [[nodiscard]] virtual auto getFeatureReport(hid_device* device_handle, std::span<uint8_t> data)
        -> Result<size_t>
        = 0;

    /**
     * @brief Get input report
     */
    [[nodiscard]] virtual auto getInputReport(hid_device* device_handle, std::span<uint8_t> data)
        -> Result<size_t>
        = 0;
};

// ============================================================================
// Real HID Implementation (Production)
// ============================================================================

/**
 * @brief Production HID interface using real HIDAPI
 *
 * Forwards all calls to the actual HIDAPI library for real hardware communication.
 */
class RealHIDInterface : public HIDInterface {
public:
    [[nodiscard]] auto write(hid_device* device_handle, std::span<const uint8_t> data)
        -> Result<void> override
    {
        const auto bytes_written = hid_write(device_handle, data.data(), data.size());
        if (bytes_written < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("HID write failed: {} bytes, expected {}", bytes_written, data.size()));
        }
        return {};
    }

    [[nodiscard]] auto write(hid_device* device_handle, std::span<const uint8_t> data, size_t size)
        -> Result<void> override
    {
        const auto bytes_written = hid_write(device_handle, data.data(), size);
        if (bytes_written < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("HID write failed: {} bytes, expected {}", bytes_written, size));
        }
        return {};
    }

    [[nodiscard]] auto readTimeout(hid_device* device_handle, std::span<uint8_t> data, int timeout_ms)
        -> Result<size_t> override
    {
        const auto bytes_read = hid_read_timeout(device_handle, data.data(), data.size(), timeout_ms);

        if (bytes_read < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("HID read failed after {}ms timeout", timeout_ms));
        }

        if (bytes_read == 0) [[unlikely]] {
            return DeviceError::timeout(
                std::format("HID read timeout after {}ms (no data)", timeout_ms));
        }

        return static_cast<size_t>(bytes_read);
    }

    [[nodiscard]] auto sendFeatureReport(hid_device* device_handle, std::span<const uint8_t> data)
        -> Result<void> override
    {
        const auto result = hid_send_feature_report(device_handle, data.data(), data.size());
        if (result < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("Failed to send feature report ({} bytes)", data.size()));
        }
        return {};
    }

    [[nodiscard]] auto sendFeatureReport(hid_device* device_handle, std::span<const uint8_t> data, size_t size)
        -> Result<void> override
    {
        const auto result = hid_send_feature_report(device_handle, data.data(), size);
        if (result < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("Failed to send feature report ({} bytes)", size));
        }
        return {};
    }

    [[nodiscard]] auto getFeatureReport(hid_device* device_handle, std::span<uint8_t> data)
        -> Result<size_t> override
    {
        const auto bytes_read = hid_get_feature_report(device_handle, data.data(), data.size());
        if (bytes_read < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("Failed to get feature report (report ID: 0x{:02x})", data[0]));
        }
        return static_cast<size_t>(bytes_read);
    }

    [[nodiscard]] auto getInputReport(hid_device* device_handle, std::span<uint8_t> data)
        -> Result<size_t> override
    {
        const auto bytes_read = hid_get_input_report(device_handle, data.data(), data.size());
        if (bytes_read < 0) [[unlikely]] {
            return DeviceError::hidError(
                std::format("Failed to get input report (report ID: 0x{:02x})", data[0]));
        }
        return static_cast<size_t>(bytes_read);
    }
};

} // namespace headsetcontrol

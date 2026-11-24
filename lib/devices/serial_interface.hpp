#pragma once

#include "../result_types.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace headsetcontrol {

/**
 * @brief Abstract serial port communication interface
 *
 * Counterpart to HIDInterface for devices that are controlled over a USB serial
 * port instead of HID. Ports are referred to by the handle open() returns.
 */
class SerialInterface {
public:
    virtual ~SerialInterface() = default;

    /**
     * @brief Open the serial port of a USB device in raw mode
     *
     * @param vid Vendor ID of the USB device
     * @param pid Product ID of the USB device
     * @param baud_rate Baud rate
     * @param settle_time Time to wait after opening, before discarding
     *                    anything the device sent
     * @return Result<int> - handle of the open port
     */
    [[nodiscard]] virtual auto open(uint16_t vid, uint16_t pid, uint32_t baud_rate,
        std::chrono::milliseconds settle_time) -> Result<int>
        = 0;

    /**
     * @brief Close a port returned by open()
     */
    virtual void close(int handle) = 0;

    /**
     * @brief Write data to the port
     */
    [[nodiscard]] virtual auto write(int handle, std::span<const uint8_t> data)
        -> Result<void>
        = 0;

    /**
     * @brief Read what has arrived, waiting up to timeout_ms for data
     *
     * @return Result<size_t> - bytes read, or a timeout error if nothing arrived
     */
    [[nodiscard]] virtual auto readTimeout(int handle, std::span<uint8_t> data, int timeout_ms)
        -> Result<size_t>
        = 0;
};

// ============================================================================
// Real Serial Implementation (Production)
// ============================================================================

/**
 * @brief Production serial interface
 *
 * Finds the port with get_serial_path(). Only implemented for Linux, open()
 * fails with notSupported elsewhere.
 */
class RealSerialInterface final : public SerialInterface {
public:
    [[nodiscard]] auto open(uint16_t vid, uint16_t pid, uint32_t baud_rate,
        std::chrono::milliseconds settle_time) -> Result<int> override;

    void close(int handle) override;

    [[nodiscard]] auto write(int handle, std::span<const uint8_t> data)
        -> Result<void> override;

    [[nodiscard]] auto readTimeout(int handle, std::span<uint8_t> data, int timeout_ms)
        -> Result<size_t> override;
};

} // namespace headsetcontrol

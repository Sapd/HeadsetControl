#pragma once

#include "../device.hpp"
#include "../result_types.hpp"
#include "../string_utils.hpp"
#include "hid_interface.hpp"
#include <array>
#include <concepts>
#include <cstdint>
#include <hidapi.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace headsetcontrol {

// Concepts for type safety
template <typename T>
concept DeviceImpl = requires(T device, hid_device* handle, uint8_t val) {
    { device.getVendorId() } -> std::same_as<uint16_t>;
    { device.getProductIds() } -> std::convertible_to<std::vector<uint16_t>>;
    { device.getDeviceName() } -> std::convertible_to<std::string>;
};

/**
 * @brief Base class for all HID-based headset devices
 *
 * This class provides common functionality for HID communication,
 * error handling, and capability management. Device-specific
 * implementations should inherit from this class and override
 * the virtual methods for features they support.
 */
class HIDDevice {
public:
    virtual ~HIDDevice() = default;

    /**
     * @brief Get USB vendor ID
     */
    virtual uint16_t getVendorId() const = 0;

    /**
     * @brief Get list of supported USB product IDs
     */
    virtual std::vector<uint16_t> getProductIds() const = 0;

    /**
     * @brief Get human-readable device name
     */
    virtual std::string_view getDeviceName() const = 0;

    /**
     * @brief Get supported platform bitmask
     */
    virtual constexpr uint8_t getSupportedPlatforms() const
    {
        return PLATFORM_ALL; // Default to all platforms
    }

    /**
     * @brief Get capabilities bitmask
     */
    virtual int getCapabilities() const = 0;

    /**
     * @brief Get capability details for a specific capability
     */
    virtual constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const
    {
        // Default: interface 0, no usage page/id (C++20 designated initializers)
        return { .usagepage = 0, .usageid = 0, .interface = 0 };
    }

    /**
     * @brief Get equalizer presets count
     */
    virtual uint8_t getEqualizerPresetsCount() const
    {
        return 0; // Default: no presets
    }

    /**
     * @brief Get parametric equalizer info
     */
    virtual std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const
    {
        return std::nullopt; // Default: no parametric equalizer
    }

    /**
     * @brief Get equalizer info
     */
    virtual std::optional<EqualizerInfo> getEqualizerInfo() const
    {
        return std::nullopt; // Default: no equalizer info
    }

    /**
     * @brief Get equalizer presets
     */
    virtual std::optional<EqualizerPresets> getEqualizerPresets() const
    {
        return std::nullopt; // Default: no equalizer presets
    }

    // ========================================================================
    // Feature implementations (override in subclasses)
    // ========================================================================

    /**
     * @brief Set sidetone with rich result
     *
     * @param device_handle HID device handle
     * @param level Sidetone level (0-128)
     * @return Result with actual level set and supported range, or error
     */
    virtual Result<SidetoneResult> setSidetone(hid_device* /*device_handle*/, uint8_t /*level*/)
    {
        return DeviceError::notSupported("Device does not support sidetone");
    }

    /**
     * @brief Get battery information with rich details
     *
     * @param device_handle HID device handle
     * @return Result with comprehensive battery info, or error
     */
    virtual Result<BatteryResult> getBattery(hid_device* /*device_handle*/)
    {
        return DeviceError::notSupported("Device does not support battery status");
    }

    /**
     * @brief Set lights on/off with rich result
     */
    virtual Result<LightsResult> setLights(hid_device* /*device_handle*/, bool /*on*/)
    {
        return DeviceError::notSupported("Device does not support lights control");
    }

    /**
     * @brief Set inactive time with rich result
     */
    virtual Result<InactiveTimeResult> setInactiveTime(hid_device* /*device_handle*/, uint8_t /*minutes*/)
    {
        return DeviceError::notSupported("Device does not support inactive time setting");
    }

    /**
     * @brief Get device metadata
     *
     * This can query firmware version, serial number, etc. if device supports it.
     */
    virtual Result<DeviceMetadata> getMetadata(hid_device* device_handle)
    {
        DeviceMetadata meta {
            .name       = getDeviceName(),
            .vendor_id  = getVendorId(),
            .product_id = 0 // Will be set by derived class
        };

        // Try to read HID device strings
        wchar_t manufacturer[128] = {};
        wchar_t product[128]      = {};
        wchar_t serial[128]       = {};

        if (hid_get_manufacturer_string(device_handle, manufacturer, 128) == 0) {
            meta.manufacturer = wstring_to_string(manufacturer);
        }

        if (hid_get_product_string(device_handle, product, 128) == 0) {
            meta.product = wstring_to_string(product);
        }

        if (hid_get_serial_number_string(device_handle, serial, 128) == 0 && serial[0] != 0) {
            meta.serial_number = wstring_to_string(serial);
        }

        return meta;
    }

    /**
     * @brief Get capability information
     */
    virtual Result<CapabilityInfo> getCapabilityInfo(enum capabilities cap)
    {
        CapabilityInfo info {
            .capability  = cap,
            .supported   = (getCapabilities() & B(cap)) != 0,
            .description = capabilities_str[cap],
            .hid_details = getCapabilityDetail(cap)
        };

        return info;
    }

    /**
     * @brief Send notification sound
     */
    virtual Result<NotificationSoundResult> notificationSound(hid_device* /*device_handle*/, uint8_t /*sound_id*/)
    {
        return DeviceError::notSupported("Device does not support notification sounds");
    }

    /**
     * @brief Request chat-mix level
     */
    virtual Result<ChatmixResult> getChatmix(hid_device* /*device_handle*/)
    {
        return DeviceError::notSupported("Device does not support chat-mix");
    }

    /**
     * @brief Set voice prompts on/off
     */
    virtual Result<VoicePromptsResult> setVoicePrompts(hid_device* /*device_handle*/, bool /*enabled*/)
    {
        return DeviceError::notSupported("Device does not support voice prompts");
    }

    /**
     * @brief Set rotate-to-mute on/off
     */
    virtual Result<RotateToMuteResult> setRotateToMute(hid_device* /*device_handle*/, bool /*enabled*/)
    {
        return DeviceError::notSupported("Device does not support rotate-to-mute");
    }

    /**
     * @brief Set equalizer preset
     */
    virtual Result<EqualizerPresetResult> setEqualizerPreset(hid_device* /*device_handle*/, uint8_t /*preset*/)
    {
        return DeviceError::notSupported("Device does not support equalizer presets");
    }

    /**
     * @brief Set custom equalizer settings
     */
    virtual Result<EqualizerResult> setEqualizer(hid_device* /*device_handle*/, const EqualizerSettings& /*settings*/)
    {
        return DeviceError::notSupported("Device does not support custom equalizer");
    }

    /**
     * @brief Set parametric equalizer settings
     */
    virtual Result<ParametricEqualizerResult> setParametricEqualizer(hid_device* /*device_handle*/, const ParametricEqualizerSettings& /*settings*/)
    {
        return DeviceError::notSupported("Device does not support parametric equalizer");
    }

    /**
     * @brief Set microphone mute LED brightness
     */
    virtual Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* /*device_handle*/, uint8_t /*brightness*/)
    {
        return DeviceError::notSupported("Device does not support microphone mute LED brightness");
    }

    /**
     * @brief Set microphone volume
     */
    virtual Result<MicVolumeResult> setMicVolume(hid_device* /*device_handle*/, uint8_t /*volume*/)
    {
        return DeviceError::notSupported("Device does not support microphone volume");
    }

    /**
     * @brief Set volume limiter
     */
    virtual Result<VolumeLimiterResult> setVolumeLimiter(hid_device* /*device_handle*/, bool /*enabled*/)
    {
        return DeviceError::notSupported("Device does not support volume limiter");
    }

    /**
     * @brief Set bluetooth when powered on setting
     */
    virtual Result<BluetoothWhenPoweredOnResult> setBluetoothWhenPoweredOn(hid_device* /*device_handle*/, bool /*enabled*/)
    {
        return DeviceError::notSupported("Device does not support Bluetooth when powered on");
    }

    /**
     * @brief Set bluetooth call volume
     */
    virtual Result<BluetoothCallVolumeResult> setBluetoothCallVolume(hid_device* /*device_handle*/, uint8_t /*volume*/)
    {
        return DeviceError::notSupported("Device does not support Bluetooth call volume");
    }

    // ========================================================================
    // C interface conversion
    // ========================================================================

    /**
     * @brief Convert this C++ device to a C struct device
     *
     * This allows the C++ implementation to interoperate with
     * the existing C codebase.
     */
    virtual struct device* toCDevice();

protected:
    // ========================================================================
    // HID Interface
    // ========================================================================

    /**
     * @brief Get HID interface singleton
     */
    [[nodiscard]] static auto getHIDInterface() -> RealHIDInterface&
    {
        static RealHIDInterface instance;
        return instance;
    }
    // ========================================================================
    // Modern HID Abstractions (C++20 style with Result<T>)
    // ========================================================================

    /**
     * @brief Write data to HID device with modern error handling
     *
     * Modern C++20 abstraction over raw HID write operations. Uses dependency
     * injection for testability, std::span for safety, Result<T> for error
     * handling, and [[nodiscard]] to ensure errors are handled.
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Data to write (automatically sized via std::span)
     * @return Result<void> - success or descriptive error
     *
     * @example
     * std::array<uint8_t, 3> cmd { 0x0, 0x39, level };
     * if (auto result = writeHID(device_handle, cmd); !result) {
     *     return result.error();
     * }
     */
    [[nodiscard]] auto writeHID(hid_device* device_handle,
        std::span<const uint8_t> data) const -> Result<void>
    {
        return getHIDInterface().write(device_handle, data);
    }

    /**
     * @brief Write data with explicit size to HID device
     *
     * Overload for when you need to write a specific size (e.g., MSG_SIZE = 64)
     * even when the actual data buffer is smaller.
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Data buffer
     * @param size Number of bytes to write (will pad with zeros if needed)
     * @return Result<void> - success or error with context
     */
    [[nodiscard]] auto writeHID(hid_device* device_handle,
        std::span<const uint8_t> data,
        size_t size) const -> Result<void>
    {
        return getHIDInterface().write(device_handle, data, size);
    }

    /**
     * @brief Read from HID device with timeout
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Buffer to read into
     * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = blocking)
     * @return Result<size_t> - number of bytes read or error
     */
    [[nodiscard]] auto readHIDTimeout(hid_device* device_handle,
        std::span<uint8_t> data,
        int timeout_ms) const -> Result<size_t>
    {
        return getHIDInterface().readTimeout(device_handle, data, timeout_ms);
    }

    /**
     * @brief Send feature report to HID device
     *
     * Feature reports are used for device configuration and settings that
     * persist across power cycles.
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Feature report data (first byte is usually report ID)
     * @return Result<void> - success or error
     */
    [[nodiscard]] auto sendFeatureReport(hid_device* device_handle,
        std::span<const uint8_t> data) const -> Result<void>
    {
        return getHIDInterface().sendFeatureReport(device_handle, data);
    }

    /**
     * @brief Send feature report with explicit size
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Feature report buffer
     * @param size Number of bytes to send
     * @return Result<void> - success or error
     */
    [[nodiscard]] auto sendFeatureReport(hid_device* device_handle,
        std::span<const uint8_t> data,
        size_t size) const -> Result<void>
    {
        return getHIDInterface().sendFeatureReport(device_handle, data, size);
    }

    /**
     * @brief Get feature report from HID device
     *
     * Retrieves a feature report from the device. The first byte of the
     * buffer should be set to the report ID you want to retrieve.
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Buffer to read into (first byte = report ID)
     * @return Result<size_t> - number of bytes read or error
     */
    [[nodiscard]] auto getFeatureReport(hid_device* device_handle,
        std::span<uint8_t> data) const -> Result<size_t>
    {
        return getHIDInterface().getFeatureReport(device_handle, data);
    }

    /**
     * @brief Get input report from HID device
     *
     * Retrieves an input report using the control channel. The first byte
     * should be set to the report ID.
     *
     * @param device_handle HID device handle (can be nullptr for mocks)
     * @param data Buffer to read into (first byte = report ID)
     * @return Result<size_t> - number of bytes read or error
     */
    [[nodiscard]] auto getInputReport(hid_device* device_handle,
        std::span<uint8_t> data) const -> Result<size_t>
    {
        return getHIDInterface().getInputReport(device_handle, data);
    }

private:
    // Cache for C struct conversion (allocated on first call to toCDevice)
    std::unique_ptr<struct device> c_device_cache;
};

} // namespace headsetcontrol

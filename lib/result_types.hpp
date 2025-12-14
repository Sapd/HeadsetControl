#pragma once

#include "device.hpp"
#include <cassert>
#include <chrono>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <variant>

namespace headsetcontrol {

// ============================================================================
// Error Handling
// ============================================================================

/**
 * @brief Rich error type with context
 */
struct DeviceError {
    enum class Code {
        Success,
        Timeout,
        DeviceOffline,
        ProtocolError,
        InvalidParameter,
        NotSupported,
        PermissionDenied,
        USBError,
        HIDError,
        OutOfBounds,
        Unknown
    };

    Code code = Code::Unknown;
    std::string message;
    std::string details;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    std::source_location location                   = std::source_location::current();

    [[nodiscard]] std::string fullMessage() const;

    static DeviceError timeout(std::string_view details = "",
        std::source_location loc                        = std::source_location::current());

    static DeviceError deviceOffline(std::string_view details = "",
        std::source_location loc                              = std::source_location::current());

    static DeviceError protocolError(std::string_view details = "",
        std::source_location loc                              = std::source_location::current());

    static DeviceError invalidParameter(std::string_view details = "",
        std::source_location loc                                 = std::source_location::current());

    static DeviceError notSupported(std::string_view details = "",
        std::source_location loc                             = std::source_location::current());

    static DeviceError hidError(std::string_view details = "",
        std::source_location loc                         = std::source_location::current());
};

/**
 * @brief Result type (simplified std::expected for C++20)
 */
template <typename T>
class [[nodiscard]] Result {
public:
    // Success constructor
    Result(T value)
        : data_(std::move(value))
    {
    }

    // Error constructor
    Result(DeviceError error)
        : data_(std::move(error))
    {
    }

    [[nodiscard]] bool hasValue() const { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool hasError() const { return std::holds_alternative<DeviceError>(data_); }

    [[nodiscard]] explicit operator bool() const { return hasValue(); }

    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }

    [[nodiscard]] DeviceError& error() & { return std::get<DeviceError>(data_); }
    [[nodiscard]] const DeviceError& error() const& { return std::get<DeviceError>(data_); }

    [[nodiscard]] T& operator*() & { return value(); }
    [[nodiscard]] const T& operator*() const& { return value(); }
    [[nodiscard]] T&& operator*() && { return std::move(value()); }

    [[nodiscard]] T* operator->() { return &value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }

    template <typename U>
    [[nodiscard]] T valueOr(U&& default_value) const&
    {
        return hasValue() ? value() : static_cast<T>(std::forward<U>(default_value));
    }

private:
    std::variant<T, DeviceError> data_;
};

// Specialization for void
template <>
class [[nodiscard]] Result<void> {
public:
    Result()
        : error_(std::nullopt)
    {
    }
    Result(DeviceError error)
        : error_(std::move(error))
    {
    }

    [[nodiscard]] bool hasValue() const { return !error_.has_value(); }
    [[nodiscard]] bool hasError() const { return error_.has_value(); }
    [[nodiscard]] explicit operator bool() const { return hasValue(); }

    /**
     * @brief Get the error
     * @pre hasError() must be true, otherwise behavior is undefined
     * @note In debug builds, an assertion will fire if called incorrectly
     */
    [[nodiscard]] DeviceError& error()
    {
        assert(error_.has_value() && "error() called on successful Result<void>");
        return *error_;
    }

    /**
     * @brief Get the error (const version)
     * @pre hasError() must be true, otherwise behavior is undefined
     * @note In debug builds, an assertion will fire if called incorrectly
     */
    [[nodiscard]] const DeviceError& error() const
    {
        assert(error_.has_value() && "error() called on successful Result<void>");
        return *error_;
    }

private:
    std::optional<DeviceError> error_;
};

// ============================================================================
// Rich Result Types
// ============================================================================

/**
 * @brief Battery information
 *
 * Simplified to only include fields that devices actually populate.
 * Extended fields can be added when devices support them.
 */
struct BatteryResult {
    // Core info (always populated)
    int level_percent; // 0-100%
    enum battery_status status; // AVAILABLE, CHARGING, etc.
    enum microphone_status mic_status = MICROPHONE_UNKNOWN;

    // Extended info (populated if device supports it)
    std::optional<int> voltage_mv; // Battery voltage in millivolts
    std::optional<int> time_to_full_min; // Estimated minutes until full charge
    std::optional<int> time_to_empty_min; // Estimated minutes until empty
    std::optional<std::vector<uint8_t>> raw_data; // Raw HID response for debugging
    std::chrono::milliseconds query_duration { 0 }; // Time taken to query battery
};

/**
 * @brief Sidetone information with range
 */
struct SidetoneResult {
    uint8_t current_level; // Current sidetone level
    uint8_t min_level; // Minimum supported level
    uint8_t max_level; // Maximum supported level
    uint8_t device_min; // Device's native min (e.g., 200 for Corsair)
    uint8_t device_max; // Device's native max (e.g., 255 for Corsair)
    bool is_muted = false; // Whether sidetone is muted
};

/**
 * @brief Lights status information
 */
struct LightsResult {
    bool enabled; // Lights on/off
    std::optional<std::string> mode; // Mode if supported (e.g., "breathing", "static", "off")
};

/**
 * @brief Inactive time information
 */
struct InactiveTimeResult {
    uint8_t minutes; // Minutes (0 = disabled)
    uint8_t min_minutes; // Minimum supported
    uint8_t max_minutes; // Maximum supported (typically 90)
};

/**
 * @brief Chat-mix dial information
 */
struct ChatmixResult {
    int level; // 0-128 (< 64 = game, > 64 = chat)
    int game_volume_percent; // Calculated game volume
    int chat_volume_percent; // Calculated chat volume
};

/**
 * @brief Device metadata and information
 */
struct DeviceMetadata {
    std::string_view name;
    uint16_t vendor_id;
    uint16_t product_id;

    // HID device info
    std::string manufacturer; // From HID descriptor
    std::string product; // From HID descriptor
    std::optional<std::string> serial_number;
};

/**
 * @brief Notification sound result
 */
struct NotificationSoundResult {
    uint8_t sound_id; // Sound ID that was played
};

/**
 * @brief Voice prompts status
 */
struct VoicePromptsResult {
    bool enabled; // Whether voice prompts are on
};

/**
 * @brief Rotate to mute status
 */
struct RotateToMuteResult {
    bool enabled; // Whether rotate-to-mute is on
};

/**
 * @brief Equalizer preset result
 */
struct EqualizerPresetResult {
    uint8_t preset; // Preset ID
    uint8_t total_presets; // Total number of presets available
};

/**
 * @brief Custom equalizer result
 */
struct EqualizerResult {
    // Success indicator (presence of this result means success)
};

/**
 * @brief Parametric equalizer result
 */
struct ParametricEqualizerResult {
    // Success indicator (presence of this result means success)
};

/**
 * @brief Microphone mute LED brightness result
 */
struct MicMuteLedBrightnessResult {
    uint8_t brightness; // Brightness level
    uint8_t min_brightness; // Minimum supported
    uint8_t max_brightness; // Maximum supported
};

/**
 * @brief Microphone volume result
 */
struct MicVolumeResult {
    uint8_t volume; // Volume level
    uint8_t min_volume; // Minimum supported
    uint8_t max_volume; // Maximum supported
};

/**
 * @brief Volume limiter result
 */
struct VolumeLimiterResult {
    bool enabled; // Whether volume limiter is on
};

/**
 * @brief Bluetooth when powered on result
 */
struct BluetoothWhenPoweredOnResult {
    bool enabled; // Whether Bluetooth turns on with device
};

/**
 * @brief Bluetooth call volume result
 */
struct BluetoothCallVolumeResult {
    uint8_t volume; // Volume level
    uint8_t min_volume; // Minimum supported
    uint8_t max_volume; // Maximum supported
};

/**
 * @brief Capability information with details
 */
struct CapabilityInfo {
    enum capabilities capability;
    bool supported;
    std::string_view description;

    // Parameter information
    struct RangeParam {
        int min;
        int max;
        int step = 1;
        std::optional<std::string_view> units;
    };

    struct ChoiceParam {
        std::vector<std::string_view> choices;
    };

    struct BoolParam {
        // Just on/off
    };

    std::variant<std::monostate, RangeParam, ChoiceParam, BoolParam> parameter;

    // HID details
    capability_detail hid_details;
};

} // namespace headsetcontrol

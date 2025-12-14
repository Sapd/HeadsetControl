#pragma once

#include "../device.hpp"
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace headsetcontrol {

// ============================================================================
// Sidetone Level Helpers
// ============================================================================

/**
 * @brief Map sidetone level (0-128) to discrete device levels
 *
 * Many devices only support a fixed number of sidetone levels.
 * This helper provides uniform mapping from 0-128 to N discrete levels.
 *
 * @tparam N Number of discrete levels supported by device
 * @param level Input level (0-128)
 * @return Discrete level (0 to N-1)
 *
 * @example
 * // Device supports 4 levels (0, 1, 2, 3)
 * auto mapped = mapSidetoneToDiscrete<4>(64);  // Returns 2
 * auto mapped = mapSidetoneToDiscrete<4>(128); // Returns 3
 * auto mapped = mapSidetoneToDiscrete<4>(0);   // Returns 0
 */
template <size_t N>
[[nodiscard]] constexpr uint8_t mapSidetoneToDiscrete(uint8_t level)
{
    static_assert(N > 0, "Device must support at least 1 level");

    if (level == 0)
        return 0;

    // Calculate threshold for each level
    // E.g., for N=4: thresholds are at 32, 64, 96, 128
    constexpr uint8_t step = 128 / N;

    for (size_t i = 1; i < N; ++i) {
        if (level < step * i) {
            return static_cast<uint8_t>(i - 1);
        }
    }
    return static_cast<uint8_t>(N - 1);
}

/**
 * @brief Map sidetone level (0-128) to device range with on/off support
 *
 * For devices that have both an on/off toggle AND a level range.
 * Returns {is_on, mapped_level}.
 *
 * @param level Input level (0-128)
 * @param device_min Device's minimum level when on
 * @param device_max Device's maximum level
 * @return Pair of {is_on, mapped_level}
 */
[[nodiscard]] constexpr std::pair<bool, uint8_t> mapSidetoneWithToggle(
    uint8_t level, uint8_t device_min, uint8_t device_max)
{
    if (level == 0) {
        return { false, 0 };
    }
    // Map 1-128 to device_min-device_max
    uint8_t mapped = device_min + ((level - 1) * (device_max - device_min)) / 127;
    return { true, mapped };
}

// ============================================================================
// Range Mapping Utilities
// ============================================================================

/**
 * @brief Concept for arithmetic types suitable for mapping
 */
template <typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

/**
 * @brief Linear mapping from one range to another (integer version)
 *
 * Maps a value from [in_min, in_max] to [out_min, out_max] using linear interpolation.
 * Commonly used to convert between normalized (0-128) and device-specific ranges.
 *
 * This overload accepts mixed integer types and returns the most appropriate type.
 *
 * @param value Input value to map
 * @param in_min Minimum of input range
 * @param in_max Maximum of input range
 * @param out_min Minimum of output range
 * @param out_max Maximum of output range
 * @return Mapped value in output range (type determined by out_min/out_max)
 *
 * @example
 * // Map normalized sidetone (0-128) to device range (0-31)
 * uint8_t device_level = map(128, 0, 128, 0, 31); // Returns 31
 * uint8_t device_level = map(64, 0, 128, 0, 31);  // Returns 15
 * int percentage = map(31, 0, 31, 0, 100);        // Returns 100
 */
[[nodiscard]] constexpr auto map(auto value, auto in_min, auto in_max, auto out_min, auto out_max)
{
    using InType  = std::common_type_t<decltype(value), decltype(in_min), decltype(in_max)>;
    using OutType = std::common_type_t<decltype(out_min), decltype(out_max)>;

    // Convert to common input type for comparison
    const auto val   = static_cast<InType>(value);
    const auto i_min = static_cast<InType>(in_min);
    const auto i_max = static_cast<InType>(in_max);

    // Handle edge cases
    if (i_max == i_min) {
        return static_cast<OutType>(out_min);
    }

    // Clamp input to valid range
    const auto clamped = std::clamp(val, i_min, i_max);

    // Integer arithmetic - avoid overflow and maintain precision
    const auto in_range   = i_max - i_min;
    const auto out_range  = out_max - out_min;
    const auto normalized = clamped - i_min;

    // Use 64-bit intermediate to avoid overflow
    return static_cast<OutType>(out_min + (static_cast<int64_t>(normalized) * out_range) / in_range);
}

/**
 * @brief Map with discrete level quantization
 *
 * Maps a continuous input to discrete output levels. Useful for devices
 * that only support specific values (e.g., 4 brightness levels).
 *
 * @param value Input value (0-128 normalized)
 * @param levels Array of discrete output values
 * @return Closest discrete level
 *
 * @example
 * // Map to 4 discrete brightness levels
 * std::array<uint8_t, 4> levels { 0x00, 0x04, 0x08, 0x0C };
 * auto brightness = mapDiscrete(96, levels); // Returns 0x08
 */
template <typename T, std::size_t N>
[[nodiscard]] constexpr auto mapDiscrete(uint8_t value, const std::array<T, N>& levels) -> T
{
    if constexpr (N == 0) {
        return T {};
    }

    // Map to index [0, N-1]
    const auto index = map<uint8_t>(value, 0, 128, 0, static_cast<uint8_t>(N - 1));
    return levels[index];
}

// ============================================================================
// Voltage/Battery Utilities
// ============================================================================

/**
 * @brief Convert voltage (mV) to battery percentage using lookup table
 *
 * Uses linear interpolation between calibration points for accurate
 * battery estimation.
 *
 * @param voltage Measured voltage in millivolts
 * @param calibration_points Array of {percentage, voltage_mV} pairs (sorted by percentage)
 * @return Battery percentage (0-100)
 *
 * @example
 * constexpr std::array<std::pair<uint8_t, uint16_t>, 4> calibration {
 *     {0, 3500}, {50, 3700}, {80, 3900}, {100, 4200}
 * };
 * auto percent = voltageToPercent(3800, calibration); // ~65%
 */
template <std::size_t N>
[[nodiscard]] constexpr auto voltageToPercent(
    uint16_t voltage_mv,
    const std::array<std::pair<uint8_t, uint16_t>, N>& calibration_points) -> uint8_t
{
    if constexpr (N == 0) {
        return 0;
    }

    // Below minimum voltage
    if (voltage_mv <= calibration_points[0].second) {
        return calibration_points[0].first;
    }

    // Above maximum voltage
    if (voltage_mv >= calibration_points[N - 1].second) {
        return calibration_points[N - 1].first;
    }

    // Linear interpolation between points
    for (std::size_t i = 1; i < N; ++i) {
        if (voltage_mv <= calibration_points[i].second) {
            const auto& [p0, v0] = calibration_points[i - 1];
            const auto& [p1, v1] = calibration_points[i];

            return map<uint16_t>(voltage_mv, v0, v1, p0, p1);
        }
    }

    return calibration_points[N - 1].first;
}

// ============================================================================
// Byte Manipulation Utilities
// ============================================================================

/**
 * @brief Extract 16-bit value from two bytes (big-endian)
 *
 * @param high_byte Most significant byte
 * @param low_byte Least significant byte
 * @return Combined 16-bit value
 */
[[nodiscard]] constexpr auto bytes_to_uint16_be(uint8_t high_byte, uint8_t low_byte) -> uint16_t
{
    return static_cast<uint16_t>((static_cast<uint16_t>(high_byte) << 8) | static_cast<uint16_t>(low_byte));
}

/**
 * @brief Extract 16-bit value from two bytes (little-endian)
 */
[[nodiscard]] constexpr auto bytes_to_uint16_le(uint8_t low_byte, uint8_t high_byte) -> uint16_t
{
    return static_cast<uint16_t>((static_cast<uint16_t>(high_byte) << 8) | static_cast<uint16_t>(low_byte));
}

/**
 * @brief Split 16-bit value into two bytes (big-endian)
 *
 * @return {high_byte, low_byte}
 */
[[nodiscard]] constexpr auto uint16_to_bytes_be(uint16_t value) -> std::pair<uint8_t, uint8_t>
{
    return { static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF) };
}

/**
 * @brief Split 16-bit value into two bytes (little-endian)
 *
 * @return {low_byte, high_byte}
 */
[[nodiscard]] constexpr auto uint16_to_bytes_le(uint16_t value) -> std::pair<uint8_t, uint8_t>
{
    return { static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8) };
}

// ============================================================================
// Validation Utilities
// ============================================================================

/**
 * @brief Validate value against discrete allowed values
 *
 * @param value Value to validate
 * @param allowed Array of allowed values
 * @return Closest allowed value
 *
 * @example
 * // Some devices only support specific inactive times
 * constexpr std::array allowed_times = {0, 5, 10, 15, 30};
 * auto validated = validateDiscrete(12, allowed_times); // Returns 10
 */
template <typename T, std::size_t N>
[[nodiscard]] constexpr auto validateDiscrete(T value, const std::array<T, N>& allowed) -> T
{
    if constexpr (N == 0) {
        return T {};
    }

    // Find closest allowed value
    T closest     = allowed[0];
    auto min_diff = std::abs(value - allowed[0]);

    for (std::size_t i = 1; i < N; ++i) {
        const auto diff = std::abs(value - allowed[i]);
        if (diff < min_diff) {
            min_diff = diff;
            closest  = allowed[i];
        }
    }

    return closest;
}

/**
 * @brief Round value to nearest allowed value from sorted array
 *
 * More efficient version when the allowed array is sorted.
 */
template <typename T, std::size_t N>
[[nodiscard]] constexpr auto roundToNearest(T value, const std::array<T, N>& sorted_allowed) -> T
{
    if constexpr (N == 0) {
        return T {};
    }
    if constexpr (N == 1) {
        return sorted_allowed[0];
    }

    // Binary search for closest value
    auto it = std::lower_bound(sorted_allowed.begin(), sorted_allowed.end(), value);

    if (it == sorted_allowed.begin()) {
        return *it;
    }
    if (it == sorted_allowed.end()) {
        return sorted_allowed[N - 1];
    }

    // Compare with previous element
    const auto prev = std::prev(it);
    return (std::abs(value - *prev) < std::abs(value - *it)) ? *prev : *it;
}

// ============================================================================
// Capability Detail Helpers
// ============================================================================

/**
 * @brief Common capability detail for devices using same interface for all capabilities
 *
 * Many devices use the same HID interface, usage page, and usage ID for all features.
 * This helper creates a consistent capability_detail.
 *
 * @param usagepage HID usage page
 * @param usageid HID usage ID
 * @param iface HID interface number
 * @return capability_detail struct
 *
 * @example
 * constexpr capability_detail getCapabilityDetail(enum capabilities) const override {
 *     return makeCapabilityDetail(0xffc0, 0x1, 3);
 * }
 */
[[nodiscard]] constexpr capability_detail makeCapabilityDetail(
    uint16_t usagepage, uint16_t usageid, int iface)
{
    return { .usagepage = usagepage, .usageid = usageid, .interface = iface };
}

// ============================================================================
// Common Inactive Time Validation
// ============================================================================

/**
 * @brief Clamp inactive time to device-supported range
 *
 * @param minutes Requested minutes
 * @param max_minutes Maximum supported by device
 * @return Clamped value
 */
[[nodiscard]] constexpr uint8_t clampInactiveTime(uint8_t minutes, uint8_t max_minutes)
{
    return minutes > max_minutes ? max_minutes : minutes;
}

} // namespace headsetcontrol

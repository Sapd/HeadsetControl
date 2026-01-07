#pragma once

#include <array>
#include <span>

/**
 * @file logitech_calibrations.hpp
 * @brief Logitech-specific battery voltage calibration data
 *
 * Logitech HID++ devices report battery status as voltage (in mV) rather than
 * percentage. This file contains device-specific discharge curves that map
 * voltage readings to battery percentage using spline interpolation.
 *
 * Other vendors (Corsair, SteelSeries, HyperX) report battery percentage
 * directly and do not need voltage calibrations.
 */

namespace headsetcontrol::protocols {

/**
 * @brief Battery voltage calibration data structure
 *
 * Maps voltage measurements (in mV) to battery percentage.
 * Used by spline interpolation for accurate battery estimation.
 */
struct BatteryCalibration {
    std::span<const int> percentages;
    std::span<const int> voltages;

    constexpr BatteryCalibration(std::span<const int> p, std::span<const int> v)
        : percentages(p)
        , voltages(v)
    {
    }
};

} // namespace headsetcontrol::protocols

namespace headsetcontrol::calibrations {

using headsetcontrol::protocols::BatteryCalibration;

/**
 * @brief Battery calibration data for Logitech devices
 *
 * Each Logitech device has slightly different battery discharge characteristics
 * due to variations in battery chemistry, capacity, and power management.
 * These calibrations are based on manual measurements or reverse-engineered
 * discharge curves.
 *
 * Format: (voltage_mV, percentage)
 * Voltage values are typically in range 3000-4200mV for Li-Ion batteries.
 */

// Logitech G533 calibration
// Based on polynomial discharge curve from original implementation
inline constexpr std::array<int, 6> G533_PERCENTAGES { 100, 50, 30, 20, 5, 0 };
inline constexpr std::array<int, 6> G533_VOLTAGES { 4200, 3850, 3790, 3750, 3680, 3330 };
inline constexpr BatteryCalibration LOGITECH_G533 { G533_PERCENTAGES, G533_VOLTAGES };

// Logitech G535 calibration
// Based on manual measurements provided in source comments
// Full 48-point curve available in original code, condensed to 6 key points
inline constexpr std::array<int, 6> G535_PERCENTAGES { 100, 50, 30, 20, 5, 0 };
inline constexpr std::array<int, 6> G535_VOLTAGES { 4175, 3817, 3766, 3730, 3664, 3310 };
inline constexpr BatteryCalibration LOGITECH_G535 { G535_PERCENTAGES, G535_VOLTAGES };

// Logitech G633/G933/G935 calibration
// Conservative values based on typical Li-Ion discharge characteristics
inline constexpr std::array<int, 8> G633_PERCENTAGES { 100, 80, 60, 40, 20, 10, 5, 0 };
inline constexpr std::array<int, 8> G633_VOLTAGES { 4100, 3950, 3850, 3750, 3650, 3500, 3300, 3150 };
inline constexpr BatteryCalibration LOGITECH_G633 { G633_PERCENTAGES, G633_VOLTAGES };

// Same calibration for G933 and G935 (similar hardware)
inline constexpr auto& LOGITECH_G933 = LOGITECH_G633;
inline constexpr auto& LOGITECH_G935 = LOGITECH_G633;
inline constexpr auto& LOGITECH_G635 = LOGITECH_G633;
inline constexpr auto& LOGITECH_G733 = LOGITECH_G633;

// Logitech G430/G432 calibration
// Based on typical USB-powered headset with smaller battery
inline constexpr std::array<int, 6> G430_PERCENTAGES { 100, 50, 30, 20, 5, 0 };
inline constexpr std::array<int, 6> G430_VOLTAGES { 4100, 3800, 3750, 3700, 3600, 3300 };
inline constexpr BatteryCalibration LOGITECH_G430 { G430_PERCENTAGES, G430_VOLTAGES };
inline constexpr auto& LOGITECH_G432 = LOGITECH_G430;

// Logitech G Pro X calibration
// Based on similar hardware to G533
inline constexpr std::array<int, 6> GPRO_PERCENTAGES { 100, 50, 30, 20, 5, 0 };
inline constexpr std::array<int, 6> GPRO_VOLTAGES { 4150, 3830, 3780, 3740, 3670, 3320 };
inline constexpr BatteryCalibration LOGITECH_GPRO { GPRO_PERCENTAGES, GPRO_VOLTAGES };
inline constexpr auto& LOGITECH_GPRO_X  = LOGITECH_GPRO;
inline constexpr auto& LOGITECH_GPRO_X2 = LOGITECH_GPRO;

// Logitech Zone Wired - uses USB power, but may have small buffer battery
inline constexpr std::array<int, 4> ZONE_PERCENTAGES { 100, 50, 20, 0 };
inline constexpr std::array<int, 4> ZONE_VOLTAGES { 4100, 3800, 3600, 3300 };
inline constexpr BatteryCalibration LOGITECH_ZONE_WIRED { ZONE_PERCENTAGES, ZONE_VOLTAGES };

/**
 * @brief Default calibration for unknown Logitech devices
 *
 * Conservative Li-Ion discharge curve that works reasonably well
 * for most Logitech wireless headsets.
 */
inline constexpr std::array<int, 6> DEFAULT_PERCENTAGES { 100, 50, 30, 20, 5, 0 };
inline constexpr std::array<int, 6> DEFAULT_VOLTAGES { 4100, 3850, 3780, 3730, 3650, 3300 };
inline constexpr BatteryCalibration DEFAULT_LOGITECH { DEFAULT_PERCENTAGES, DEFAULT_VOLTAGES };

} // namespace headsetcontrol::calibrations

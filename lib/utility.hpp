#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Forward declarations
struct ParametricEqualizerSettings;

namespace headsetcontrol {

/**
 * @brief Rounds a given positive number to the nearest given multiple
 *
 * I.e. A number of 17 would be rounded to 15 if multiple is 5
 *
 * @param number A number to round
 * @param multiple A multiple
 * @return unsigned int the result rounded number
 */
unsigned int round_to_multiples(unsigned int number, unsigned int multiple);

/**
 * @brief This function calculates the estimate battery level in percent using splines.
 *
 * @param percentages percentage values to be associated with voltage values
 * @param voltages voltage values associated with percentage values
 * @param voltage readings
 * @return battery level in percent
 */
int spline_battery_level(std::span<const int> percentages, std::span<const int> voltages, uint16_t voltage);

/**
 * @brief This function calculates the estimate battery level in percent.
 *
 * To find the terms representing the polynomial discharge curve of the
 * battery a solver like https://arachnoid.com/polysolve/ can be used.
 *
 * @param terms polynomial terms for the battery discharge curve
 * @param voltage readings
 * @return battery level in percent
 */
float poly_battery_level(std::span<const double> terms, uint16_t voltage);

/**
 * @brief Helper function used during debugging for printing out binary data
 *
 * @param data data to be represented as hex string
 * @return hex string representation
 */
std::string hexdump(std::span<const uint8_t> data);

/**
 * @brief Accepts textual input and converts them to a byte buffer
 *
 * Parses data like "0xff, 123, 0xb" and converts them to a vector of bytes
 *
 * @param input string to parse
 * @return vector of parsed bytes
 */
std::vector<uint8_t> parse_byte_data(std::string_view input);

/**
 * @brief Accepts textual input and converts them to float values
 *
 * Parses data like "1.0, 2.5, 3.0" and converts them to a vector of floats
 *
 * @param input string to parse
 * @return vector of parsed floats
 */
std::vector<float> parse_float_data(std::string_view input);

/**
 * @brief Parse parametric equalizer settings string
 *
 * @param input input string or empty for reset
 * @return ParametricEqualizerSettings object
 */
ParametricEqualizerSettings parse_parametric_equalizer_settings(std::string_view input);

/**
 * @brief Parse two IDs from a string like "123:456" or "0x1b1c:0x1b27"
 *
 * @param input string to parse
 * @return pair of IDs if successful, nullopt otherwise
 */
std::optional<std::pair<int, int>> parse_two_ids(std::string_view input);

} // namespace headsetcontrol

// utilities
#include "devices/device_utils.hpp"
#include "string_utils.hpp"

// Bring commonly used functions into global namespace for convenience
using headsetcontrol::map;
using headsetcontrol::round_to_multiples;

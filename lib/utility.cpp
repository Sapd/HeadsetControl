#include "utility.hpp"
#include "device.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace headsetcontrol {

unsigned int round_to_multiples(unsigned int number, unsigned int multiple)
{
    if (multiple == 0) {
        return number;
    }

    // Avoid overflow: check if adding half of multiple would overflow
    const unsigned int half     = multiple / 2;
    const unsigned int max_safe = std::numeric_limits<unsigned int>::max() - half;

    if (number > max_safe) {
        // Would overflow - just do the division directly
        return (number / multiple) * multiple;
    }

    return ((number + half) / multiple) * multiple;
}

int spline_battery_level(std::span<const int> percentages, std::span<const int> voltages, uint16_t voltage)
{
    int percent = 0;
    size_t size = std::min(percentages.size(), voltages.size());

    if (size == 0) {
        return 0;
    }

    for (size_t i = 0; i < size; ++i) {
        // if >= then 100%
        if (voltage >= voltages[i]) {
            percent = percentages[i];
            break;
        }

        // if not last
        if (i < size - 1 && voltage >= voltages[i + 1]) {
            // Guard against division by zero
            const int voltage_diff = voltages[i] - voltages[i + 1];
            if (voltage_diff == 0) {
                percent = percentages[i + 1];
            } else {
                float fraction = static_cast<float>(voltage - voltages[i + 1]) / static_cast<float>(voltage_diff);
                percent        = percentages[i + 1] + static_cast<int>(fraction * static_cast<float>(percentages[i] - percentages[i + 1]));
            }
            break;
        }
    }

    return percent;
}

float poly_battery_level(std::span<const double> terms, uint16_t voltage)
{
    double t       = 1;
    double percent = 0;
    for (size_t i = 0; i < terms.size(); i++) {
        percent += terms[i] * t;
        t *= voltage;
    }

    if (percent > 100)
        percent = 100;
    if (percent < 0)
        percent = 0;
    return static_cast<float>(percent);
}

std::string hexdump(std::span<const uint8_t> data)
{
    std::string result;
    result.reserve(data.size() * 6); // "0xXX " per byte

    for (uint8_t byte : data) {
        result += std::format("0x{:02X} ", byte);
    }

    return result;
}

std::vector<uint8_t> parse_byte_data(std::string_view input)
{
    std::vector<uint8_t> result;
    constexpr std::string_view delimiters = " ,{}\n\r";

    size_t pos = 0;
    while (pos < input.size()) {
        // Skip delimiters
        pos = input.find_first_not_of(delimiters, pos);
        if (pos == std::string_view::npos) {
            break;
        }

        // Find end of token
        size_t end = input.find_first_of(delimiters, pos);
        if (end == std::string_view::npos) {
            end = input.size();
        }

        std::string_view token = input.substr(pos, end - pos);

        // Parse the value (supports hex with 0x prefix)
        long val    = 0;
        bool parsed = false;
        if (token.starts_with("0x") || token.starts_with("0X")) {
            auto [ptr, ec] = std::from_chars(token.data() + 2, token.data() + token.size(), val, 16);
            parsed         = (ec == std::errc() && ptr == token.data() + token.size());
        } else {
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val, 10);
            parsed         = (ec == std::errc() && ptr == token.data() + token.size());
        }

        // Validate range before narrowing to uint8_t
        if (parsed && val >= 0 && val <= 255) {
            result.push_back(static_cast<uint8_t>(val));
        }

        pos = end;
    }

    return result;
}

std::vector<float> parse_float_data(std::string_view input)
{
    std::vector<float> result;
    constexpr std::string_view delimiters = " ,{}\n\r";

    size_t pos = 0;
    while (pos < input.size()) {
        // Skip delimiters
        pos = input.find_first_not_of(delimiters, pos);
        if (pos == std::string_view::npos) {
            break;
        }

        // Find end of token
        size_t end = input.find_first_of(delimiters, pos);
        if (end == std::string_view::npos) {
            end = input.size();
        }

        std::string_view token = input.substr(pos, end - pos);

        // Parse float - use strtof since from_chars for float isn't always available
        std::string token_str(token);
        char* endptr = nullptr;
        errno        = 0;
        float val    = std::strtof(token_str.c_str(), &endptr);

        // Validate: ensure entire string was consumed and no error occurred
        if (endptr != token_str.c_str() && *endptr == '\0' && errno == 0) {
            // Reject NaN and Inf as invalid input
            if (std::isfinite(val)) {
                result.push_back(val);
            }
        }

        pos = end;
    }

    return result;
}

std::optional<std::pair<int, int>> parse_two_ids(std::string_view input)
{
    constexpr std::string_view delimiters = " :.,";

    std::vector<long> values;
    size_t pos = 0;

    while (pos < input.size() && values.size() < 2) {
        pos = input.find_first_not_of(delimiters, pos);
        if (pos == std::string_view::npos) {
            break;
        }

        size_t end = input.find_first_of(delimiters, pos);
        if (end == std::string_view::npos) {
            end = input.size();
        }

        std::string_view token = input.substr(pos, end - pos);

        // Parse the value (supports hex with 0x prefix)
        long val = 0;
        if (token.starts_with("0x") || token.starts_with("0X")) {
            auto [ptr, ec] = std::from_chars(token.data() + 2, token.data() + token.size(), val, 16);
            if (ec == std::errc()) {
                values.push_back(val);
            }
        } else {
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), val, 10);
            if (ec == std::errc()) {
                values.push_back(val);
            }
        }

        pos = end;
    }

    if (values.size() != 2) {
        return std::nullopt;
    }

    return std::make_pair(static_cast<int>(values[0]), static_cast<int>(values[1]));
}

/**
 * Converts a filter type string to the corresponding EqualizerFilterType enum
 * Returns nullopt if the string does not match a known filter type.
 */
static std::optional<EqualizerFilterType> parse_eq_filter_type(std::string_view input)
{
    for (int i = 0; i < NUM_EQ_FILTER_TYPES; i++) {
        if (input == equalizer_filter_type_str[i]) {
            return static_cast<EqualizerFilterType>(i);
        }
    }

    return std::nullopt;
}

/**
 * Parses a equalizer band string into a ParametricEqualizerBand.
 *
 * Expected band_str format: "float,float,float,string"
 * Expected fields:          "frequency,gain,q-factor,filter-type"
 *
 * Returns nullopt if the string can't be parsed.
 */
static std::optional<ParametricEqualizerBand> parse_parametric_equalizer_band(std::string_view band_str)
{
    constexpr std::string_view delimiters = " ,";
    std::vector<std::string_view> tokens;

    size_t pos = 0;
    while (pos < band_str.size()) {
        pos = band_str.find_first_not_of(delimiters, pos);
        if (pos == std::string_view::npos) {
            break;
        }

        size_t end = band_str.find_first_of(delimiters, pos);
        if (end == std::string_view::npos) {
            end = band_str.size();
        }

        tokens.push_back(band_str.substr(pos, end - pos));
        pos = end;
    }

    if (tokens.size() < 4) {
        return std::nullopt;
    }

    ParametricEqualizerBand band;

    // Parse frequency with validation
    std::string freq_str(tokens[0]);
    char* endptr   = nullptr;
    errno          = 0;
    band.frequency = std::strtof(freq_str.c_str(), &endptr);
    if (endptr == freq_str.c_str() || *endptr != '\0' || errno != 0 || !std::isfinite(band.frequency)) {
        std::cerr << "Couldn't parse frequency: " << freq_str << '\n';
        return std::nullopt;
    }

    // Parse gain with validation
    std::string gain_str(tokens[1]);
    endptr    = nullptr;
    errno     = 0;
    band.gain = std::strtof(gain_str.c_str(), &endptr);
    if (endptr == gain_str.c_str() || *endptr != '\0' || errno != 0 || !std::isfinite(band.gain)) {
        std::cerr << "Couldn't parse gain: " << gain_str << '\n';
        return std::nullopt;
    }

    // Parse q_factor with validation
    std::string q_str(tokens[2]);
    endptr        = nullptr;
    errno         = 0;
    band.q_factor = std::strtof(q_str.c_str(), &endptr);
    if (endptr == q_str.c_str() || *endptr != '\0' || errno != 0 || !std::isfinite(band.q_factor)) {
        std::cerr << "Couldn't parse Q factor: " << q_str << '\n';
        return std::nullopt;
    }

    // Parse filter type
    auto filter_type = parse_eq_filter_type(tokens[3]);
    if (!filter_type) {
        std::cerr << "Couldn't parse filter type: " << tokens[3] << '\n';
        return std::nullopt;
    }
    band.type = *filter_type;

    return band;
}

ParametricEqualizerSettings parse_parametric_equalizer_settings(std::string_view input)
{
    ParametricEqualizerSettings settings;

    if (input.empty() || input == "reset") {
        // Return empty if null/empty or reset of bands requested.
        return settings;
    }

    // Parse each band definition separated by ';'
    size_t pos = 0;
    while (pos < input.size()) {
        size_t end = input.find(';', pos);
        if (end == std::string_view::npos) {
            end = input.size();
        }

        std::string_view band_str = input.substr(pos, end - pos);
        auto band                 = parse_parametric_equalizer_band(band_str);
        if (band) {
            settings.bands.push_back(*band);
        }

        pos = end + 1;
    }

    return settings;
}

} // namespace headsetcontrol

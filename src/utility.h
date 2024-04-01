#pragma once

#include <stdarg.h>

// For unused variables
#define UNUSED(x) (void)x;

/** @brief Maps a value x from a given range to another range
 *
 *  The input x is mapped from the range in_min and in_max
 *  in relation to the range out_min and out_max.
 *
 *  @return the mapped value
 */
int map(int x, int in_min, int in_max, int out_min, int out_max);

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
 * @brief This function calculates the estimate batttery level in percent using splines.
 *
 * Example:
 * @code{.c}
 * static const int battery_estimate_percentages[] = { 100, 50, 30, 20, 5, 0 };
 * static const int battery_estimate_voltages[]    = { 4175, 3817, 3766, 3730, 3664, 3310 };
 * static size_t battery_estimate_size = sizeof(battery_estimate_percentages)/sizeof(battery_estimate_percentages[0]);
 * int level = spline_battery_level(battery_estimate_percentages, battery_estimate_voltages, battery_estimate_size, voltage_read);
 * @endcode
 *
 * @param p percentage values to be associated with voltage values
 * @param v voltage values associated with percentage values
 * @param size number of percentage and voltage associations
 * @param voltage readings
 * @return battery level in percent
 */
int spline_battery_level(const int p[], const int v[], const size_t size, uint16_t voltage);

/**
 * @brief This function calculates the estimate batttery level in percent.
 *
 * To find the terms representing the polynominal discarge curve of the
 * battery an solver like https://arachnoid.com/polysolve/ can be used.
 *
 * @param terms polynominal terms for the battery discharge curve
 * @param numterms number of terms
 * @param voltage readings
 * @return battery level in percent
 */
float poly_battery_level(const double terms[], const size_t numterms, uint16_t voltage);

/**
 * @brief Helper function used during debugging for printing out binary data
 *
 * @param out buffer to write output example char tmp[128];
 * @param out_size sizeof(out)
 * @param data data to be represented as hex string
 * @param data_size sizeof(data) or sizeof(*data);
 * @return 0 on failure or filled size of out
 */
size_t hexdump(char* out, size_t out_size, unsigned char* data, size_t data_size);

/**
 * @brief Accepts textual input and converts them to a sendable buffer
 *
 * Parses data like "0xff, 123, 0xb" and converts them to an array of len 3
 *
 * @param input string
 * @param dest destination array
 * @param len max dest length
 * @return int amount of data converted
 */
int get_byte_data_from_parameter(char* input, unsigned char* dest, size_t len);

/**
 * @brief Accepts textual input and converts them to a sendable buffer
 *
 * Parses data like "0xff, 123, 0xb" and converts them to an array of len 3
 *
 * @param input string
 * @param dest destination array
 * @param len max dest length
 * @return int amount of data converted
 */
int get_float_data_from_parameter(char* input, float* dest, size_t len);

int vasprintf(char** str, const char* fmt, va_list ap);

int asprintf(char** str, const char* fmt, ...);

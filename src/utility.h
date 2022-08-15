#pragma once

/** @brief Maps a value x from a given range to another range
 *
 *  The input x is mapped from the range in_min and in_max
 *  in relation to the range out_min and out_max.
 *
 *  @return the mapped value
 */
int map(int x, int in_min, int in_max, int out_min, int out_max);

/**
 * @brief This function calculates the estimate batttery level in percent.
 *
 * To find the terms representing the polynominal discarge curve of the
 * battery an solver like https://arachnoid.com/polysolve/ can be used.
 *
 * @param array polynominal terms for the battery discharge curve
 * @param number of terms
 * @param voltage readings
 * @return battery level in percent
 */
float poly_battery_level(const double terms[], const size_t numterms, uint16_t voltage);

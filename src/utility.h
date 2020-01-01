#pragma once

/** @brief Maps a value x from a given range to another range
 *
 *  The input x is mapped from the range in_min and in_max
 *  in relation to the range out_min and out_max.
 *
 *  @return the mapped value
 */
int map(int x, int in_min, int in_max, int out_min, int out_max);

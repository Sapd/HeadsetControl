#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utility.h"

int map(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

unsigned int round_to_multiples(unsigned int number, unsigned int multiple)
{
    return ((number + (multiple / 2)) / multiple) * multiple;
}

int spline_battery_level(const int p[], const int v[], const size_t size, uint16_t voltage)
{
    int percent = 0;

    for (int i = 0; i < size; ++i) {
        // if >= then 100%
        if (voltage >= v[i]) {
            percent = p[i];
            break;
        }

        // if not last
        if (i < size - 1 && voltage >= v[i + 1]) {
            percent = p[i + 1] + (float)(voltage - v[i + 1]) / (float)(v[i] - v[i + 1]) * (p[i] - p[i + 1]);
            break;
        }
    }

    return percent;
}

float poly_battery_level(const double terms[], const size_t numterms, uint16_t voltage)
{
    double t       = 1;
    double percent = 0;
    for (int i = 0; i < numterms; i++) {
        percent += terms[i] * t;
        t *= voltage;
    }

    if (percent > 100)
        percent = 100;
    if (percent < 0)
        percent = 0;
    return percent;
}

size_t hexdump(char* out, size_t out_size, unsigned char* data, size_t data_size)
{
    size_t i;
    size_t used_buf = 0;
    int rc;
    for (i = 0; i < data_size; ++i) {
        if (used_buf >= out_size) {
            return 0;
        }
        rc = snprintf(out + used_buf, out_size - used_buf, "0x%02X ", data[i]);
        if (rc < 0) {
#ifdef _WIN32
            printf("hexdump formatting failed\n");
#elif __APPLE__
            printf("%s\n", strerror(errno));
#else //__linux__
            printf("%s\n", strerror_l(errno, NULL));
#endif
        } else {
            used_buf += rc;
        }
    }
    return i;
}

int get_byte_data_from_parameter(char* input, unsigned char* dest, size_t len)
{
    const char* delim = " ,{}\n\r";

    // For each token in the string, parse and store in buf[].
    char* token = strtok(input, delim);
    int i       = 0;
    while (token) {
        char* endptr;
        long int val = strtol(token, &endptr, 0);

        if (i >= len)
            return -1;

        dest[i++] = val;
        token     = strtok(NULL, delim);
    }

    return i;
}

int get_float_data_from_parameter(char* input, float* dest, size_t len)
{
    const char* delim = " ,{}\n\r";

    // For each token in the string, parse and store in buf[].
    char* token = strtok(input, delim);
    int i       = 0;
    while (token) {
        char* endptr;
        float val = strtof(token, &endptr);

        if (i >= len)
            return -1;

        dest[i++] = val;
        token     = strtok(NULL, delim);
    }

    return i;
}
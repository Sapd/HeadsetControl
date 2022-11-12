#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "utility.h"

int map(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
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

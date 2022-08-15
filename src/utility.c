#include <stdint.h>
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

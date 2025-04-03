#include <errno.h>
#include <limits.h>
#include <stdarg.h>
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

    // Make a copy of the input string to avoid modifying the original
    char* input_copy = strdup(input);
    if (input_copy == NULL) {
        // Memory allocation failed
        return -1;
    }

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

    // Make a copy of the input string to avoid modifying the original
    char* input_copy = strdup(input);
    if (input_copy == NULL) {
        // Memory allocation failed
        return -1;
    }

    // For each token in the string, parse and store in dest[].
    char* token = strtok(input_copy, delim);
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

/**
 * Converts a filter type string to the corresponding EqualizerFilterType enum
 * Returns HSC_INVALID_ARG if the string does not match a known filter type.
 */
static EqualizerFilterType parse_eq_filter_type(const char* input)
{
    for (int i = 0; i < NUM_EQ_FILTER_TYPES; i++) {
        if (strcmp(input, equalizer_filter_type_str[i]) == 0) {
            return i;
        }
    }

    return HSC_INVALID_ARG;
}

/**
 * Parses a equalizer band string into a struct parametric_equalizer_band.
 *
 * Expected band_str format: "float,float,float,string"
 * Expected fields:          "frequency,gain,q-factor,filter-type"
 *
 * Returns HSC_INVALID_ARG if the string can't be parsed.
 */
static int parse_parametric_equalizer_band(const char* band_str, struct parametric_equalizer_band* out_band)
{
    const char* delim = " ,";

    // Make a modifiable copy of input, because strtok modifies the string.
    char* tmp = strdup(band_str);
    if (!tmp) {
        return -1;
    }

    // parse freq, gain, q_factor, type
    char* token = strtok(tmp, delim);
    if (!token) {
        free(tmp);
        return HSC_INVALID_ARG;
    }
    out_band->frequency = strtof(token, NULL);

    token = strtok(NULL, delim);
    if (!token) {
        free(tmp);
        return HSC_INVALID_ARG;
    }
    out_band->gain = strtof(token, NULL);

    token = strtok(NULL, delim);
    if (!token) {
        free(tmp);
        return HSC_INVALID_ARG;
    }
    out_band->q_factor = strtof(token, NULL);

    token = strtok(NULL, delim);
    if (!token) {
        free(tmp);
        return HSC_INVALID_ARG;
    }

    out_band->type = parse_eq_filter_type(token);
    if ((int)out_band->type == HSC_INVALID_ARG) {
        printf("Couldn't parse filter type: %s\n", token);
        free(tmp);
        return HSC_INVALID_ARG;
    }

    free(tmp);
    return 0;
}

/**
 * Parses the full parametric equalizer string that can contain multiple band
 * definitions separated by ';' into a parametric_equalizer_settings object.
 *
 * Example of input format:
 *     "100.0,3.5,1.0,lowshelf;500.0,-2.0,1.2,peaking;2000.0,5.0,0.7,highshelf"
 */
struct parametric_equalizer_settings* parse_parametric_equalizer_settings(const char* input)
{
    struct parametric_equalizer_settings* settings = malloc(sizeof(struct parametric_equalizer_settings));
    settings->size                                 = 0;
    settings->bands                                = NULL;

    if (!input || !*input || (strcmp(input, "reset") == 0)) {
        // Return empty if null/empty or reset of bands requested.
        // The device implementation is responsible for resetting
        // all remaining bands that are not provided by the user (all in this case).
        return settings;
    }

    // create a modifiable copy of the input
    char* input_copy = strdup(input);
    if (!input_copy) {
        return settings;
    }

    // Count how many bands we have by counting the number of semicolons + 1
    int band_count = 1;
    for (const char* p = input; *p; ++p) {
        if (*p == ';') {
            band_count++;
        }
    }

    // Allocate space for bands
    struct parametric_equalizer_band* bands = (struct parametric_equalizer_band*)calloc(band_count, sizeof(struct parametric_equalizer_band));

    if (!bands) {
        free(input_copy);
        return settings;
    }

    // Tokenize by ';' and parse each band definition
    char* context  = NULL;
    char* band_str = strtok_r(input_copy, ";", &context);
    int i          = 0;
    while (band_str && i < band_count) {
        parse_parametric_equalizer_band(band_str, &bands[i++]);
        band_str = strtok_r(NULL, ";", &context);
    }

    settings->size  = i;
    settings->bands = bands;

    free(input_copy);
    return settings;
}

int get_two_ids(char* input, int* id1, int* id2)
{
    const char* delim = " :.,";

    size_t sz = strlen(input);
    char* str = (char*)malloc(sz + 1);
    strcpy(str, input);

    int v1, v2;

    char* token = strtok(input, delim);
    int i       = 0;
    while (token) {
        char* endptr;
        long int val = strtol(token, &endptr, 0);

        if (i == 0)
            v1 = val;
        else if (i == 1)
            v2 = val;

        i++;
        token = strtok(NULL, delim);
    }

    free(str);

    if (i != 2) // not exactly supplied two ids
        return 1;

    *id1 = v1;
    *id2 = v2;

    return 0;
}

// ----------------- asprintf / vasprintf -----------------
/*
 * Copyright (c) 2004 Darren Tucker.
 *
 * Based originally on asprintf.c from OpenBSD:
 * Copyright (c) 1997 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef VA_COPY
#ifdef HAVE_VA_COPY
#define VA_COPY(dest, src) va_copy(dest, src)
#else
#ifdef HAVE___VA_COPY
#define VA_COPY(dest, src) __va_copy(dest, src)
#else
#define VA_COPY(dest, src) (dest) = (src)
#endif
#endif
#endif

#define INIT_SZ 128

int vasprintf(char** str, const char* fmt, va_list ap)
{
    int ret;
    va_list ap2;
    char *string, *newstr;
    size_t len;

    if ((string = malloc(INIT_SZ)) == NULL)
        goto fail;

    VA_COPY(ap2, ap);
    ret = vsnprintf(string, INIT_SZ, fmt, ap2);
    va_end(ap2);
    if (ret >= 0 && ret < INIT_SZ) { /* succeeded with initial alloc */
        *str = string;
    } else if (ret == INT_MAX || ret < 0) { /* Bad length */
        free(string);
        goto fail;
    } else { /* bigger than initial, realloc allowing for nul */
        len = (size_t)ret + 1;
        if ((newstr = realloc(string, len)) == NULL) {
            free(string);
            goto fail;
        }
        VA_COPY(ap2, ap);
        ret = vsnprintf(newstr, len, fmt, ap2);
        va_end(ap2);
        if (ret < 0 || (size_t)ret >= len) { /* failed with realloc'ed string */
            free(newstr);
            goto fail;
        }
        *str = newstr;
    }
    return (ret);

fail:
    *str  = NULL;
    errno = ENOMEM;
    return (-1);
}

int _asprintf(char** str, const char* fmt, ...)
{
    va_list ap;
    int ret;

    *str = NULL;
    va_start(ap, fmt);
    ret = vasprintf(str, fmt, ap);
    va_end(ap);

    return ret;
}

// ----------------- -------------------- -----------------

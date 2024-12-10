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

int get_two_ids(char* input, int* id1, int* id2)
{
    const char* delim = " :.,";

    size_t sz = strlen(input);
    char* str = (char*)malloc(sz + 1);
    strcpy(str, input);

    char* token = strtok(input, delim);
    int i       = 0;
    while (token) {
        char* endptr;
        long int val = strtol(token, &endptr, 0);

        if (i == 0)
            *id1 = val;
        else if (i == 1)
            *id2 = val;

        i++;
        token = strtok(NULL, delim);
    }

    free(str);

    if (i != 2) // not exactly supplied two ids
        return 1;

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

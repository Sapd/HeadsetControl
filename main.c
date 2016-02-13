/***
    Copyright (C) 2016 Denis Arnst (Sapd) <https://github.com/Sapd>

    This file is part of G930Sidetone.

    G930Sidetone is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    G930Sidetone is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with G930Sidetone.  If not, see <http://www.gnu.org/licenses/>.
***/


#include <libusb-1.0/libusb.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define VENDOR_LOGITECH 0x046d
#define PRODUCT_G930    0x0a1f

static libusb_device **devices = NULL;
static libusb_context *ctx = NULL;
static struct libusb_device_handle *handle = NULL;

static int init_libusb()
{
    int errno;

    errno = libusb_init(&ctx);

    if (errno < 0)
    {
        fprintf(stderr, "Failed initializing libusb %s", libusb_error_name(errno));
        return errno;
    }

    libusb_set_debug(ctx, 3);

    return 0;
}

static void free_libusb()
{
    if (devices)
        libusb_free_device_list(devices, 1);

    libusb_exit(ctx);
}

static int open_device(libusb_device* device)
{
    int errno;
    errno = libusb_open(device, &handle);

    if (errno < 0)
    {
        fprintf(stderr, "Failed to open USB device %s\n", libusb_error_name(errno));
        return 1;
    }

    printf("Opened USB handle\n");

    libusb_set_auto_detach_kernel_driver(handle, 1);
    if (errno < 0)
    {
        fprintf(stderr, "Failed to call auto detach USB device %s\n", libusb_error_name(errno));
        return 1;
    }

    errno = libusb_claim_interface(handle, 3);

    if (errno < 0)
    {
        fprintf(stderr, "Failed to claim USB interface %s\n", libusb_error_name(errno));
        return 1;
    }

    printf("Detached and claimed device\n");

    return 0;
}

static void close_device()
{
    libusb_release_interface(handle, 3);
    libusb_close(handle);
}

static libusb_device* find_logitech_g930()
{
    // return value
    libusb_device* r = NULL;

    int errno;

    // number of devices found
    ssize_t cnt;

    cnt = libusb_get_device_list(ctx, &devices);

    for (ssize_t i = 0; i < cnt; i++)
    {
        struct libusb_device_descriptor desc;
        errno = libusb_get_device_descriptor(devices[i], &desc);
        if (errno < 0)
        {
            fprintf(stderr, "Failed getting device descriptor of device %zd %s\n", i, libusb_error_name(errno));
        }

        if (desc.idVendor == VENDOR_LOGITECH && desc.idProduct == PRODUCT_G930)
        {
            r = devices[i];
            printf("Found Logitech G930!\n");
            break;
        }
    }

    return r;
}

void send_sidetone(unsigned char num)
{
    unsigned char data[64] = {0xFF, 0x0A, 0, 0xFF, 0xF4, 0x10, 0x05, 0xDA, 0x8F, 0xF2, 0x01, num, 0, 0, 0, 0};

    for (int i = 16; i < 64; i++)
        data[i] = 0;

    int size = libusb_control_transfer(handle, LIBUSB_DT_HID, LIBUSB_REQUEST_SET_CONFIGURATION, 0x03ff, 0x0003, data, 64, 1000);

    if (size > 0)
    {
        printf("Set Sidetone successfully! (%d bytes transferred)\n", size);
    }
    else
    {
        printf("Error in transfering data :(\n");
    }
}

int main(int argc, char *argv[])
{
    printf("G930Sidetone written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd\n");

    if (argc != 2)
    {
        goto usage;
    }

    int loudness = strtol(argv[1], NULL, 10);
    if (loudness > 128 || loudness < 0)
        goto usage;


    if (init_libusb())
        return 1;

    libusb_device* device = find_logitech_g930();
    if (!device)
    {
        fprintf(stderr, "Couldn't find Headset");
        free_libusb();
        return 1;
    }

    if (open_device(device))
    {
        fprintf(stderr, "Couldn't claim Device");
        close_device();
        free_libusb();
        return 1;
    }

    send_sidetone(loudness);

    close_device();
    free_libusb();

    return 0;

    usage:
    printf("Usage: %s 0-128\n\t(0 silent, 128 very loud)\n", argv[0]);
    return 0;
}


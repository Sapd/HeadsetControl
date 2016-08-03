/***
    Copyright (C) 2016 Denis Arnst (Sapd) <https://github.com/Sapd>

    This file is part of headsetcontrol.

    Headsetcontrol is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Headsetcontrol is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with headsetcontrol.  If not, see <http://www.gnu.org/licenses/>.
***/


#include <libusb-1.0/libusb.h>

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

enum devices
{
    DEVICE_G930,
    DEVICE_VOID
} device_found;

#define VENDOR_LOGITECH 0x046d
#define PRODUCT_G930    0x0a1f

#define VENDOR_CORSAIR  0x1b1c
#define PRODUCT_VOID    0x1b27

static libusb_device **devices = NULL;
static libusb_context *usb_context = NULL;
static struct libusb_device_handle *device_handle = NULL;

int map(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static int init_libusb()
{
    int errno;

    errno = libusb_init(&usb_context);

    if (errno < 0)
    {
        printf("Failed initializing libusb %s", libusb_error_name(errno));
        return errno;
    }

    libusb_set_debug(usb_context, 3);

    return 0;
}

static void free_libusb()
{
    libusb_exit(usb_context);
}

static libusb_device* find_device()
{
    // return value
    libusb_device* r = NULL;

    int errno;

    // number of devices found
    ssize_t cnt;

    cnt = libusb_get_device_list(usb_context, &devices);

    for (ssize_t i = 0; i < cnt; i++)
    {
        struct libusb_device_descriptor desc;
        errno = libusb_get_device_descriptor(devices[i], &desc);
        if (errno < 0)
        {
            printf("Failed getting device descriptor of device %zd %s\n", i, libusb_error_name(errno));
        }

        if (desc.idVendor == VENDOR_LOGITECH && desc.idProduct == PRODUCT_G930)
        {
            r = devices[i];
            printf("Found Logitech G930!\n");
            device_found = DEVICE_G930;
            break;
        }
        else if (desc.idVendor == VENDOR_CORSAIR && desc.idProduct == PRODUCT_VOID)
        {
            r = devices[i];
            printf("Found Corsair VOID!\n");
            device_found = DEVICE_VOID;
            break;
        }
    }

    return r;
}

static int open_device(libusb_device* device)
{
    int errno;
    errno = libusb_open(device, &device_handle);

    if (errno < 0)
    {
        fprintf(stderr, "Failed to open USB device %s\n", libusb_error_name(errno));
        return 1;
    }

    errno = libusb_set_auto_detach_kernel_driver(device_handle, 1);
    if (errno < 0)
    {
        printf("Failed to call auto detach USB device %s\n", libusb_error_name(errno));
        return 1;
    }

    errno = libusb_claim_interface(device_handle, 3);

    if (errno < 0)
    {
        printf("Failed to claim USB interface %s\n", libusb_error_name(errno));
        return 1;
    }

    return 0;
}

static void close_device()
{
    if (libusb_release_interface(device_handle, 3) != 0)
        printf("Failed releasing interface\n");

    libusb_close(device_handle);
}

void send_sidetone_g930(unsigned char num)
{
    int i;
    unsigned char data[64] = {0xFF, 0x0A, 0, 0xFF, 0xF4, 0x10, 0x05, 0xDA, 0x8F, 0xF2, 0x01, num, 0, 0, 0, 0};

    for (i = 16; i < 64; i++)
        data[i] = 0;

    int size = libusb_control_transfer(device_handle, LIBUSB_DT_HID, LIBUSB_REQUEST_SET_CONFIGURATION, 0x03ff, 0x0003, data, 64, 1000);

    if (size > 0)
    {
        printf("Set Sidetone successfully! (%d bytes transferred)\n", size);
    }
    else
    {
        printf("Error in transfering data :(\n");
    }
}

void send_sidetone_void(unsigned char num)
{
    // the range of the void seems to be from 200 to 255
    num = map(num, 0, 128, 200, 255);

    unsigned char data[64] = {0xFF, 0x0B, 0, 0xFF, 0x04, 0x0E, 0xFF, 0x05, 0x01, 0x04, 0x00, num, 0, 0, 0, 0};

    for (int i = 16; i < 64; i++)
        data[i] = 0;

    int size = libusb_control_transfer(device_handle, LIBUSB_DT_HID, LIBUSB_REQUEST_SET_CONFIGURATION, 0x03ff, 0x0003, data, 64, 1000);

    if (size > 0)
    {
        printf("Set Sidetone successfully! (%d bytes transferred)\n", size);
    }
    else
    {
        printf("Error in transfering data :(\n");
    }
}

void send_sidetone(unsigned char num)
{
    if (device_found == DEVICE_G930)
        return send_sidetone_g930(num);
    else if (device_found == DEVICE_VOID)
        return send_sidetone_void(num);

    assert(0);
}

int main(int argc, char *argv[])
{
    printf("Headsetcontrol written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd\n");

    if (argc != 2)
    {
        goto usage;
    }

    int loudness = strtol(argv[1], NULL, 10);
    if (loudness > 128 || loudness < 0)
        goto usage;


    if (init_libusb())
        return 1;

    libusb_device* device = find_device();
    if (!device)
    {
        printf("Couldn't find device");
        free_libusb();
        return 1;
    }

    if (open_device(device))
    {
        printf("Couldn't claim Device");
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

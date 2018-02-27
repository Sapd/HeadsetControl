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
    DEVICE_G430,    
    DEVICE_G533,
    DEVICE_VOID
} device_found;

#define VENDOR_LOGITECH 0x046d
#define PRODUCT_G930    0x0a1f
#define PRODUCT_G430    0x0a4d
#define PRODUCT_G533	0x0a66

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
        else if (desc.idVendor == VENDOR_LOGITECH && desc.idProduct == PRODUCT_G430)
        {
            r = devices[i];
            printf("Found Logitech G430!\n");
            device_found = DEVICE_G430;
            break;
        }
        else if (desc.idVendor == VENDOR_LOGITECH && desc.idProduct == PRODUCT_G533)
        {
          r = devices[i];
          printf("Found Logitech G533!\n");
          device_found = DEVICE_G533;
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

    #ifndef __APPLE__
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
    #endif

    return 0;
}

static void close_device()
{
    #ifndef __APPLE__
    if (libusb_release_interface(device_handle, 3) != 0)
        printf("Failed releasing interface\n");
    #endif

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

void send_sidetone_g430(unsigned char num)
{
    // Values for volumes 0 - 100 were taken from a USB data capture. Values for 101-128 were invented, but they work :)
    uint16_t volumes[129] = {
        0x00F2, 0x9EF9, 0x9CFD, 0x5500, 0x6702, 0x1304, 0x7A05, 0xAE06, 0xBD07, 0xAF08, 0x8909, 0x4F0A, 0x050B, 0xAE0B,
        0x4A0C, 0xDD0C, 0x660D, 0xE70D, 0x610E, 0xD50E, 0x430F, 0xAC0F, 0x1010, 0x7010, 0xCB10, 0x2411, 0x7911, 0xCA11,
        0x1912, 0x6512, 0xAF12, 0xF612, 0x3B13, 0x7E13, 0xBF13, 0xFE13, 0x3C14, 0x7714, 0xB214, 0xEA14, 0x2215, 0x5715,
        0x8C15, 0xC015, 0xF215, 0x2316, 0x5316, 0x8216, 0xB016, 0xDE16, 0x0A17, 0x3517, 0x6017, 0x6017, 0xB317, 0xDB17,
        0x0318, 0x2918, 0x5018, 0x5018, 0x9A18, 0xBE18, 0xE218, 0x0519, 0x2819, 0x4A19, 0x6C19, 0x8D19, 0xAD19, 0xCD19,
        0xED19, 0x0C1A, 0x2B1A, 0x4A1A, 0x671A, 0x851A, 0xA21A, 0xBF1A, 0xDB1A, 0xF81A, 0x131B, 0x2F1B, 0x4A1B, 0x641B,
        0x7F1B, 0x991B, 0xB31B, 0xCC1B, 0xE51B, 0xFE1B, 0x171C, 0x2F1C, 0x471C, 0x5F1C, 0x771C, 0x8E1C, 0xA51C, 0xBC1C,
        0xD31C, 0xE91C, 0x001D, 0x161D, 0x2C1D, 0x421D, 0x581D, 0x6E1D, 0x841D, 0x9A1D, 0xB01D, 0xC61D, 0xDC1D, 0xF21D,
        0x081E, 0x1E1E, 0x341E, 0x4A1E, 0x601E, 0x761E, 0x8C1E, 0xA21E, 0xB81E, 0xCE1E, 0xE41E, 0xFA1E, 0x101F, 0x261F,
        0x3C1F, 0x521F, 0x681F };

    unsigned char data[2] = { volumes[num] >> 8, volumes[num] & 0xFF };
    int size = libusb_control_transfer(device_handle, LIBUSB_DT_HID, LIBUSB_REQUEST_CLEAR_FEATURE, 0x0201, 0x0600, data, 0x2, 1000);

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
    else if (device_found == DEVICE_G430)
        return send_sidetone_g430(num);
    else if (device_found == DEVICE_VOID || DEVICE_G533)
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

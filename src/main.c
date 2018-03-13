/***
    Copyright (C) 2016-2018 Denis Arnst (Sapd) <https://github.com/Sapd>

    This file is part of HeadsetControl.

    HeadsetControl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    HeadsetControl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with HeadsetControl.  If not, see <http://www.gnu.org/licenses/>.
***/

#include "device_registry.h"

#include <libusb.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


// describes the device, when a headset was found
static struct device device_found;

// libusb handlers
static libusb_device **devices = NULL;
static libusb_context *usb_context = NULL;
static struct libusb_device_handle *device_handle = NULL;

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

/**
 *  This function iterates through all USB devices.
 *  When a supported device is found device_found is set
 */
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
        
        errno = get_device(&device_found, desc.idVendor, desc.idProduct);
        
        if (errno == 0)
        {
            printf("Found %s!\n", device_found.device_name);
            r = devices[i];
            return r;
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

    // The headsets are usally already claimed by the HID drivers
    // As these are drivers shipped with the OS, we can't override it
    // Luckily, Darwin doesn't require it (But Linux does!)
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

int main(int argc, char *argv[])
{
    printf("Headsetcontrol written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd\n\n");

    int c;
    
    int sidetone_loudness = -1;
    
    while ((c = getopt(argc, argv, "hs:")) != -1)
    {
        switch(c) {
            case 's':
                sidetone_loudness = strtol(optarg, NULL, 10);
                
                if (sidetone_loudness > 128 || sidetone_loudness < 0)
                {
                    printf("Usage: %s -s 0-100\n", argv[0]);
                    return 1;
                }
                break;
            case 'h':
                printf("Parameters\n");
                printf("  -s level\tSets sidetone, level must be between 0 and 128\n");
                
                printf("\n");
                return 0;
            default:
                printf("Invalid argument %c (Programming error)\n", c);
                return 1;
        }
        
    }

    for (int index = optind; index < argc; index++)
        printf ("Non-option argument %s\n", argv[index]);

    // Init all information of supported devices
    init_devices();
    
    if (init_libusb())
        return 1;

    // Look for a supported device
    libusb_device* device = find_device();
    if (!device)
    {
        printf("No supported headset found\n");
        free_libusb();
        return 1;
    }
    printf("\n");

    // Check wether we support the feature the user wants to set for this device
    if (sidetone_loudness != -1 && (device_found.capabilities & CAP_SIDETONE) == 0)
    {
        printf("Error: This headset doesn't support sidetone\n");
        return 1;
    }
    
    // Open libusb device
    if (open_device(device))
    {
        printf("Couldn't claim device\n");
        close_device();
        free_libusb();
        return 1;
    }

    int error;
    // Set all features the user wants us to set
    if (sidetone_loudness != -1)
    {
        error = device_found.send_sidetone(device_handle, sidetone_loudness);
        
        if (error < 0)
        {
            printf("Failed to set sidetone. Error: %d: %s\n", error, libusb_error_name(error));
            return 1;
        }
        else
        {
            printf("Set Sidetone successfully!\n");
        }
    }
    else
    {
        printf("You didn't set any arguments, so nothing happend.\nType %s -h for help.\n", argv[0]);
    }
    
    // cleanup
    close_device();
    free_libusb();

    return 0;
}

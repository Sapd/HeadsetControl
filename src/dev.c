#include "dev.h"

#include "hid_utility.h"

#include "utility.h"

#include <hidapi.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Print information of devices
 *
 * When vendorid and productid are 0, then all are listed.
 * If they are specified, information about all usagepages and interfaces of the respective device are listed
 *
 * @param vendorid
 * @param productid
 */
static void print_devices(unsigned short vendorid, unsigned short productid)
{
    struct hid_device_info *devs, *cur_dev;

    devs    = hid_enumerate(vendorid, productid);
    cur_dev = devs;
    while (cur_dev) {
        printf("Device Found\n  VendorID: %#06hx\n ProductID: %#06hx\n  path: %s\n  serial_number: %ls",
            cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
        printf("\n");
        printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
        printf("  Product:      %ls\n", cur_dev->product_string);
        printf("  Interface:    %d\n", cur_dev->interface_number);
        printf("  Usage-Page: 0x%hx Usageid: 0x%hx\n", cur_dev->usage_page, cur_dev->usage);
        printf("\n");
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);
}

/**
 * @brief check if number inside range
 *
 * @param number to check
 * @param low lower bound
 * @param high upper bound
 * @return int 0 if in range; 1 if too high; -1 if too low
 */

int check_range(int number, int low, int high)
{
    if (number > high)
        return 1;

    if (number < low)
        return -1;

    return 0;
}

static void print_help()
{
    printf("HeadsetControl Developer menu. Take caution.\n\n");

    printf("Usage\n");
    printf("  headsetcontrol --dev -- PARAMETERS\n");
    printf("\n");

    printf("Parameters\n");
    printf("  --list\n"
           "\tLists all HID devices when used without --device\n"
           "\tWhen used with --device, searches for a specific device, and prints all interfaces and usageids\n");
    printf("  --device VENDORID:PRODUCTID\n"
           "\te.g. --device 1234:5678 or --device 0x4D2:0x162E\n"
           "\tRequired for most parameters\n");
    printf("  --interface INTERFACEID\n"
           "\tWhich interface of the device to use. 0 is default\n");
    printf("  --usage USAGEPAGE:USAGEID\n"
           "\tSpecifies an usage-page and usageid. 0:0 is default\n"
           "\tImportant for Windows. Ignored on Mac/Linux\n");
    printf("\n");

    printf("  --send DATA\n"
           "\tSend data to specified device\n");
    printf("  --send-feature DATA\n"
           "\tSend a feature report to the device. The first byte is the reportid\n");
    printf("  --sleep microsecounds\n"
           "\tSleep for x microsecounds after sending\n");
    printf("  --receive\n"
           "\tTry to receive data from device. Can be combined with --timeout\n");
    printf("  --timeout\n"
           "\t--timeout in millisecounds for --receive\n");
    printf("  --receive-feature REPORTID\n"
           "\tTry to receive a report for REPORTID.\n");
    printf("  --repeat SECS\n"
           "\tRepeat command every SECS.\n");
    printf("\n");

    printf("  --dev-help\n"
           "\tThis menu\n");
    printf("\n");

    printf("HINTS\n");
    printf("\tsend and receive can be combined\n");
    printf("\t--send does not return anything on success and is always executed first\n");
    printf("\tDATA can be specified as single bytes, either as decimal or hex, separated by spaces or commas or newlines\n");
    printf("\n");

    printf("EXAMPLEs\n");
    printf("  headsetcontrol --dev -- --list\n");
    printf("  headsetcontrol --dev -- --list --device 0x1b1c:0x1b27\n");
    printf("  headsetcontrol --dev -- --device 0x1b1c:0x1b27 --send \"0xC9, 0x64\" --receive --timeout 10\n");
}

int dev_main(int argc, char* argv[])
{
    int vendorid    = 0;
    int productid   = 0;
    int interfaceid = 0;
    int usagepage   = 0;
    int usageid     = 0;

    int send         = 0;
    int send_feature = 0;

    int sleep_time = -1;

    int receive       = 0;
    int receivereport = 0;

    int timeout = -1;

    int repeat_seconds = 0;

    int print_deviceinfo = 0;

#define BUFFERLENGTH 1024
    unsigned char* sendbuffer       = calloc(BUFFERLENGTH, sizeof(char));
    unsigned char* sendreportbuffer = calloc(BUFFERLENGTH, sizeof(char));

    unsigned char* receivebuffer       = malloc(sizeof(char) * BUFFERLENGTH);
    unsigned char* receivereportbuffer = malloc(sizeof(char) * BUFFERLENGTH);

    struct option opts[] = {
        { "device", required_argument, NULL, 'd' },
        { "interface", required_argument, NULL, 'i' },
        { "usage", required_argument, NULL, 'u' },
        { "list", no_argument, NULL, 'l' },
        { "send", required_argument, NULL, 's' },
        { "send-feature", required_argument, NULL, 'f' },
        { "sleep", required_argument, NULL, 'm' },
        { "receive", no_argument, NULL, 'r' },
        { "receive-feature", required_argument, NULL, 'g' },
        { "timeout", required_argument, NULL, 't' },
        { "dev-help", no_argument, NULL, 'h' },
        { "repeat", required_argument, NULL, 0 },
        { 0, 0, 0, 0 }
    };

    int option_index = 0;

    optind = 1; // getopt_long requires resetting this variable, we already used it in main()

    int c;
    while ((c = getopt_long(argc, argv, "d:i:lu:s:m:f:rg:th", opts, &option_index)) != -1) {
        switch (c) {
        case 'd': { // --device vendorid:productid
            int ret = get_two_ids(optarg, &vendorid, &productid);
            if (ret) {
                fprintf(stderr, "You must supply a vendorid:productid pair in the --device / d parameter.\n\tE.g. --device 1234:5678 or --device 0x4D2:0x162E\n");
                return 1;
            }

            if (check_range(vendorid, 1, 65535) != 0 || check_range(productid, 1, 65535) != 0) {
                fprintf(stderr, "Vendor and Productid must be between 1 and 65535 or 0x1 and 0xffff\n");
                return 1;
            }

            break;
        }
        case 'i': { // --interface interfaceid
            interfaceid = strtol(optarg, NULL, 0);

            if (interfaceid < 0) {
                fprintf(stderr, "The interfaceid you supplied is invalid\n");
                return 1;
            }

            break;
        }
        case 'u': { // --usage usagepage:usageid
            int ret = get_two_ids(optarg, &usagepage, &usageid);
            if (ret) {
                fprintf(stderr, "You must supply a usagepage:usageid pair in the --usage / u parameter.\n\tE.g. --usage 1234:5678 or --usage 0x4D2:0x162E\n");
                return 1;
            }

            if (check_range(vendorid, 1, 65535) != 0 || check_range(productid, 1, 65535) != 0) {
                fprintf(stderr, "Usagepage and Usageid must be between 1 and 65535 or 0x1 and 0xffff\n");
                return 1;
            }

            break;
        }
        case 's': { // --send string
            int size = get_byte_data_from_parameter(optarg, sendbuffer, BUFFERLENGTH);

            if (size < 0) {
                fprintf(stderr, "Data to send larger than %d\n", BUFFERLENGTH);
                return 1;
            }

            if (size == 0) {
                fprintf(stderr, "No data specified to --send\n");
                return 1;
            }

            send = size;

            break;
        }
        case 'f': { // --send-feature string
            int size = get_byte_data_from_parameter(optarg, sendreportbuffer, BUFFERLENGTH);

            if (size < 0) {
                fprintf(stderr, "Data to send for feature report larger than %d\n", BUFFERLENGTH);
                return 1;
            }

            if (size == 0) {
                fprintf(stderr, "No data specified to --send-feature\n");
                return 1;
            }

            send_feature = size;

            break;
        }
        case 'm': { // --sleep
            sleep_time = strtol(optarg, NULL, 10);

            if (sleep_time < 0) {
                fprintf(stderr, "--sleep must be positive\n");
                return 1;
            }

            break;
        }
        case 'r': { // --receive
            receive = 1;
            break;
        }
        case 'g': { // --receive-feature [reportid]
            int reportid = 0;
            reportid     = strtol(optarg, NULL, 10);

            if (reportid > 255 || reportid < 0) {
                fprintf(stderr, "The reportid for --receive-feature must be smaller than 255\n");
                return 1;
            }

            // the first byte of the receivereport buffer must be set for hidapi
            receivereportbuffer[0] = reportid;

            break;
        }
        case 't': { // --timeout timeout
            timeout = strtol(optarg, NULL, 10);

            if (timeout < -1) {
                fprintf(stderr, "--timeout cannot be smaller than -1\n");
                return 1;
            }
            break;
        }
        case 0: {
            if (strcmp(opts[option_index].name, "repeat") == 0) { // --repeat SECS
                repeat_seconds = strtol(optarg, NULL, 10);

                if (repeat_seconds < 1) {
                    fprintf(stderr, "--repeat SECS cannot be smaller than 1\n");
                    return 1;
                }
            }
            break;
        }
        case 'l': { // --list [vendorid:productid]
            print_deviceinfo = 1;
            break;
        }
        case 'h': {
            print_help();
            break;
        }
        default:
            printf("Invalid argument %c\n", c);
        }
    }

    if (argc <= 1)
        print_help();

    if (print_deviceinfo)
        print_devices(vendorid, productid);

    if (!(send || send_feature || receive || receivereport))
        goto cleanup;

    if (!vendorid || !productid) {
        fprintf(stderr, "You must supply a vendor/productid pair via the parameter --device\n");
        return 1;
    }

    char* hid_path            = get_hid_path(vendorid, productid, interfaceid, usagepage, usageid);
    hid_device* device_handle = NULL;

    if (hid_path == NULL) {
        fprintf(stderr, "Could not find a device with this parameters:\n");
        fprintf(stderr, "\t Vendor (%#x) Product (%#x) Interface (%#x) UsagePage (%#x) UsageID (%#x)\n",
            vendorid, productid, interfaceid, usagepage, usageid);
    }

    if (send && send_feature) {
        fprintf(stderr, "Warning: --send and --send-feature specified at the same time\n");
    }

    if (receive && receivereport) {
        fprintf(stderr, "Warning: --receive and --receive-feature specified at the same time\n");
    }

    device_handle = hid_open_path(hid_path);
    if (device_handle == NULL) {
        fprintf(stderr, "Couldn't open device.\n");
        terminate_hid(&device_handle, &hid_path);
        return 1;
    }

    do {
        if (send) {
            int ret = hid_write(device_handle, (const unsigned char*)sendbuffer, send);

            if (ret < 0) {
                fprintf(stderr, "Failed to send data. Error: %d: %ls\n", ret, hid_error(device_handle));
                terminate_hid(&device_handle, &hid_path);
                return 1;
            }
        }

        if (send_feature) {
            int ret = hid_send_feature_report(device_handle, (const unsigned char*)sendreportbuffer, send_feature);

            if (ret < 0) {
                fprintf(stderr, "Failed to send data. Error: %d: %ls\n", ret, hid_error(device_handle));
                terminate_hid(&device_handle, &hid_path);
                return 1;
            }
        }

        if (sleep_time >= 0) { // also allow 0 as a minimum sleep
            usleep(sleep_time * 1000);
        }

        if (receive) {
            int read = hid_read_timeout(device_handle, receivebuffer, BUFFERLENGTH, timeout);

            if (read < 0) {
                fprintf(stderr, "Failed to read. Error: %d: %ls\n", read, hid_error(device_handle));
                terminate_hid(&device_handle, &hid_path);
                return 1;
            } else if (read == 0) {
                fprintf(stderr, "No data to read\n");
            } else {
                for (int i = 0; i < read; i++) {
                    printf("%#04x ", receivebuffer[i]);
                }
                printf("\n");
            }
        }

        if (receivereport) {
            int read = hid_get_feature_report(device_handle, receivereportbuffer, BUFFERLENGTH);

            if (read < 0) {
                fprintf(stderr, "Failed to read. Error: %d: %ls\n", read, hid_error(device_handle));
                terminate_hid(&device_handle, &hid_path);
                return 1;
            } else if (read == 0) {
                fprintf(stderr, "No data to read\n");
            } else {
                for (int i = 0; i < read; i++) {
                    printf("%#04x ", receivereportbuffer[i]);
                }
                printf("\n");
            }
        }
        sleep(repeat_seconds);
    } while (repeat_seconds);

    terminate_hid(&device_handle, &hid_path);

cleanup:

    free(receivereportbuffer);
    free(receivebuffer);
    free(sendreportbuffer);
    free(sendbuffer);

    return 0;
}

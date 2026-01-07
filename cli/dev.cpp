#include "dev.hpp"

#include "hid_utility.hpp"
#include "string_utils.hpp"
#include "utility.hpp"

#include <hidapi.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

// ============================================================================
// RAII Wrappers
// ============================================================================

struct HIDDeviceDeleter {
    void operator()(hid_device* dev) const noexcept
    {
        if (dev) {
            headsetcontrol::close_hid_device(dev);
        }
    }
};
using HIDDevicePtr = std::unique_ptr<hid_device, HIDDeviceDeleter>;

struct HIDEnumerationDeleter {
    void operator()(hid_device_info* info) const noexcept
    {
        if (info) {
            hid_free_enumeration(info);
        }
    }
};
using HIDEnumerationPtr = std::unique_ptr<hid_device_info, HIDEnumerationDeleter>;

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] constexpr bool in_range(int value, int min, int max) noexcept
{
    return value >= min && value <= max;
}

[[nodiscard]] std::optional<int> parse_int(std::string_view str) noexcept
{
    int value      = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
    return ec == std::errc() ? std::optional { value } : std::nullopt;
}

void print_hex_bytes(std::span<const uint8_t> data)
{
    for (auto byte : data) {
        std::cout << std::format("{:#04x} ", byte);
    }
    std::cout << '\n';
}

void print_devices(uint16_t vendorid, uint16_t productid)
{
    HIDEnumerationPtr devs { hid_enumerate(vendorid, productid) };

    for (auto* cur = devs.get(); cur != nullptr; cur = cur->next) {
        std::cout << std::format(
            "Device Found\n"
            "  VendorID:  {:#06x}\n"
            "  ProductID: {:#06x}\n"
            "  Path: {}\n",
            cur->vendor_id, cur->product_id, cur->path);

        if (cur->serial_number) {
            std::cout << "  Serial: " << headsetcontrol::wstring_to_string(cur->serial_number) << '\n';
        }
        if (cur->manufacturer_string) {
            std::cout << "  Manufacturer: " << headsetcontrol::wstring_to_string(cur->manufacturer_string) << '\n';
        }
        if (cur->product_string) {
            std::cout << "  Product: " << headsetcontrol::wstring_to_string(cur->product_string) << '\n';
        }

        std::cout << std::format(
            "  Interface: {}\n"
            "  Usage-Page: {:#06x}  Usage-ID: {:#06x}\n\n",
            cur->interface_number, cur->usage_page, cur->usage);
    }
}

constexpr std::string_view HELP_TEXT = R"(HeadsetControl Developer menu. Take caution.

Usage
  headsetcontrol --dev -- PARAMETERS

Parameters
  --list
        Lists all HID devices when used without --device
        When used with --device, searches for a specific device, and prints all interfaces and usageids
  --device VENDORID:PRODUCTID
        e.g. --device 1234:5678 or --device 0x4D2:0x162E
        Required for most parameters
  --interface INTERFACEID
        Which interface of the device to use. 0 is default
  --usage USAGEPAGE:USAGEID
        Specifies an usage-page and usageid. 0:0 is default
        Important for Windows. Ignored on Mac/Linux

  --send DATA
        Send data to specified device
  --send-feature DATA
        Send a feature report to the device. The first byte is the reportid
  --sleep MILLISECONDS
        Sleep for x milliseconds after sending
  --receive
        Try to receive data from device. Can be combined with --timeout
  --timeout MILLISECONDS
        Timeout in milliseconds for --receive
  --receive-feature REPORTID
        Try to receive a report for REPORTID.
  --repeat SECS
        Repeat command every SECS seconds.

  --dev-help
        This menu

HINTS
  - send and receive can be combined
  - --send does not return anything on success and is always executed first
  - DATA can be specified as single bytes, either decimal or hex, separated by spaces or commas

EXAMPLES
  headsetcontrol --dev -- --list
  headsetcontrol --dev -- --list --device 0x1b1c:0x1b27
  headsetcontrol --dev -- --device 0x1b1c:0x1b27 --send "0xC9, 0x64" --receive --timeout 100
)";

// ============================================================================
// Command Line Options
// ============================================================================

struct DevOptions {
    // Device identification
    uint16_t vendorid  = 0;
    uint16_t productid = 0;
    int interfaceid    = 0;
    uint16_t usagepage = 0;
    uint16_t usageid   = 0;

    // Actions
    std::vector<uint8_t> send_data;
    std::vector<uint8_t> send_feature_data;
    std::optional<uint8_t> receive_report_id;
    bool do_receive       = false;
    bool print_deviceinfo = false;

    // Timing
    std::chrono::milliseconds sleep_time { 0 };
    std::chrono::milliseconds timeout { -1 };
    std::chrono::seconds repeat_interval { 0 };

    [[nodiscard]] constexpr bool has_send_action() const noexcept
    {
        return !send_data.empty() || !send_feature_data.empty();
    }

    [[nodiscard]] constexpr bool has_receive_action() const noexcept
    {
        return do_receive || receive_report_id.has_value();
    }

    [[nodiscard]] constexpr bool has_any_action() const noexcept
    {
        return has_send_action() || has_receive_action();
    }
};

[[nodiscard]] std::optional<DevOptions> parse_options(int argc, char* argv[])
{
    DevOptions opts;

    static const std::array long_opts {
        option { "device", required_argument, nullptr, 'd' },
        option { "interface", required_argument, nullptr, 'i' },
        option { "usage", required_argument, nullptr, 'u' },
        option { "list", no_argument, nullptr, 'l' },
        option { "send", required_argument, nullptr, 's' },
        option { "send-feature", required_argument, nullptr, 'f' },
        option { "sleep", required_argument, nullptr, 'm' },
        option { "receive", no_argument, nullptr, 'r' },
        option { "receive-feature", required_argument, nullptr, 'g' },
        option { "timeout", required_argument, nullptr, 't' },
        option { "dev-help", no_argument, nullptr, 'h' },
        option { "repeat", required_argument, nullptr, 'R' },
        option { nullptr, 0, nullptr, 0 },
    };

    optind           = 1;
    int option_index = 0;

    for (int c; (c = getopt_long(argc, argv, "d:i:lu:s:m:f:rg:t:hR:", long_opts.data(), &option_index)) != -1;) {
        switch (c) {
        case 'd': {
            auto ids = headsetcontrol::parse_two_ids(optarg);
            if (!ids || !in_range(ids->first, 1, 65535) || !in_range(ids->second, 1, 65535)) {
                std::cerr << "Invalid --device. Use format: VENDORID:PRODUCTID (1-65535 or 0x1-0xffff)\n"
                          << "  Example: --device 0x1b1c:0x1b27\n";
                return std::nullopt;
            }
            opts.vendorid  = static_cast<uint16_t>(ids->first);
            opts.productid = static_cast<uint16_t>(ids->second);
            break;
        }
        case 'i':
            if (auto v = parse_int(optarg); v && *v >= 0) {
                opts.interfaceid = *v;
            } else {
                std::cerr << "Invalid --interface. Must be a non-negative integer.\n";
                return std::nullopt;
            }
            break;

        case 'u': {
            auto ids = headsetcontrol::parse_two_ids(optarg);
            if (!ids || !in_range(ids->first, 0, 65535) || !in_range(ids->second, 0, 65535)) {
                std::cerr << "Invalid --usage. Use format: USAGEPAGE:USAGEID (0-65535)\n";
                return std::nullopt;
            }
            opts.usagepage = static_cast<uint16_t>(ids->first);
            opts.usageid   = static_cast<uint16_t>(ids->second);
            break;
        }
        case 'l':
            opts.print_deviceinfo = true;
            break;

        case 's':
            opts.send_data = headsetcontrol::parse_byte_data(optarg);
            if (opts.send_data.empty()) {
                std::cerr << "No data specified to --send\n";
                return std::nullopt;
            }
            break;

        case 'f':
            opts.send_feature_data = headsetcontrol::parse_byte_data(optarg);
            if (opts.send_feature_data.empty()) {
                std::cerr << "No data specified to --send-feature\n";
                return std::nullopt;
            }
            break;

        case 'm':
            if (auto v = parse_int(optarg); v && *v >= 0) {
                opts.sleep_time = std::chrono::milliseconds { *v };
            } else {
                std::cerr << "--sleep must be a non-negative integer (milliseconds)\n";
                return std::nullopt;
            }
            break;

        case 'r':
            opts.do_receive = true;
            break;

        case 'g':
            if (auto v = parse_int(optarg); v && in_range(*v, 0, 255)) {
                opts.receive_report_id = static_cast<uint8_t>(*v);
            } else {
                std::cerr << "--receive-feature REPORTID must be 0-255\n";
                return std::nullopt;
            }
            break;

        case 't':
            if (auto v = parse_int(optarg); v && *v >= -1) {
                opts.timeout = std::chrono::milliseconds { *v };
            } else {
                std::cerr << "--timeout must be >= -1 (milliseconds, -1 for infinite)\n";
                return std::nullopt;
            }
            break;

        case 'R':
            if (auto v = parse_int(optarg); v && *v >= 1) {
                opts.repeat_interval = std::chrono::seconds { *v };
            } else {
                std::cerr << "--repeat must be >= 1 (seconds)\n";
                return std::nullopt;
            }
            break;

        case 'h':
            std::cout << HELP_TEXT;
            std::exit(0);

        default:
            return std::nullopt;
        }
    }

    return opts;
}

// ============================================================================
// HID Operations
// ============================================================================

[[nodiscard]] bool send_data(hid_device* dev, std::span<const uint8_t> data)
{
    if (hid_write(dev, data.data(), data.size()) < 0) {
        std::cerr << std::format("Failed to send: {}\n",
            headsetcontrol::wstring_to_string(hid_error(dev)));
        return false;
    }
    return true;
}

[[nodiscard]] bool send_feature_report(hid_device* dev, std::span<const uint8_t> data)
{
    if (hid_send_feature_report(dev, data.data(), data.size()) < 0) {
        std::cerr << std::format("Failed to send feature report: {}\n",
            headsetcontrol::wstring_to_string(hid_error(dev)));
        return false;
    }
    return true;
}

[[nodiscard]] bool receive_data(hid_device* dev, std::span<uint8_t> buffer, int timeout_ms)
{
    int bytes = hid_read_timeout(dev, buffer.data(), buffer.size(), timeout_ms);
    if (bytes < 0) {
        std::cerr << std::format("Failed to read: {}\n",
            headsetcontrol::wstring_to_string(hid_error(dev)));
        return false;
    }
    if (bytes == 0) {
        std::cerr << "No data received (timeout)\n";
    } else {
        print_hex_bytes(buffer.first(static_cast<size_t>(bytes)));
    }
    return true;
}

[[nodiscard]] bool receive_feature_report(hid_device* dev, std::span<uint8_t> buffer, uint8_t report_id)
{
    buffer[0] = report_id;
    int bytes = hid_get_feature_report(dev, buffer.data(), buffer.size());
    if (bytes < 0) {
        std::cerr << std::format("Failed to get feature report: {}\n",
            headsetcontrol::wstring_to_string(hid_error(dev)));
        return false;
    }
    if (bytes == 0) {
        std::cerr << "No data received\n";
    } else {
        print_hex_bytes(buffer.first(static_cast<size_t>(bytes)));
    }
    return true;
}

} // namespace

int dev_main(int argc, char* argv[])
{
    if (argc <= 1) {
        std::cout << HELP_TEXT;
        return 0;
    }

    auto opts = parse_options(argc, argv);
    if (!opts) {
        return 1;
    }

    if (opts->print_deviceinfo) {
        print_devices(opts->vendorid, opts->productid);
    }

    if (!opts->has_any_action()) {
        return 0;
    }

    if (opts->vendorid == 0 || opts->productid == 0) {
        std::cerr << "You must supply --device VENDORID:PRODUCTID for send/receive operations\n";
        return 1;
    }

    auto hid_path = headsetcontrol::get_hid_path(
        opts->vendorid, opts->productid, opts->interfaceid, opts->usagepage, opts->usageid);

    if (!hid_path) {
        std::cerr << std::format(
            "Could not find device: Vendor={:#06x} Product={:#06x} Interface={} UsagePage={:#06x} UsageID={:#06x}\n",
            opts->vendorid, opts->productid, opts->interfaceid, opts->usagepage, opts->usageid);
        return 1;
    }

    if (!opts->send_data.empty() && !opts->send_feature_data.empty()) {
        std::cerr << "Warning: both --send and --send-feature specified\n";
    }
    if (opts->do_receive && opts->receive_report_id.has_value()) {
        std::cerr << "Warning: both --receive and --receive-feature specified\n";
    }

    HIDDevicePtr device { hid_open_path(hid_path->c_str()) };
    if (!device) {
        std::cerr << "Failed to open device\n";
        return 1;
    }

    constexpr size_t BUFFER_SIZE = 1024;
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    do {
        if (!opts->send_data.empty() && !send_data(device.get(), opts->send_data)) {
            return 1;
        }

        if (!opts->send_feature_data.empty() && !send_feature_report(device.get(), opts->send_feature_data)) {
            return 1;
        }

        if (opts->sleep_time.count() > 0) {
            std::this_thread::sleep_for(opts->sleep_time);
        }

        if (opts->do_receive && !receive_data(device.get(), buffer, static_cast<int>(opts->timeout.count()))) {
            return 1;
        }

        if (opts->receive_report_id.has_value() && !receive_feature_report(device.get(), buffer, *opts->receive_report_id)) {
            return 1;
        }

        if (opts->repeat_interval.count() > 0) {
            std::this_thread::sleep_for(opts->repeat_interval);
        }
    } while (opts->repeat_interval.count() > 0);

    return 0;
}

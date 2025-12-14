/***
    Copyright (C) 2016-2025 Denis Arnst (Sapd) <https://github.com/Sapd>

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

#include "argument_parser.hpp"
#include "capability_descriptors.hpp"
#include "dev.hpp"
#include "device.hpp"
#include "device_registry.hpp"
#include "devices/hid_device.hpp"
#include "feature_handlers.hpp"
#include "feature_utils.hpp"
#include "headsetcontrol.hpp"
#include "hid_utility.hpp"
#include "output.hpp"
#include "result_types.hpp"
#include "utility.hpp"
#include "version.h"

#include <hidapi.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// ============================================================================
// Namespace imports
// ============================================================================

using headsetcontrol::DeviceRegistry;
using headsetcontrol::HIDDevice;
using headsetcontrol::make_battery_result;
using headsetcontrol::make_error;
using headsetcontrol::make_success;

// Forward declaration for C++ device registry initialization
extern "C" void init_cpp_devices();

namespace {

volatile sig_atomic_t g_follow_running = false;

// ============================================================================
// Output helpers
// ============================================================================

template <typename... Args>
void print(std::format_string<Args...> fmt, Args&&... args)
{
    std::cout << std::format(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void println(std::format_string<Args...> fmt, Args&&... args)
{
    std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

inline void println()
{
    std::cout << '\n';
}

template <typename... Args>
void eprintln(std::format_string<Args...> fmt, Args&&... args)
{
    std::cerr << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

// ============================================================================
// Command-line options - Clean data structure
// ============================================================================

struct Options {
    // Device selection
    uint16_t vendor_id  = 0;
    uint16_t product_id = 0;

    // Mode flags
    bool show_help          = false;
    bool show_help_all      = false;
    bool show_version       = false;
    bool print_udev_rules   = false;
    bool print_capabilities = false;
    bool dev_mode           = false;
    bool test_device        = false;
    bool follow_mode        = false;
    bool request_connected  = false;
    unsigned follow_seconds = 2;

    // Output format
    OutputType output_format = OUTPUT_STANDARD;

    // Feature settings
    std::optional<uint8_t> sidetone_level;
    std::optional<uint8_t> notification_sound;
    std::optional<bool> lights_enabled;
    std::optional<uint8_t> inactive_time;
    std::optional<bool> voice_prompts_enabled;
    std::optional<bool> rotate_to_mute_enabled;
    std::optional<uint8_t> equalizer_preset;
    std::optional<uint8_t> mic_mute_led_brightness;
    std::optional<uint8_t> mic_volume;
    std::optional<bool> volume_limiter_enabled;
    std::optional<bool> bt_when_powered_on;
    std::optional<uint8_t> bt_call_volume;

    // Info requests
    bool request_battery = false;
    bool request_chatmix = false;

    // Complex settings
    std::optional<EqualizerSettings> equalizer;
    std::optional<ParametricEqualizerSettings> parametric_equalizer;

    // Helper
    [[nodiscard]] bool hasDeviceFilter() const
    {
        return vendor_id != 0 || product_id != 0;
    }

    [[nodiscard]] bool matchesDevice(uint16_t vid, uint16_t pid) const
    {
        return (vendor_id == 0 || vendor_id == vid) && (product_id == 0 || product_id == pid);
    }
};

// ============================================================================
// Argument parser configuration - Declarative option definitions
// ============================================================================

std::optional<cli::ParseError> configureParser(cli::ArgumentParser& parser, Options& opts)
{
    // Output format choices
    static const std::unordered_map<std::string, OutputType> output_formats = {
        { "JSON", OUTPUT_JSON },
        { "YAML", OUTPUT_YAML },
        { "ENV", OUTPUT_ENV },
        { "STANDARD", OUTPUT_STANDARD },
        { "SHORT", OUTPUT_SHORT }
    };

    parser
        // === Device Selection ===
        .custom('d', "device", cli::ArgRequirement::Required, [&opts](std::optional<std::string_view> arg) -> std::optional<cli::ParseError> {
                if (!arg)
                    return cli::ParseError { "requires vendor:product", "device" };
                auto ids = headsetcontrol::parse_two_ids(*arg);
                if (!ids) {
                    return cli::ParseError { "format: vendorid:productid", "device" };
                }
                opts.vendor_id = static_cast<uint16_t>(ids->first);
                opts.product_id = static_cast<uint16_t>(ids->second);
                return std::nullopt; }, "Select device by vendor:product ID")

        // === Help & Version ===
        .flag('h', "help", opts.show_help, "Show help message")
        .long_flag("help-all", opts.show_help_all, "Show all options including advanced")
        .long_flag("version", opts.show_version, "Show version information")

        // === Feature Controls ===
        .value('s', "sidetone", opts.sidetone_level, uint8_t(0), uint8_t(128), "Set sidetone level", "LEVEL")
        .flag('b', "battery", opts.request_battery, "Check battery level")
        .toggle('l', "light", opts.lights_enabled, "Turn lights off (0) or on (1)")
        .toggle('v', "voice-prompt", opts.voice_prompts_enabled, "Turn voice prompts off (0) or on (1)")
        .value('i', "inactive-time", opts.inactive_time, uint8_t(0), uint8_t(90), "Set inactive time in minutes", "MINUTES")
        .flag('m', "chatmix", opts.request_chatmix, "Get chat-mix level")
        .value('n', "notificate", opts.notification_sound, uint8_t(0), uint8_t(255), "Play notification sound", "SOUNDID")
        .toggle('r', "rotate-to-mute", opts.rotate_to_mute_enabled, "Toggle rotate to mute")
        .value('p', "equalizer-preset", opts.equalizer_preset, uint8_t(0), uint8_t(255), "Set equalizer preset", "PRESET")

        // === Equalizer (custom parsing) ===
        .custom('e', "equalizer", cli::ArgRequirement::Required, [&opts](std::optional<std::string_view> arg) -> std::optional<cli::ParseError> {
                if (!arg)
                    return cli::ParseError { "requires equalizer values", "equalizer" };

                auto values = headsetcontrol::parse_float_data(*arg);
                if (values.empty()) {
                    return cli::ParseError { "no band values specified", "equalizer" };
                }

                opts.equalizer = EqualizerSettings(std::move(values));
                return std::nullopt; }, "Set equalizer curve", "VALUES")

        // === Parametric Equalizer ===
        .long_custom("parametric-equalizer", cli::ArgRequirement::Required, [&opts](std::optional<std::string_view> arg) -> std::optional<cli::ParseError> {
                if (!arg)
                    return cli::ParseError { "requires band settings", "parametric-equalizer" };

                auto peq = headsetcontrol::parse_parametric_equalizer_settings(*arg);
                // Note: if any band failed to parse, it won't be in the list
                // This allows partial success - valid bands are kept
                opts.parametric_equalizer = std::move(peq);
                return std::nullopt; }, "Set parametric EQ bands", "BANDS")

        // === Microphone ===
        .long_value("microphone-mute-led-brightness", opts.mic_mute_led_brightness, uint8_t(0), uint8_t(3), "Set mic mute LED brightness", "LEVEL")
        .long_value("microphone-volume", opts.mic_volume, uint8_t(0), uint8_t(128), "Set microphone volume", "VOLUME")

        // === Volume ===
        .long_toggle("volume-limiter", opts.volume_limiter_enabled, "Toggle volume limiter")

        // === Bluetooth ===
        .long_toggle("bt-when-powered-on", opts.bt_when_powered_on, "Bluetooth on at power-on")
        .long_value("bt-call-volume", opts.bt_call_volume, uint8_t(0), uint8_t(255), "Set bluetooth call volume", "VOLUME")

        // === Output Format ===
        .choice('o', "output", opts.output_format, output_formats, "Output format")
        .custom('c', "short-output", cli::ArgRequirement::None, [&opts](auto) -> std::optional<cli::ParseError> {
                opts.output_format = OUTPUT_SHORT;
                return std::nullopt; }, "Short output format")

        // === Follow Mode ===
        .optional_value('f', "follow", opts.follow_mode, opts.follow_seconds, 2u, 1u, 3600u, "Re-run commands periodically", "SECS")

        // === Advanced ===
        .flag('u', "udev", opts.print_udev_rules, "Output udev rules")
        .flag('?', "capabilities", opts.print_capabilities, "List device capabilities")
        .long_flag("connected", opts.request_connected, "Check if device connected")
        .long_flag("dev", opts.dev_mode, "Development mode")

        // === Test Device ===
        .long_custom("test-device", cli::ArgRequirement::Optional, [&opts](std::optional<std::string_view> arg) -> std::optional<cli::ParseError> {
                opts.test_device = true;
                if (arg && !arg->empty()) {
                    long val = 0;
                    auto [ptr, ec] = std::from_chars(arg->data(), arg->data() + arg->size(), val);
                    if (ec == std::errc {} && ptr == arg->data() + arg->size() && val >= 0 && val <= 255) {
                        headsetcontrol::setTestProfile(static_cast<int>(val));
                    }
                }
                return std::nullopt; }, "Use test device", "PROFILE")

        // === Timeout ===
        .long_custom("timeout", cli::ArgRequirement::Required, [](std::optional<std::string_view> arg) -> std::optional<cli::ParseError> {
                if (!arg)
                    return cli::ParseError { "requires timeout value", "timeout" };
                long val = 0;
                auto [ptr, ec] = std::from_chars(arg->data(), arg->data() + arg->size(), val);
                if (ec != std::errc {} || ptr != arg->data() + arg->size() || val < 0 || val > 100000) {
                    return cli::ParseError { "invalid timeout (0-100000)", "timeout" };
                }
                headsetcontrol::setDeviceTimeout(static_cast<int>(val));
                return std::nullopt; }, "Set timeout in ms", "MS")

        // === Readme Helper (exits immediately) ===
        .long_custom("readme-helper", cli::ArgRequirement::None, [](auto) -> std::optional<cli::ParseError> {
                init_cpp_devices();
                // Print table (inline for simplicity)
                std::cout << "| Device | Platform |";
                for (int j = 0; j < NUM_CAPABILITIES; j++) {
                    std::cout << " " << capabilities_str[j] << " |";
                }
                std::cout << "\n| --- | --- |";
                for (int j = 0; j < NUM_CAPABILITIES; j++) {
                    std::cout << " --- |";
                }
                std::cout << '\n';

                for (const auto& device_ptr : DeviceRegistry::instance().getAllDevices()) {
                    auto* device = device_ptr.get();
                    std::cout << "| " << device->getDeviceName() << " |";
                    uint8_t platforms = device->getSupportedPlatforms();
                    const char* p = platforms == PLATFORM_ALL ? " All " : platforms == (PLATFORM_LINUX | PLATFORM_MACOS) ? " L/M "
                        : platforms == (PLATFORM_LINUX | PLATFORM_WINDOWS)                                               ? " L/W "
                        : platforms == PLATFORM_LINUX                                                                    ? " L "
                                                                                                                         : " ? ";
                    std::cout << p << "|";
                    int caps = device->getCapabilities();
                    for (int j = 0; j < NUM_CAPABILITIES; j++) {
                        std::cout << ((caps & B(j)) ? " x " : "   ") << "|";
                    }
                    std::cout << '\n';
                }
                std::exit(0); }, "Output README table");

    return std::nullopt;
}

// ============================================================================
// RAII wrapper for HID connection
// ============================================================================

class HIDConnection {
public:
    HIDConnection() = default;
    ~HIDConnection() { close(); }

    HIDConnection(const HIDConnection&)            = delete;
    HIDConnection& operator=(const HIDConnection&) = delete;

    HIDConnection(HIDConnection&& other) noexcept
        : handle_(other.handle_)
        , path_(std::move(other.path_))
    {
        other.handle_ = nullptr;
    }

    HIDConnection& operator=(HIDConnection&& other) noexcept
    {
        if (this != &other) {
            close();
            handle_       = other.handle_;
            path_         = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool isOpen() const { return handle_ != nullptr; }
    [[nodiscard]] hid_device* get() const { return handle_; }

    bool open(const std::string& new_path)
    {
        if (path_ == new_path && handle_)
            return true;
        close();
        handle_ = hid_open_path(new_path.c_str());
        if (handle_) {
            path_ = new_path;
            return true;
        }
        return false;
    }

    void close()
    {
        if (handle_) {
            hid_close(handle_);
            handle_ = nullptr;
        }
        path_.clear();
    }

private:
    hid_device* handle_ = nullptr;
    std::string path_;
};

// ============================================================================
// Device discovery
// ============================================================================

struct DiscoveredDevice {
    HIDDevice* device   = nullptr;
    uint16_t product_id = 0;
    HIDConnection connection;
    std::vector<FeatureRequest> feature_requests;
    std::wstring vendor_name;
    std::wstring product_name;

    [[nodiscard]] uint16_t vendorId() const
    {
        return device ? device->getVendorId() : 0;
    }

    [[nodiscard]] bool matchesFilter(const Options& opts) const
    {
        return device && opts.matchesDevice(device->getVendorId(), product_id);
    }

    [[nodiscard]] bool hasCapability(capabilities cap) const
    {
        return device && (device->getCapabilities() & B(cap)) != 0;
    }
};

// RAII wrapper for hid_enumerate result
class HIDEnumeration {
public:
    explicit HIDEnumeration(uint16_t vid = 0, uint16_t pid = 0)
        : devices_(hid_enumerate(vid, pid))
    {
    }
    ~HIDEnumeration()
    {
        if (devices_)
            hid_free_enumeration(devices_);
    }
    HIDEnumeration(const HIDEnumeration&) = delete;
    HIDEnumeration& operator=(const HIDEnumeration&) = delete;

    hid_device_info* get() const { return devices_; }

private:
    hid_device_info* devices_;
};

std::vector<DiscoveredDevice> discoverDevices(const Options& opts)
{
    std::vector<DiscoveredDevice> devices;
    auto& registry = DeviceRegistry::instance();

    if (opts.test_device) {
        if (auto* test_dev = registry.getDevice(VENDOR_TESTDEVICE, PRODUCT_TESTDEVICE)) {
            DiscoveredDevice dev;
            dev.device       = test_dev;
            dev.product_id   = PRODUCT_TESTDEVICE;
            dev.vendor_name  = L"HeadsetControl";
            dev.product_name = L"Test Device";
            devices.push_back(std::move(dev));
        }
    }

    HIDEnumeration enumeration(opts.vendor_id, opts.product_id);
    for (auto* cur = enumeration.get(); cur; cur = cur->next) {
        bool duplicate = std::any_of(devices.begin(), devices.end(), [&](const DiscoveredDevice& d) {
            return d.vendorId() == cur->vendor_id && d.product_id == cur->product_id;
        });

        if (!duplicate) {
            if (auto* device = registry.getDevice(cur->vendor_id, cur->product_id)) {
                DiscoveredDevice dev;
                dev.device     = device;
                dev.product_id = cur->product_id;
                if (cur->manufacturer_string)
                    dev.vendor_name = cur->manufacturer_string;
                if (cur->product_string)
                    dev.product_name = cur->product_string;
                devices.push_back(std::move(dev));
            }
        }
    }
    return devices;
}

// ============================================================================
// Feature handling
// ============================================================================

hid_device* connectForCapability(HIDConnection& conn, HIDDevice* device, uint16_t product_id, capabilities cap)
{
    auto detail   = device->getCapabilityDetail(cap);
    auto hid_path = headsetcontrol::get_hid_path(device->getVendorId(), product_id, detail.interface, detail.usagepage, detail.usageid);
    if (!hid_path)
        return nullptr;

    return conn.open(*hid_path) ? conn.get() : nullptr;
}

// Convert FeatureOutput to FeatureResult for output formatting
FeatureResult convertToFeatureResult(const headsetcontrol::FeatureOutput& output)
{
    FeatureResult result;
    result.status  = FEATURE_SUCCESS;
    result.value   = output.value;
    result.message = output.message;

    // Handle battery special case with extended info
    if (output.battery) {
        const auto& b  = *output.battery;
        result.value   = b.level_percent;
        result.status2 = static_cast<int>(b.status);

        // Copy extended battery info
        if (b.voltage_mv)
            result.battery_voltage_mv = b.voltage_mv;
        if (b.time_to_full_min)
            result.battery_time_to_full_min = b.time_to_full_min;
        if (b.time_to_empty_min)
            result.battery_time_to_empty_min = b.time_to_empty_min;
    }

    // Handle chatmix special case
    if (output.chatmix) {
        result.value = output.chatmix->level;
    }

    // Handle sidetone special case
    if (output.sidetone) {
        result.value = output.sidetone->current_level;
    }

    return result;
}

// Handle a feature request via the handler registry
FeatureResult handleFeature(DiscoveredDevice& dev, capabilities cap, const FeatureParam& param)
{
    // Validate parameter
    if (auto error = headsetcontrol::validateFeatureParam(cap, param)) {
        return make_error(-1, *error);
    }

    // Check device support
    if (!dev.hasCapability(cap)) {
        const auto& desc = headsetcontrol::getCapabilityDescriptor(cap);
        return make_error(-1, std::format("This headset doesn't support {}", desc.name));
    }

    bool is_test       = (dev.product_id == PRODUCT_TESTDEVICE);
    hid_device* handle = nullptr;

    // Connect to device (unless test device)
    if (!is_test) {
        handle = connectForCapability(dev.connection, dev.device, dev.product_id, cap);
        if (!handle) {
            return make_error(-1, "Could not open device");
        }
    }

    // Execute via handler registry (no more giant switch!)
    auto result = headsetcontrol::FeatureHandlerRegistry::instance().execute(
        cap, dev.device, handle, param);

    if (result.hasError()) {
        return make_error(-1, result.error().message);
    }

    return convertToFeatureResult(result.value());
}

// ============================================================================
// Help output
// ============================================================================

namespace help {

    // ANSI terminal formatting
    namespace ansi {
        constexpr std::string_view bold  = "\033[1m";
        constexpr std::string_view dim   = "\033[2m";
        constexpr std::string_view green = "\033[32m";
        constexpr std::string_view reset = "\033[0m";
    } // namespace ansi

    // Get value hint from capability descriptor (single source of truth)
    [[nodiscard]] inline std::string getValueHint(capabilities cap)
    {
        const auto& desc = headsetcontrol::getCapabilityDescriptor(cap);
        return std::string(desc.value_hint);
    }

    // Option definition for help display
    struct Option {
        char short_opt = '\0';
        std::string_view long_opt;
        std::string arg; // Owns string data (for dynamic value hints from descriptors)
        std::string_view description;
        std::optional<capabilities> required_cap = std::nullopt; // Show only if device has this
        bool advanced_only                       = false; // Show only in --help-all
    };

    // Section with its options
    struct Section {
        std::string_view title;
        std::vector<Option> options;

        // Fluent builder for adding options
        template <typename ArgT>
        Section& add(char short_opt, std::string_view long_opt, ArgT&& arg,
            std::string_view desc, std::optional<capabilities> cap = std::nullopt, bool advanced = false)
        {
            options.push_back({ short_opt, long_opt, std::string(std::forward<ArgT>(arg)), desc, cap, advanced });
            return *this;
        }

        template <typename ArgT>
        Section& add(std::string_view long_opt, ArgT&& arg, std::string_view desc,
            std::optional<capabilities> cap = std::nullopt, bool advanced = false)
        {
            return add('\0', long_opt, std::forward<ArgT>(arg), desc, cap, advanced);
        }
    };

    // Help generator class
    class HelpGenerator {
    public:
        HelpGenerator(std::string_view program, HIDDevice* dev, bool all)
            : program_name_(program)
            , device_(dev)
            , show_all_(all)
            , caps_(dev ? dev->getCapabilities() : 0)
        {
        }

        void generate()
        {
            printHeader();
            printUsage();

            for (const auto& section : buildSections()) {
                printSection(section);
            }

            printExamples();
            printFooter();
        }

    private:
        std::string_view program_name_;
        HIDDevice* device_;
        bool show_all_;
        int caps_;

        static constexpr int kColumnWidth = 36;

        [[nodiscard]] bool hasCapability(capabilities cap) const
        {
            return (caps_ & B(cap)) != 0;
        }

        [[nodiscard]] bool shouldShow(const Option& opt) const
        {
            if (opt.advanced_only && !show_all_)
                return false;
            if (opt.required_cap && !show_all_ && !hasCapability(*opt.required_cap))
                return false;
            return true;
        }

        [[nodiscard]] bool sectionHasVisibleOptions(const Section& section) const
        {
            return std::ranges::any_of(section.options, [this](const Option& opt) {
                return shouldShow(opt);
            });
        }

        void printHeader() const
        {
            println("{}HeadsetControl{} - Control USB gaming headsets on Linux/macOS/Windows",
                ansi::bold, ansi::reset);
            println("Version {} | https://github.com/Sapd/HeadsetControl", VERSION);

            if (device_) {
                println("\nDetected: {}{}{}", ansi::green, device_->getDeviceName(), ansi::reset);
            }
        }

        void printUsage() const
        {
            println("\n{}USAGE{}", ansi::bold, ansi::reset);
            println("  {} [OPTIONS]", program_name_);
            println("  {} -b                      # Check battery", program_name_);
            println("  {} -s 64 -l 1              # Set sidetone + lights on", program_name_);
        }

        void printOption(const Option& opt) const
        {
            std::string left;

            if (opt.short_opt != '\0') {
                left = std::format("  -{}", opt.short_opt);
                if (!opt.long_opt.empty()) {
                    left += std::format(", --{}", opt.long_opt);
                }
            } else if (!opt.long_opt.empty()) {
                left = std::format("      --{}", opt.long_opt);
            } else {
                return; // No option name
            }

            if (!opt.arg.empty()) {
                left += std::format(" {}", opt.arg);
            }

            // Pad or add spacing
            int padding = kColumnWidth - static_cast<int>(left.size());
            if (padding > 0) {
                left.append(padding, ' ');
            } else {
                left += "  ";
            }

            println("{}{}", left, opt.description);
        }

        void printSection(const Section& section) const
        {
            if (!sectionHasVisibleOptions(section))
                return;

            println("\n{}{}{}", ansi::bold, section.title, ansi::reset);

            for (const auto& opt : section.options) {
                if (shouldShow(opt)) {
                    printOption(opt);
                }
            }

            // Special handling for parametric EQ types
            if (section.title == "EQUALIZER" && (show_all_ || hasCapability(CAP_PARAMETRIC_EQUALIZER))) {
                printParametricEqTypes();
            }
        }

        void printParametricEqTypes() const
        {
            if (!show_all_ && !hasCapability(CAP_PARAMETRIC_EQUALIZER))
                return;

            println("                                      Format: FREQ,GAIN,Q,TYPE;...");

            auto peq_info = device_ ? device_->getParametricEqualizerInfo() : std::nullopt;
            std::string types;

            for (int i = 0; i < NUM_EQ_FILTER_TYPES; i++) {
                bool include = show_all_ || (peq_info && has_capability(peq_info->filter_types, static_cast<capabilities>(i)));
                if (include) {
                    if (!types.empty())
                        types += ", ";
                    types += equalizer_filter_type_str[i];
                }
            }

            if (!types.empty()) {
                println("                                      Types: {}", types);
            }
        }

        void printExamples() const
        {
            println("\n{}EXAMPLES{}", ansi::bold, ansi::reset);
            println("  {} -b -o json              # Battery status as JSON", program_name_);
            println("  {} -s 0 -l 0               # Disable sidetone and lights", program_name_);
            println("  {} -e 0,2,4,2,0,-2         # Custom 6-band EQ curve", program_name_);
            println("  {} -f 5 -b                 # Poll battery every 5 seconds", program_name_);

            if (show_all_) {
                println("  {} -d 1038:12ad -s 50      # Target specific device", program_name_);
            }
        }

        void printFooter() const
        {
            if (!show_all_) {
                println("\n{}Use --help-all to see all available options{}", ansi::dim, ansi::reset);
            }
        }

        [[nodiscard]] std::vector<Section> buildSections() const
        {
            std::vector<Section> sections;

            // Device selection - always shown
            sections.push_back({ "DEVICE SELECTION", {} });
            sections.back().add('d', "device", "VID:PID", "Select device (e.g., 1038:12ad)");

            // Status
            sections.push_back({ "STATUS", {} });
            sections.back()
                .add('b', "battery", "", "Show battery level and status", CAP_BATTERY_STATUS)
                .add('m', "chatmix", "", "Show current chat-mix balance", CAP_CHATMIX_STATUS)
                .add("connected", "", "Check if headset is connected", std::nullopt, true);

            // Audio - value hints from capability descriptors
            sections.push_back({ "AUDIO", {} });
            sections.back()
                .add('s', "sidetone", getValueHint(CAP_SIDETONE), "Mic feedback level (0=off)", CAP_SIDETONE)
                .add("volume-limiter", getValueHint(CAP_VOLUME_LIMITER), "Enable/disable volume limiter", CAP_VOLUME_LIMITER);

            // Equalizer
            sections.push_back({ "EQUALIZER", {} });
            sections.back()
                .add('e', "equalizer", "V1,V2,...", "Set EQ curve (comma-separated dB values)", CAP_EQUALIZER);

            // Dynamic preset range based on device
            if (device_ && device_->getEqualizerPresetsCount() > 0) {
                sections.back().add('p', "equalizer-preset",
                    std::format("0-{}", device_->getEqualizerPresetsCount() - 1),
                    "Select built-in EQ preset", CAP_EQUALIZER_PRESET);
            } else {
                sections.back().add('p', "equalizer-preset", "N", "Select built-in EQ preset", CAP_EQUALIZER_PRESET);
            }

            sections.back().add("parametric-equalizer", "BANDS", "Set parametric EQ bands", CAP_PARAMETRIC_EQUALIZER);

            // Microphone - value hints from capability descriptors
            sections.push_back({ "MICROPHONE", {} });
            sections.back()
                .add('r', "rotate-to-mute", getValueHint(CAP_ROTATE_TO_MUTE), "Mute when boom arm raised", CAP_ROTATE_TO_MUTE)
                .add("microphone-mute-led-brightness", getValueHint(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS), "Mute LED brightness", CAP_MICROPHONE_MUTE_LED_BRIGHTNESS)
                .add("microphone-volume", getValueHint(CAP_MICROPHONE_VOLUME), "Microphone gain level", CAP_MICROPHONE_VOLUME);

            // Lights & Audio Cues - value hints from capability descriptors
            sections.push_back({ "LIGHTS & AUDIO CUES", {} });
            sections.back()
                .add('l', "light", getValueHint(CAP_LIGHTS), "RGB/LED lights off/on", CAP_LIGHTS)
                .add('v', "voice-prompt", getValueHint(CAP_VOICE_PROMPTS), "Voice prompts off/on", CAP_VOICE_PROMPTS)
                .add('n', "notificate", getValueHint(CAP_NOTIFICATION_SOUND), "Play notification sound", CAP_NOTIFICATION_SOUND);

            // Power & Bluetooth - value hints from capability descriptors
            sections.push_back({ "POWER & BLUETOOTH", {} });
            sections.back()
                .add('i', "inactive-time", getValueHint(CAP_INACTIVE_TIME), "Auto-off after N minutes (0=never)", CAP_INACTIVE_TIME)
                .add("bt-when-powered-on", getValueHint(CAP_BT_WHEN_POWERED_ON), "Enable Bluetooth at power-on", CAP_BT_WHEN_POWERED_ON)
                .add("bt-call-volume", getValueHint(CAP_BT_CALL_VOLUME), "Bluetooth call volume", CAP_BT_CALL_VOLUME);

            // Output - always shown
            sections.push_back({ "OUTPUT", {} });
            sections.back()
                .add('o', "output", "FORMAT", "json, yaml, env, standard, short")
                .add('c', "short-output", "", "Compact output (same as -o short)")
                .add('?', "capabilities", "", "List device capabilities");

            // Advanced - only in --help-all
            sections.push_back({ "ADVANCED", {} });
            sections.back()
                .add('f', "follow", "[SECS]", "Repeat every N seconds (default: 2)", std::nullopt, true)
                .add("timeout", "MS", "HID read timeout (default: 5000)", std::nullopt, true)
                .add('u', "", "", "Generate udev rules for Linux", std::nullopt, true)
                .add("test-device", "[N]", "Use mock device (for testing)", std::nullopt, true);

            // Help - always shown
            sections.push_back({ "HELP", {} });
            sections.back()
                .add('h', "help", "", "Show help for detected device")
                .add("help-all", "", "Show all options");

            return sections;
        }
    };

} // namespace help

void printHelp(std::string_view program_name, HIDDevice* device, bool show_all)
{
    help::HelpGenerator(program_name, device, show_all).generate();
}

// ============================================================================
// Udev rules output
// ============================================================================

void printUdevRules()
{
    println("ACTION!=\"add|change\", GOTO=\"headset_end\"");
    println();

    for (const auto& device_ptr : DeviceRegistry::instance().getAllDevices()) {
        auto* device = device_ptr.get();
        println("# {}", device->getDeviceName());
        for (uint16_t pid : device->getProductIds()) {
            println("KERNEL==\"hidraw*\", SUBSYSTEM==\"hidraw\", "
                    "ATTRS{{idVendor}}==\"{:04x}\", ATTRS{{idProduct}}==\"{:04x}\", TAG+=\"uaccess\"",
                device->getVendorId(), pid);
        }
        println();
    }
    println("LABEL=\"headset_end\"");
}

// ============================================================================
// Feature request initialization
// ============================================================================

/**
 * @brief Storage for feature request parameters
 *
 * This struct holds copies of feature parameters that need to persist
 * for the duration of feature request processing. Using this struct
 * instead of static local variables ensures proper behavior in follow mode
 * and avoids const_cast issues with optional values.
 */
struct FeatureParamStorage {
    int sidetone_val         = 0;
    int lights_val           = 0;
    int notification_val     = 0;
    int inactive_time_val    = 0;
    int voice_prompts_val    = 0;
    int rotate_to_mute_val   = 0;
    int equalizer_preset_val = 0;
    int mic_led_val          = 0;
    int mic_vol_val          = 0;
    int volume_limiter_val   = 0;
    int bt_power_val         = 0;
    int bt_call_vol_val      = 0;
    int battery_req          = 0;
    int chatmix_req          = 0;

    // Store copies of complex settings to avoid const_cast
    EqualizerSettings equalizer_settings;
    ParametricEqualizerSettings parametric_eq_settings;

    void updateFrom(const Options& opts)
    {
        if (opts.sidetone_level)
            sidetone_val = *opts.sidetone_level;
        if (opts.lights_enabled)
            lights_val = *opts.lights_enabled ? 1 : 0;
        if (opts.notification_sound)
            notification_val = *opts.notification_sound;
        if (opts.inactive_time)
            inactive_time_val = *opts.inactive_time;
        if (opts.voice_prompts_enabled)
            voice_prompts_val = *opts.voice_prompts_enabled ? 1 : 0;
        if (opts.rotate_to_mute_enabled)
            rotate_to_mute_val = *opts.rotate_to_mute_enabled ? 1 : 0;
        if (opts.equalizer_preset)
            equalizer_preset_val = *opts.equalizer_preset;
        if (opts.mic_mute_led_brightness)
            mic_led_val = *opts.mic_mute_led_brightness;
        if (opts.mic_volume)
            mic_vol_val = *opts.mic_volume;
        if (opts.volume_limiter_enabled)
            volume_limiter_val = *opts.volume_limiter_enabled ? 1 : 0;
        if (opts.bt_when_powered_on)
            bt_power_val = *opts.bt_when_powered_on ? 1 : 0;
        if (opts.bt_call_volume)
            bt_call_vol_val = *opts.bt_call_volume;
        battery_req = opts.request_battery ? 1 : 0;
        chatmix_req = opts.request_chatmix ? 1 : 0;

        // Copy complex settings (avoids const_cast)
        if (opts.equalizer)
            equalizer_settings = *opts.equalizer;
        if (opts.parametric_equalizer)
            parametric_eq_settings = *opts.parametric_equalizer;
    }
};

// Global storage for feature parameters (must outlive feature requests)
FeatureParamStorage g_feature_params;

void initializeFeatureRequests(std::vector<DiscoveredDevice>& devices, const Options& opts)
{
    g_feature_params.updateFrom(opts);

    // Build feature requests with type-safe parameters (no more void*)
    std::vector<FeatureRequest> requests = {
        { CAP_SIDETONE, CAPABILITYTYPE_ACTION, g_feature_params.sidetone_val, opts.sidetone_level.has_value(), {} },
        { CAP_LIGHTS, CAPABILITYTYPE_ACTION, g_feature_params.lights_val, opts.lights_enabled.has_value(), {} },
        { CAP_NOTIFICATION_SOUND, CAPABILITYTYPE_ACTION, g_feature_params.notification_val, opts.notification_sound.has_value(), {} },
        { CAP_BATTERY_STATUS, CAPABILITYTYPE_INFO, std::monostate {}, opts.request_battery, {} },
        { CAP_INACTIVE_TIME, CAPABILITYTYPE_ACTION, g_feature_params.inactive_time_val, opts.inactive_time.has_value(), {} },
        { CAP_CHATMIX_STATUS, CAPABILITYTYPE_INFO, std::monostate {}, opts.request_chatmix, {} },
        { CAP_VOICE_PROMPTS, CAPABILITYTYPE_ACTION, g_feature_params.voice_prompts_val, opts.voice_prompts_enabled.has_value(), {} },
        { CAP_ROTATE_TO_MUTE, CAPABILITYTYPE_ACTION, g_feature_params.rotate_to_mute_val, opts.rotate_to_mute_enabled.has_value(), {} },
        { CAP_EQUALIZER_PRESET, CAPABILITYTYPE_ACTION, g_feature_params.equalizer_preset_val, opts.equalizer_preset.has_value(), {} },
        { CAP_MICROPHONE_MUTE_LED_BRIGHTNESS, CAPABILITYTYPE_ACTION, g_feature_params.mic_led_val, opts.mic_mute_led_brightness.has_value(), {} },
        { CAP_MICROPHONE_VOLUME, CAPABILITYTYPE_ACTION, g_feature_params.mic_vol_val, opts.mic_volume.has_value(), {} },
        { CAP_EQUALIZER, CAPABILITYTYPE_ACTION, opts.equalizer.has_value() ? FeatureParam { g_feature_params.equalizer_settings } : FeatureParam { std::monostate {} }, opts.equalizer.has_value(), {} },
        { CAP_PARAMETRIC_EQUALIZER, CAPABILITYTYPE_ACTION, opts.parametric_equalizer.has_value() ? FeatureParam { g_feature_params.parametric_eq_settings } : FeatureParam { std::monostate {} }, opts.parametric_equalizer.has_value(), {} },
        { CAP_VOLUME_LIMITER, CAPABILITYTYPE_ACTION, g_feature_params.volume_limiter_val, opts.volume_limiter_enabled.has_value(), {} },
        { CAP_BT_WHEN_POWERED_ON, CAPABILITYTYPE_ACTION, g_feature_params.bt_power_val, opts.bt_when_powered_on.has_value(), {} },
        { CAP_BT_CALL_VOLUME, CAPABILITYTYPE_ACTION, g_feature_params.bt_call_vol_val, opts.bt_call_volume.has_value(), {} }
    };

    for (auto& dev : devices) {
        dev.feature_requests = requests;
    }
}

// ============================================================================
// Signal handling
// ============================================================================

void signalHandler(int) { g_follow_running = false; }

void setupSignalHandler()
{
#ifdef _WIN32
    signal(SIGINT, signalHandler);
#else
    struct sigaction act {};
    act.sa_handler = signalHandler;
    sigaction(SIGINT, &act, nullptr);
#endif
}

// ============================================================================
// Legacy output bridge
// ============================================================================

std::vector<DeviceList> toLegacyDeviceList(std::vector<DiscoveredDevice>& devices)
{
    std::vector<DeviceList> legacy;
    legacy.reserve(devices.size());
    for (size_t i = 0; i < devices.size(); i++) {
        DeviceList entry {};
        entry.featureRequests = devices[i].feature_requests.data();
        entry.size            = static_cast<int>(devices[i].feature_requests.size());
        entry.device          = devices[i].device;
        entry.product_id      = devices[i].product_id;
        entry.num_devices     = static_cast<int>(devices.size() - i);
        entry.vendor_name     = devices[i].vendor_name.empty() ? nullptr : devices[i].vendor_name.c_str();
        entry.product_name    = devices[i].product_name.empty() ? nullptr : devices[i].product_name.c_str();
        legacy.push_back(entry);
    }
    return legacy;
}

} // anonymous namespace

// ============================================================================
// Main entry point
// ============================================================================

int main(int argc, char* argv[])
{
    Options opts;
    cli::ArgumentParser parser(argv[0]);
    configureParser(parser, opts);

    if (auto error = parser.parse(argc, argv)) {
        eprintln("Error: {}", error->format());
        return 1;
    }

    init_cpp_devices();

    // Handle immediate modes
    if (opts.print_udev_rules) {
        std::cerr << "Generating udev rules..\n\n";
        printUdevRules();
        return 0;
    }

    if (opts.dev_mode) {
        // Build argv for dev_main: program name + positional args (after --)
        auto positional = parser.positionalArgs();
        std::vector<char*> dev_argv;
        dev_argv.reserve(1 + positional.size());
        dev_argv.push_back(argv[0]);
        for (auto* arg : positional) {
            dev_argv.push_back(const_cast<char*>(arg));
        }
        return dev_main(static_cast<int>(dev_argv.size()), dev_argv.data());
    }

    // Warn about positional arguments (only if not in dev mode)
    for (auto* arg : parser.positionalArgs()) {
        eprintln("Warning: ignoring argument '{}'", arg);
    }

    // Discover devices
    auto devices = discoverDevices(opts);

    // Select device
    DiscoveredDevice* selected = nullptr;
    if (devices.size() == 1) {
        selected = &devices[0];
    } else if (opts.hasDeviceFilter()) {
        for (auto& dev : devices) {
            if (dev.matchesFilter(opts)) {
                selected = &dev;
                break;
            }
        }
    }

    // Handle version
    if (opts.show_version) {
        std::cout << "HeadsetControl " << VERSION << '\n';
        return 0;
    }

    // Handle help
    if (opts.show_help || opts.show_help_all) {
        printHelp(argv[0], selected ? selected->device : nullptr, opts.show_help_all);
        return 0;
    }

    // No devices
    if (devices.empty()) {
        output(nullptr, false, opts.output_format);
        return 1;
    }

    // Connection check
    if (opts.request_connected) {
        if (!selected) {
            std::cerr << "Error: No device selected\n";
            return 1;
        }
        bool connected = true;
        if (selected->hasCapability(CAP_BATTERY_STATUS)) {
            bool is_test  = opts.test_device && selected->vendorId() == VENDOR_TESTDEVICE;
            hid_device* h = is_test ? nullptr : connectForCapability(selected->connection, selected->device, selected->product_id, CAP_BATTERY_STATUS);
            if (!is_test && !h) {
                connected = false;
            } else {
                auto r    = selected->device->getBattery(h);
                connected = !r.hasError() && r.value().status == BATTERY_AVAILABLE;
            }
        }
        std::cout << (connected ? "true" : "false") << '\n';
        return 0;
    }

    // Setup follow mode
    g_follow_running = opts.follow_mode;
    setupSignalHandler();

    // Initialize requests
    initializeFeatureRequests(devices, opts);

    // Extended output enables all info requests
    bool extended = opts.output_format == OUTPUT_YAML || opts.output_format == OUTPUT_JSON || opts.output_format == OUTPUT_ENV;

    for (auto& dev : devices) {
        for (auto& req : dev.feature_requests) {
            if (extended && req.type == CAPABILITYTYPE_INFO && !req.should_process && dev.hasCapability(req.cap)) {
                req.should_process = true;
            }
            if (req.type == CAPABILITYTYPE_ACTION && req.should_process && devices.size() > 1) {
                if (!opts.hasDeviceFilter()) {
                    req.result.status  = FEATURE_NOT_PROCESSED;
                    req.result.message = "Multiple devices, specify with -d";
                    req.result.value   = -1;
                } else if (!dev.matchesFilter(opts)) {
                    req.should_process = false;
                }
            }
        }
    }

    // Main loop
    do {
        for (auto& dev : devices) {
            if (!dev.matchesFilter(opts))
                continue;
            for (auto& req : dev.feature_requests) {
                if (req.should_process && req.result.status == FEATURE_NOT_PROCESSED) {
                    req.result = handleFeature(dev, req.cap, req.param);
                }
            }
        }

        auto legacy = toLegacyDeviceList(devices);
        output(legacy.data(), opts.print_capabilities, opts.output_format);

        if (g_follow_running)
            std::this_thread::sleep_for(std::chrono::seconds(opts.follow_seconds));
    } while (g_follow_running);

    // Note: hid_exit() is called automatically by library's LibraryState destructor
    return 0;
}

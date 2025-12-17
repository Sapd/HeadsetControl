/***
    HeadsetControl Output System

    Main entry point for output formatting. Uses the serializer abstraction
    from serializers.hpp and data model from output_data.hpp.
***/

#include "output.hpp"
#include "devices/hid_device.hpp"
#include "output_data.hpp"
#include "string_utils.hpp"
#include "version.h"

#include <format>
#include <hidapi.h>

namespace {

using namespace headsetcontrol::output;
using namespace headsetcontrol::serializers;

// ============================================================================
// Constants
// ============================================================================

constexpr std::string_view API_VERSION = "1.4";
constexpr std::string_view APP_NAME    = "HeadsetControl";

// ============================================================================
// Feature Request Processing Helpers
// ============================================================================

// Process battery status result and update device data
void processBatteryResult(const FeatureResult& result, DeviceData& dev)
{
    if (result.status == FEATURE_SUCCESS || result.status == FEATURE_INFO) {
        BatteryData bat;
        if (result.status2 == BATTERY_CHARGING) {
            bat.status = BATTERY_CHARGING;
            bat.level  = result.value;
        } else if (result.status2 == BATTERY_UNAVAILABLE) {
            bat.status = BATTERY_UNAVAILABLE;
            bat.level  = -1;
        } else {
            bat.status = BATTERY_AVAILABLE;
            bat.level  = result.value;
        }
        bat.voltage_mv        = result.battery_voltage_mv;
        bat.time_to_full_min  = result.battery_time_to_full_min;
        bat.time_to_empty_min = result.battery_time_to_empty_min;
        dev.battery           = bat;
    } else if (result.status == FEATURE_ERROR) {
        dev.errors.emplace_back(capabilities_str[CAP_BATTERY_STATUS], result.message);
        dev.status = STATUS_PARTIAL;
        // Populate battery with UNAVAILABLE status when device errors
        BatteryData bat;
        bat.status  = BATTERY_UNAVAILABLE;
        bat.level   = -1;
        dev.battery = bat;
    }
}

// Process chatmix status result and update device data
void processChatmixResult(const FeatureResult& result, DeviceData& dev)
{
    if (result.status == FEATURE_SUCCESS || result.status == FEATURE_INFO) {
        dev.chatmix = result.value;
    } else if (result.status == FEATURE_ERROR) {
        dev.errors.emplace_back(capabilities_str[CAP_CHATMIX_STATUS], result.message);
        dev.status = STATUS_PARTIAL;
    }
}

// Process action capability result and add to device actions
void processActionResult(const FeatureRequest& req, DeviceData& dev, std::string_view device_name)
{
    ActionData action;
    action.capability     = capabilities_str_enum[req.cap];
    action.capability_str = capabilities_str[req.cap];
    action.device         = device_name;
    action.status         = req.result.status == FEATURE_SUCCESS ? STATUS_SUCCESS : STATUS_FAILURE;
    action.value          = req.result.value;
    action.error_message  = req.result.message;
    dev.actions.push_back(std::move(action));
}

// Process a single feature request and update device data
void processFeatureRequest(const FeatureRequest& req, DeviceData& dev, std::string_view device_name)
{
    if (!req.should_process)
        return;

    if (req.result.status == FEATURE_DEVICE_FAILED_OPEN) {
        dev.errors.emplace_back(capabilities_str[req.cap], req.result.message);
        return;
    }

    if (req.cap == CAP_BATTERY_STATUS) {
        processBatteryResult(req.result, dev);
    } else if (req.cap == CAP_CHATMIX_STATUS) {
        processChatmixResult(req.result, dev);
    } else if (req.type == CAPABILITYTYPE_ACTION) {
        processActionResult(req, dev, device_name);
    }
}

// ============================================================================
// Data Building
// ============================================================================

[[nodiscard]] OutputData buildOutputData(DeviceList* deviceList)
{
    OutputData data;
    data.name        = std::string(APP_NAME);
    data.version     = VERSION;
    data.api_version = std::string(API_VERSION);

#if defined(HID_API_VERSION_MAJOR)
    data.hidapi_version = hid_version_str();
#else
    data.hidapi_version = "0.0.0";
#endif

    if (!deviceList)
        return data;

    int num_devices = deviceList->num_devices;

    for (int i = 0; i < num_devices; ++i) {
        DeviceData dev;
        auto* hid_device    = deviceList[i].device;
        uint16_t product_id = deviceList[i].product_id;

        dev.status      = STATUS_SUCCESS;
        dev.vendor_id   = std::format("0x{:04x}", hid_device->getVendorId());
        dev.product_id  = std::format("0x{:04x}", product_id);
        dev.device_name = hid_device->getDeviceName();

        // Vendor and product names from HID enumeration
        if (deviceList[i].vendor_name)
            dev.vendor_name = deviceList[i].vendor_name;
        if (deviceList[i].product_name)
            dev.product_name = deviceList[i].product_name;

        // Capabilities
        int device_caps = hid_device->getCapabilities();
        for (int j = 0; j < NUM_CAPABILITIES; ++j) {
            if (device_caps & B(j)) {
                dev.caps.emplace_back(capabilities_str_enum[j]);
                dev.caps_str.emplace_back(capabilities_str[j]);
                dev.caps_enum.emplace_back(static_cast<enum capabilities>(j));
            }
        }

        // Equalizer info
        if (device_caps & B(CAP_EQUALIZER)) {
            if (auto eq_info = hid_device->getEqualizerInfo()) {
                dev.equalizer = EqualizerData {
                    .bands_count = eq_info->bands_count,
                    .baseline    = eq_info->bands_baseline,
                    .step        = eq_info->bands_step,
                    .min         = eq_info->bands_min,
                    .max         = eq_info->bands_max
                };
            }
        }

        // Equalizer presets count
        if (device_caps & B(CAP_EQUALIZER_PRESET)) {
            auto presets_count = hid_device->getEqualizerPresetsCount();
            if (presets_count > 0) {
                dev.equalizer_presets_count = presets_count;
            }
        }

        // Process feature requests
        auto* requests = deviceList[i].featureRequests;
        int req_size   = deviceList[i].size;

        for (int r = 0; r < req_size; ++r) {
            processFeatureRequest(requests[r], dev, hid_device->getDeviceName());
        }

        data.devices.push_back(std::move(dev));
    }

    return data;
}

// ============================================================================
// JSON Output
// ============================================================================

void outputJson(const OutputData& data)
{
    JsonSerializer s(std::cout);
    data.serialize(s);
}

// ============================================================================
// YAML Output
// ============================================================================

void outputYaml(const OutputData& data)
{
    YamlSerializer s(std::cout);

    s.beginDocument();
    s.write("name", data.name);
    s.write("version", data.version);
    s.write("api_version", data.api_version);
    s.write("hidapi_version", data.hidapi_version);

    // Actions
    if (data.hasActions()) {
        s.beginArray("actions");
        for (const auto& dev : data.devices) {
            for (const auto& action : dev.actions) {
                s.writeListItem("capability", action.capability);
                s.pushIndent(1); // Align subsequent keys with "capability" after "- "
                s.write("device", action.device);
                s.write("status", statusToString(action.status));
                if (action.value > 0)
                    s.write("value", action.value);
                if (!action.error_message.empty())
                    s.write("error_message", action.error_message);
                s.popIndent(1);
            }
        }
        s.endArray();
    }

    s.write("device_count", data.deviceCount());

    if (!data.devices.empty()) {
        s.beginArray("devices");
        for (const auto& dev : data.devices) {
            s.writeListItem("status", statusToString(dev.status));
            s.pushIndent(1); // Align subsequent keys with "status" after "- "
            s.write("device", dev.device_name);
            s.write("vendor", dev.vendor_name.empty() ? "" : headsetcontrol::wstring_to_string(dev.vendor_name.c_str()));
            s.write("product", dev.product_name.empty() ? "" : headsetcontrol::wstring_to_string(dev.product_name.c_str()));
            s.write("id_vendor", dev.vendor_id);
            s.write("id_product", dev.product_id);

            s.writeArray("capabilities", dev.caps);
            s.writeArray("capabilities_str", dev.caps_str);

            if (dev.battery.has_value()) {
                dev.battery->serialize(s);
            }

            if (dev.chatmix.has_value()) {
                s.write("chatmix", *dev.chatmix);
            }

            if (!dev.errors.empty()) {
                s.beginObject("errors");
                for (const auto& err : dev.errors) {
                    s.write(err.source, err.message);
                }
                s.endObject();
            }
            s.popIndent(1);
        }
        s.endArray();
    }

    s.endDocument();
}

// ============================================================================
// ENV Output
// ============================================================================

void outputEnv(const OutputData& data)
{
    EnvSerializer s(std::cout);

    s.write("HEADSETCONTROL_NAME", data.name);
    s.write("HEADSETCONTROL_VERSION", data.version);
    s.write("HEADSETCONTROL_API_VERSION", data.api_version);
    s.write("HEADSETCONTROL_HIDAPI_VERSION", data.hidapi_version);

    // Actions
    int action_count = 0;
    for (const auto& dev : data.devices) {
        action_count += static_cast<int>(dev.actions.size());
    }
    s.write("ACTION_COUNT", action_count);

    int action_idx = 0;
    for (const auto& dev : data.devices) {
        for (const auto& action : dev.actions) {
            std::string prefix = std::format("ACTION_{}", action_idx);
            s.write(prefix + "_CAPABILITY", action.capability);
            s.write(prefix + "_DEVICE", action.device);
            s.write(prefix + "_STATUS", statusToString(action.status));
            if (action.value > 0) {
                s.write(prefix + "_VALUE", action.value);
            }
            if (!action.error_message.empty()) {
                s.write(prefix + "_ERROR_MESSAGE", action.error_message);
            }
            action_idx++;
        }
    }

    s.write("DEVICE_COUNT", data.deviceCount());

    for (size_t i = 0; i < data.devices.size(); ++i) {
        const auto& dev    = data.devices[i];
        std::string prefix = std::format("DEVICE_{}", i);

        s.write(prefix, dev.device_name);
        s.write(prefix + "_CAPABILITIES_AMOUNT", static_cast<int>(dev.caps.size()));

        for (size_t j = 0; j < dev.caps.size(); ++j) {
            s.write(std::format("{}_CAPABILITY_{}", prefix, j), dev.caps[j]);
        }

        if (dev.battery.has_value()) {
            s.write(prefix + "_BATTERY_STATUS", batteryStatusToString(dev.battery->status));
            s.write(prefix + "_BATTERY_LEVEL", dev.battery->level);
            s.writeOptional(prefix + "_BATTERY_VOLTAGE_MV", dev.battery->voltage_mv);
            s.writeOptional(prefix + "_BATTERY_TIME_TO_FULL_MIN", dev.battery->time_to_full_min);
            s.writeOptional(prefix + "_BATTERY_TIME_TO_EMPTY_MIN", dev.battery->time_to_empty_min);
        }

        if (dev.chatmix.has_value()) {
            s.write(prefix + "_CHATMIX", *dev.chatmix);
        }

        s.write(prefix + "_ERROR_COUNT", static_cast<int>(dev.errors.size()));
        for (size_t j = 0; j < dev.errors.size(); ++j) {
            s.write(std::format("{}_ERROR_{}_SOURCE", prefix, j + 1), dev.errors[j].source);
            s.write(std::format("{}_ERROR_{}_MESSAGE", prefix, j + 1), dev.errors[j].message);
        }
    }

    s.endDocument();
}

// ============================================================================
// Standard (Human-Readable) Output
// ============================================================================

void outputStandard(const OutputData& data, bool print_capabilities)
{
    TextSerializer s(std::cout);

    if (data.devices.empty()) {
        s.println("No supported device found");
        return;
    }

    s.println("Found {} supported device(s):", data.deviceCount());

    bool outputted = false;

    for (const auto& dev : data.devices) {
        if (!dev.product_name.empty()) {
            s.println(" {} ({}) [{}:{}]", dev.device_name, headsetcontrol::wstring_to_string(dev.product_name.c_str()), dev.vendor_id, dev.product_id);
        } else {
            s.println(" {} [{}:{}]", dev.device_name, dev.vendor_id, dev.product_id);
        }

        if (print_capabilities) {
            s.println("\tCapabilities:");
            for (const auto& cap : dev.caps_str) {
                s.println("\t* {}", cap);
            }
            s.printLine("");
            outputted = true;
            continue;
        }

        if (dev.battery.has_value()) {
            s.println("Battery:");
            s.println("\tStatus: {}", batteryStatusToString(dev.battery->status));
            if (dev.battery->status != BATTERY_UNAVAILABLE) {
                s.println("\tLevel: {}%", dev.battery->level);
            }
            if (dev.battery->voltage_mv.has_value()) {
                s.println("\tVoltage: {} mV", *dev.battery->voltage_mv);
            }
            if (dev.battery->time_to_full_min.has_value()) {
                s.println("\tTime to Full: {} minutes", *dev.battery->time_to_full_min);
            }
            if (dev.battery->time_to_empty_min.has_value()) {
                s.println("\tTime to Empty: {} minutes", *dev.battery->time_to_empty_min);
            }
            outputted = true;
        }

        if (dev.chatmix.has_value()) {
            s.println("Chatmix: {}", *dev.chatmix);
            outputted = true;
        }

        for (const auto& err : dev.errors) {
            s.println("Error: [{}] {}", err.source, err.message);
            outputted = true;
        }
    }

    if (print_capabilities) {
        s.printLine("Hint: Use --help while the device is connected to get a filtered list of parameters");
    }

    for (const auto& dev : data.devices) {
        for (const auto& action : dev.actions) {
            outputted = true;
            if (action.status == STATUS_SUCCESS) {
                s.println("\nSuccessfully set {}!", action.capability_str);
            } else {
                s.println("\n{}", action.error_message);
            }
        }
    }

    if (!outputted) {
        s.println("\nHeadsetControl ({}) written by Sapd (Denis Arnst)", VERSION);
        s.printLine("\thttps://github.com/Sapd/HeadsetControl\n");
        s.printLine("You didn't set any arguments, so nothing happened.\nUse -h for help.");
    }
}

// ============================================================================
// Short (Legacy Compact) Output
// ============================================================================

void outputShort(const OutputData& data, bool print_capabilities)
{
    ShortSerializer s(std::cout, std::cerr);

    if (data.devices.empty()) {
        s.printWarning("No supported headset found\n");
        s.endDocument();
        return;
    }

    bool first = true;
    for (const auto& dev : data.devices) {
        if (!first) {
            s.printWarning("\nWarning: multiple headsets connected but not supported by short output\n");
            break;
        }
        first = false;

        if (!dev.errors.empty()) {
            for (const auto& err : dev.errors) {
                s.printError(err.source, err.message);
            }
            break;
        }

        if (print_capabilities) {
            for (auto cap : dev.caps_enum) {
                s.printChar(capabilities_str_short[cap]);
            }
            continue;
        }

        if (dev.battery.has_value()) {
            if (dev.battery->status == BATTERY_CHARGING) {
                s.printValue(-1);
            } else if (dev.battery->status == BATTERY_UNAVAILABLE) {
                s.printValue(-2);
            } else {
                s.printValue(dev.battery->level);
            }
        } else if (dev.chatmix.has_value()) {
            s.printValue(*dev.chatmix);
        }
    }

    s.endDocument();
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

void output(DeviceList* deviceList, bool print_capabilities, OutputType outputType)
{
    auto data = buildOutputData(deviceList);

    switch (outputType) {
    case OUTPUT_JSON:
        outputJson(data);
        break;
    case OUTPUT_YAML:
        outputYaml(data);
        break;
    case OUTPUT_ENV:
        outputEnv(data);
        break;
    case OUTPUT_STANDARD:
        outputStandard(data, print_capabilities);
        break;
    case OUTPUT_SHORT:
        outputShort(data, print_capabilities);
        break;
    }
}

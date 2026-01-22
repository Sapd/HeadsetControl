#pragma once
/***
    Output Data Model for HeadsetControl

    Defines the data structures used for serialization.
    These are format-agnostic representations of device state.
***/

#include "device.hpp"
#include "output.hpp"
#include "serializers.hpp"
#include "string_utils.hpp"

#include <optional>
#include <string>
#include <vector>

namespace headsetcontrol::output {

using serializers::Serializer;

// ============================================================================
// String Conversion Utilities
// ============================================================================

[[nodiscard]] constexpr std::string_view statusToString(Status status)
{
    switch (status) {
    case STATUS_SUCCESS:
        return "success";
    case STATUS_FAILURE:
        return "failure";
    case STATUS_PARTIAL:
        return "partial";
    default:
        return "unknown";
    }
}

[[nodiscard]] constexpr std::string_view batteryStatusToString(battery_status status)
{
    switch (status) {
    case BATTERY_UNAVAILABLE:
        return "BATTERY_UNAVAILABLE";
    case BATTERY_CHARGING:
        return "BATTERY_CHARGING";
    case BATTERY_AVAILABLE:
        return "BATTERY_AVAILABLE";
    case BATTERY_HIDERROR:
        return "BATTERY_ERROR";
    case BATTERY_TIMEOUT:
        return "BATTERY_TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

// ============================================================================
// Data Structures
// ============================================================================

struct BatteryData {
    battery_status status = BATTERY_UNAVAILABLE;
    int level             = -1;
    std::optional<int> voltage_mv;
    std::optional<int> time_to_full_min;
    std::optional<int> time_to_empty_min;

    void serialize(Serializer& s) const
    {
        s.beginObject("battery");
        s.write("status", batteryStatusToString(status));
        s.write("level", level);
        s.writeOptional("voltage_mv", voltage_mv);
        s.writeOptional("time_to_full_min", time_to_full_min);
        s.writeOptional("time_to_empty_min", time_to_empty_min);
        s.endObject();
    }
};

struct ActionData {
    std::string capability;
    std::string capability_str;
    std::string device;
    Status status = STATUS_SUCCESS;
    int value     = 0;
    std::string error_message;

    void serialize(Serializer& s) const
    {
        s.beginObject("");
        s.write("capability", capability);
        s.write("device", device);
        s.write("status", statusToString(status));
        if (value > 0) {
            s.write("value", value);
        }
        if (!error_message.empty()) {
            s.write("error_message", error_message);
        }
        s.endObject();
    }
};

struct ErrorData {
    std::string source;
    std::string message;
};

struct EqualizerPresetData {
    std::string name;
    std::vector<float> values;

    void serialize(Serializer& s) const
    {
        s.beginObject("");
        s.write("name", name);
        s.writeArray("values", values);
        s.endObject();
    }
};

struct EqualizerData {
    int bands_count = 0;
    int baseline    = 0;
    float step      = 0;
    int min         = 0;
    int max         = 0;

    void serialize(Serializer& s) const
    {
        s.beginObject("equalizer");
        s.write("bands", bands_count);
        s.write("baseline", baseline);
        s.write("step", static_cast<double>(step));
        s.write("min", min);
        s.write("max", max);
        s.endObject();
    }
};

struct ParametricEqData {
    int bands_count  = 0;
    double gain_step = 0, gain_min = 0, gain_max = 0, gain_base = 0;
    double q_min = 0, q_max = 0;
    int freq_min = 0, freq_max = 0;
    std::vector<std::string> filter_types;

    void serialize(Serializer& s) const
    {
        s.beginObject("parametric_equalizer");
        s.write("bands", bands_count);

        s.beginObject("gain");
        s.write("step", gain_step);
        s.write("min", gain_min);
        s.write("max", gain_max);
        s.write("base", gain_base);
        s.endObject();

        s.beginObject("q_factor");
        s.write("min", q_min);
        s.write("max", q_max);
        s.endObject();

        s.beginObject("frequency");
        s.write("min", freq_min);
        s.write("max", freq_max);
        s.endObject();

        s.writeArray("filter_types", filter_types);
        s.endObject();
    }
};

struct DeviceData {
    Status status = STATUS_SUCCESS;
    std::string vendor_id;
    std::string product_id;
    std::string device_name;
    std::wstring vendor_name;
    std::wstring product_name;
    std::vector<std::string> caps;
    std::vector<std::string> caps_str;
    std::vector<enum capabilities> caps_enum;

    std::optional<BatteryData> battery;
    std::optional<int> chatmix;
    std::optional<EqualizerData> equalizer;
    std::optional<int> equalizer_presets_count;
    std::optional<std::vector<EqualizerPresetData>> equalizer_presets;
    std::optional<ParametricEqData> parametric_eq;

    std::vector<ActionData> actions;
    std::vector<ErrorData> errors;

    void serialize(Serializer& s) const
    {
        s.beginObject("");
        s.write("status", statusToString(status));
        s.write("device", device_name);
        s.write("vendor", vendor_name.empty() ? "" : headsetcontrol::wstring_to_string(vendor_name.c_str()));
        s.write("product", product_name.empty() ? "" : headsetcontrol::wstring_to_string(product_name.c_str()));
        s.write("id_vendor", vendor_id);
        s.write("id_product", product_id);

        s.writeArray("capabilities", caps);
        s.writeArray("capabilities_str", caps_str);

        if (battery.has_value()) {
            battery->serialize(s);
        }

        if (equalizer.has_value()) {
            equalizer->serialize(s);
        }

        if (equalizer_presets_count.has_value()) {
            s.write("equalizer_presets_count", *equalizer_presets_count);
        }

        if (equalizer_presets.has_value() && !equalizer_presets->empty()) {
            s.beginArray("equalizer_presets");
            for (const auto& preset : *equalizer_presets) {
                preset.serialize(s);
            }
            s.endArray();
        }

        if (parametric_eq.has_value()) {
            parametric_eq->serialize(s);
        }

        if (chatmix.has_value()) {
            s.write("chatmix", *chatmix);
        }

        if (!errors.empty()) {
            s.beginObject("errors");
            for (const auto& err : errors) {
                s.write(err.source, err.message);
            }
            s.endObject();
        }

        s.endObject();
    }
};

struct OutputData {
    std::string name;
    std::string version;
    std::string api_version;
    std::string hidapi_version;
    std::vector<DeviceData> devices;

    [[nodiscard]] int deviceCount() const
    {
        return static_cast<int>(devices.size());
    }

    [[nodiscard]] bool hasActions() const
    {
        for (const auto& dev : devices) {
            if (!dev.actions.empty())
                return true;
        }
        return false;
    }

    void serialize(Serializer& s) const
    {
        s.beginDocument();
        s.write("name", name);
        s.write("version", version);
        s.write("api_version", api_version);
        s.write("hidapi_version", hidapi_version);

        // Actions from all devices
        if (hasActions()) {
            s.beginArray("actions");
            for (const auto& dev : devices) {
                for (const auto& action : dev.actions) {
                    action.serialize(s);
                }
            }
            s.endArray();
        }

        s.write("device_count", deviceCount());

        s.beginArray("devices");
        for (const auto& dev : devices) {
            dev.serialize(s);
        }
        s.endArray();

        s.endDocument();
    }
};

} // namespace headsetcontrol::output

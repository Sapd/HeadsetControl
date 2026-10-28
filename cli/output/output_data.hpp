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
    std::optional<std::string> color; // "#rrggbb", only for CAP_LIGHT_COLOR
    std::string error_message;

    /**
     * @brief Whether value carries information worth printing
     *
     * Most actions report 0 when they have nothing to say (the equalizer always
     * does) and -1 on failure, so only positive values are shown. A color is the
     * exception: black packs to 0 and is still the value that was set.
     */
    [[nodiscard]] bool hasValue() const
    {
        return value > 0 || (color.has_value() && status == STATUS_SUCCESS);
    }

    void serialize(Serializer& s) const
    {
        s.beginObject("");
        s.write("capability", capability);
        s.write("device", device);
        s.write("status", statusToString(status));
        if (hasValue()) {
            s.write("value", value);
        }
        if (color) {
            s.write("color", *color);
        }
        if (!error_message.empty()) {
            s.write("error_message", error_message);
        }
        s.endObject();
    }
};

struct SidetoneData {
    int level        = 0;
    int device_level = 0;
    std::string name;
    int min_level = 0;
    int max_level = 0;
    // Selectable levels when the device is quantised; empty when the whole
    // min..max range is meaningful.
    std::vector<int> levels;

    void serialize(Serializer& s) const
    {
        s.beginObject("sidetone");
        s.write("level", level);
        s.write("device_level", device_level);
        if (max_level > min_level) {
            s.write("min", min_level);
            s.write("max", max_level);
        }
        if (!levels.empty()) {
            s.beginArray("levels");
            for (const auto level : levels) {
                s.writeArrayItem(level);
            }
            s.endArray();
        }
        if (!name.empty()) {
            s.write("name", name);
        }
        s.endObject();
    }
};

struct HeadsetVolumeData {
    int level        = 0;
    int device_level = 0;

    void serialize(Serializer& s) const
    {
        s.beginObject("headset_volume");
        s.write("level", level);
        s.write("device_level", device_level);
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
    std::vector<std::string> readable_caps;
    std::vector<std::string> readable_caps_str;

    std::optional<BatteryData> battery;
    std::optional<int> chatmix;
    std::optional<SidetoneData> sidetone;
    std::optional<HeadsetVolumeData> headset_volume;
    std::optional<bool> mic_attached;
    std::optional<bool> mic_muted;
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
        s.write("vendor", vendor_name.empty() ? "" : headsetcontrol::wstring_to_utf8(vendor_name));
        s.write("product", product_name.empty() ? "" : headsetcontrol::wstring_to_utf8(product_name));
        s.write("id_vendor", vendor_id);
        s.write("id_product", product_id);

        s.writeArray("capabilities", caps);
        s.writeArray("capabilities_str", caps_str);
        s.writeArray("readable_capabilities", readable_caps);
        s.writeArray("readable_capabilities_str", readable_caps_str);

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
            s.beginObject("equalizer_presets");
            for (const auto& preset : *equalizer_presets) {
                // Convert name to lowercase for API compatibility
                std::string lower_name;
                lower_name.reserve(preset.name.size());
                for (char c : preset.name) {
                    lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                s.writeArray(lower_name, preset.values);
            }
            s.endObject();

            // The key order of the object above is not preserved by all JSON
            // consumers (Qt, for one, sorts object keys), while `--equalizer-preset`
            // takes an index. This array is the preset list in index order so a
            // front-end can label preset N with the right name.
            s.beginArray("equalizer_presets_ordered");
            for (const auto& preset : *equalizer_presets) {
                std::string lower_name;
                lower_name.reserve(preset.name.size());
                for (char c : preset.name) {
                    lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                s.writeArrayItem(lower_name);
            }
            s.endArray();
        }

        if (parametric_eq.has_value()) {
            parametric_eq->serialize(s);
        }

        if (chatmix.has_value()) {
            s.write("chatmix", *chatmix);
        }

        if (sidetone.has_value()) {
            sidetone->serialize(s);
        }

        if (headset_volume.has_value()) {
            headset_volume->serialize(s);
        }

        if (mic_attached.has_value()) {
            s.write("mic_attached", *mic_attached);
        }

        if (mic_muted.has_value()) {
            s.write("mic_muted", *mic_muted);
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

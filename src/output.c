#include "output.h"
#include "utility.h"
#include "version.h"

#include <assert.h>
#include <ctype.h>
#include <hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* APIVERSION          = "1.3";
const char* HEADSETCONTROL_NAME = "HeadsetControl";

// Function to convert enum to string
const char* status_to_string(Status status)
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

const char* battery_status_to_string(enum battery_status status)
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

/**
 * @brief Adds an error to the HeadsetInfo struct
 *
 * @param info Headsetinfo struct
 * @param source Error source/key as string
 * @param message Error message
 */
static void addError(HeadsetInfo* info, const char* source, const char* message)
{
    if (info->error_count < MAX_ERRORS) {
        info->errors[info->error_count].source  = strdup(source);
        info->errors[info->error_count].message = strdup(message);
        info->error_count++;
    } else {
        fprintf(stderr, "Error: addError MAX_ERRORS exceeded\n");
        abort();
    }
}

/**
 * @brief Adds an action to the HeadsetInfo struct
 *
 * @param info Headsetinfo struct
 * @param capability Capability as enum
 * @param device Device name as string
 * @param status Status of the action
 * @param value Value of the action (not used currently)
 * @param error_message Error message if existing or NULL
 */
static void addAction(HeadsetInfo* info, enum capabilities capability, const char* device, Status status, int value, const char* error_message)
{
    UNUSED(value);

    if (info->action_count < MAX_ACTIONS) {
        info->actions[info->action_count].capability     = capabilities_str_enum[capability];
        info->actions[info->action_count].capability_str = capabilities_str[capability];
        info->actions[info->action_count].device         = strdup(device);
        info->actions[info->action_count].status         = status;
        info->actions[info->action_count].value          = 0; // currently not used

        if (error_message != NULL)
            info->actions[info->action_count].error_message = strdup(error_message);
        else
            info->actions[info->action_count].error_message = NULL;

        info->action_count++;
    } else {
        fprintf(stderr, "Error: addAction MAX_ACTIONS exceeded\n");
        abort();
    }
}

static HeadsetControlStatus initializeStatus(int num_devices);
static void initializeHeadsetInfo(HeadsetInfo* info, struct device* device);
static void processFeatureRequests(HeadsetInfo* info, FeatureRequest* featureRequests, int size, struct device* device);
static void outputByType(OutputType output, HeadsetControlStatus* status, HeadsetInfo* infos, bool print_capabilities);

void output(DeviceList* deviceList, bool print_capabilities, OutputType output)
{
    int num_devices = deviceList ? deviceList->num_devices : 0;

    HeadsetControlStatus status = initializeStatus(num_devices);
    HeadsetInfo* infos          = NULL;
    if (num_devices > 0) {
        infos = calloc(num_devices, sizeof(HeadsetInfo));
    }

    // Suppress static analysis warning
    assert((infos != NULL && status.device_count > 0) || (infos == NULL && status.device_count == 0));

    // Iterate through all devices
    for (int deviceIndex = 0; deviceIndex < num_devices; deviceIndex++) {
        initializeHeadsetInfo(&infos[deviceIndex], deviceList[deviceIndex].device);
        processFeatureRequests(&infos[deviceIndex], deviceList[deviceIndex].featureRequests, deviceList[deviceIndex].size, deviceList[deviceIndex].device);
    }

    // Send all gathered information to respective output function
    outputByType(output, &status, infos, print_capabilities);

    for (int i = 0; i < num_devices; i++) {
        free(infos[i].idVendor);
        free(infos[i].idProduct);

        for (int j = 0; j < infos[i].error_count; j++) {
            free(infos[i].errors[j].source);
            free(infos[i].errors[j].message);
        }

        for (int j = 0; j < infos[i].action_count; j++) {
            free(infos[i].actions[j].device);
            free(infos[i].actions[j].error_message);
        }
    }
    free(infos);
}

HeadsetControlStatus initializeStatus(int num_devices)
{
    HeadsetControlStatus status = { 0 };
    status.version              = VERSION;
    status.api_version          = APIVERSION;
    status.name                 = HEADSETCONTROL_NAME;
#if defined(HID_API_VERSION_MAJOR)
    // The `hid_version_str()` function is available
    status.hid_version = hid_version_str();
#else
    // `hid_version_str()` is not available, return a general version string or number
    status.hid_version = "0.0.0";
#endif
    status.device_count = num_devices;
    return status;
}

void initializeHeadsetInfo(HeadsetInfo* info, struct device* device)
{
    info->status = STATUS_SUCCESS;
    _asprintf(&info->idVendor, "0x%04x", device->idVendor);
    _asprintf(&info->idProduct, "0x%04x", device->idProduct);
    info->device_name  = device->device_name;
    info->vendor_name  = device->device_hid_vendorname;
    info->product_name = device->device_hid_productname;

    info->equalizer                  = device->equalizer;
    info->equalizer_presets          = device->equalizer_presets,
    info->has_equalizer_info         = info->equalizer != NULL;
    info->has_equalizer_presets_info = info->equalizer_presets != NULL;

    info->parametric_equalizer          = device->parametric_equalizer,
    info->has_parametric_equalizer_info = info->parametric_equalizer != NULL;

    info->capabilities_amount = 0;

    for (int i = 0; i < NUM_CAPABILITIES; i++) {
        if (device->capabilities & B(i)) { // Check if the ith capability is supported
            info->capabilities_enum[info->capabilities_amount] = i;
            info->capabilities[info->capabilities_amount]      = capabilities_str_enum[i];
            info->capabilities_str[info->capabilities_amount]  = capabilities_str[i];
            info->capabilities_amount++;
        }
    }
}

/**
 * @brief Iterates through all requested features and processes their responses
 *
 * @param info struct with headset information to fill
 * @param featureRequests struct with all feature requests
 * @param size size of featureRequests
 * @param device the device struct
 */
void processFeatureRequests(HeadsetInfo* info, FeatureRequest* featureRequests, int size, struct device* device)
{
    for (int i = 0; i < size; i++) {
        FeatureRequest* request = &featureRequests[i];
        if (request->should_process) {
            if (request->result.status == FEATURE_DEVICE_FAILED_OPEN) {
                addError(info, capabilities_str[request->cap], request->result.message);
            } else if (request->cap == CAP_BATTERY_STATUS) {
                if (request->result.status == FEATURE_SUCCESS || request->result.status == FEATURE_INFO) {
                    info->has_battery_info = true;

                    if (request->result.status2 == BATTERY_CHARGING) {
                        info->battery_status = (enum battery_status)request->result.status2;
                        info->battery_level  = request->result.value;
                    } else if (request->result.status2 == BATTERY_UNAVAILABLE) {
                        info->battery_status = (enum battery_status)request->result.status2;
                        info->battery_level  = -1;
                    } else {
                        info->battery_status = BATTERY_AVAILABLE;
                        info->battery_level  = request->result.value;
                    }
                } else if (request->result.status == FEATURE_ERROR) {
                    addError(info, capabilities_str[request->cap], request->result.message);
                    info->status = STATUS_PARTIAL;
                }
            } else if (request->cap == CAP_CHATMIX_STATUS) {
                if (request->result.status == FEATURE_SUCCESS || request->result.status == FEATURE_INFO) {
                    info->has_chatmix_info = true;
                    info->chatmix          = request->result.value;
                } else if (request->result.status == FEATURE_ERROR) {
                    addError(info, capabilities_str[request->cap], request->result.message);
                    info->status = STATUS_PARTIAL;
                }
            } else if (request->type == CAPABILITYTYPE_ACTION) {
                Status status = request->result.status == FEATURE_SUCCESS ? STATUS_SUCCESS : STATUS_FAILURE;
                addAction(info, request->cap, device->device_name, status, request->result.value, request->result.message);
            }
        }
    }
}

static void output_json(HeadsetControlStatus* status, HeadsetInfo* infos);
static void output_yaml(HeadsetControlStatus* status, HeadsetInfo* infos);
static void output_env(HeadsetControlStatus* status, HeadsetInfo* infos);
static void output_short(HeadsetControlStatus* status, HeadsetInfo* info, bool print_capabilities);
static void output_standard(HeadsetControlStatus* status, HeadsetInfo* info, bool print_capabilities);

void outputByType(OutputType output, HeadsetControlStatus* status, HeadsetInfo* infos, bool print_capabilities)
{
    switch (output) {
    case OUTPUT_JSON:
        output_json(status, infos);
        break;
    case OUTPUT_YAML:
        output_yaml(status, infos);
        break;
    case OUTPUT_ENV:
        output_env(status, infos);
        break;
    case OUTPUT_STANDARD:
        output_standard(status, infos, print_capabilities);
        break;
    case OUTPUT_SHORT:
        output_short(status, infos, print_capabilities);
        break;
    }
}

void json_print_string(const char* str, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("\"%s\"", str);
}

void json_printint_key_value(const char* key, int value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("\"%s\": \"%d\"", key, value);
}

void json_print_key_value(const char* key, const char* value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("\"%s\": \"%s\"", key, value);
}

void json_printw_key_value(const char* key, const wchar_t* value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("\"%s\": \"%ls\"", key, value);
}

void output_json(HeadsetControlStatus* status, HeadsetInfo* infos)
{
    printf("{\n");

    json_print_key_value("name", status->name, 2);
    printf(",\n");
    json_print_key_value("version", status->version, 2);
    printf(",\n");
    json_print_key_value("api_version", status->api_version, 2);
    printf(",\n");
    json_print_key_value("hidapi_version", status->hid_version, 2);
    printf(",\n");

    if (infos != NULL) {
        for (int j = 0; j < status->device_count; j++) {
            HeadsetInfo* info = &infos[j];
            if (info->action_count > 0) {
                printf("  \"actions\": [\n");
                for (int i = 0; i < info->action_count; i++) {
                    printf("    {\n");

                    json_print_key_value("capability", info->actions[i].capability, 6);
                    printf(",\n");
                    json_print_key_value("device", info->actions[i].device, 6);
                    printf(",\n");
                    json_print_key_value("status", status_to_string(info->actions[i].status), 6);

                    if (info->actions[i].value > 0) {
                        printf(",\n");
                        json_printint_key_value("value", info->actions[i].value, 6);
                    }

                    if (info->actions[i].error_message != NULL && strlen(info->actions[i].error_message) > 0) {
                        printf(",\n");
                        json_print_key_value("error_message", info->actions[i].error_message, 6);
                    }

                    printf("\n    }");
                    if (i < info->action_count - 1) {
                        printf(",\n");
                    }
                }
                printf("\n  ],\n");
            }
        }
    }

    // For integers, direct printing is still simplest
    printf("  \"device_count\": %d,\n", status->device_count);

    printf("  \"devices\": [\n");
    for (int i = 0; i < status->device_count; i++) {
        HeadsetInfo* info = &infos[i];
        printf("    {\n");

        json_print_key_value("status", status_to_string(info->status), 6);
        printf(",\n");
        json_print_key_value("device", info->device_name, 6);
        printf(",\n");
        json_printw_key_value("vendor", info->vendor_name, 6);
        printf(",\n");
        json_printw_key_value("product", info->product_name, 6);
        printf(",\n");
        json_print_key_value("id_vendor", info->idVendor, 6);
        printf(",\n");
        json_print_key_value("id_product", info->idProduct, 6);
        printf(",\n");

        printf("      \"capabilities\": [\n");
        for (int j = 0; j < info->capabilities_amount; j++) {
            json_print_string(info->capabilities[j], 8);
            if (j < info->capabilities_amount - 1) {
                printf(", ");
            }
            printf("\n");
        }
        printf("      ],\n");

        printf("      \"capabilities_str\": [\n");
        for (int j = 0; j < info->capabilities_amount; j++) {
            json_print_string(info->capabilities_str[j], 8);
            if (j < info->capabilities_amount - 1) {
                printf(", ");
            }
            printf("\n");
        }
        printf("      ]");

        if (info->has_battery_info) {
            printf(",\n      \"battery\": {\n");
            json_print_key_value("status", battery_status_to_string(info->battery_status), 8);
            printf(",\n");
            printf("        \"level\": %d\n", info->battery_level);
            printf("      }");
        }

        if (info->has_equalizer_info) {
            printf(",\n      \"equalizer\": {\n");
            printf("        \"bands\": %d,\n", info->equalizer->bands_count);
            printf("        \"baseline\": %d,\n", info->equalizer->bands_baseline);
            printf("        \"step\": %.1f,\n", info->equalizer->bands_step);
            printf("        \"min\": %d,\n", info->equalizer->bands_min);
            printf("        \"max\": %d\n", info->equalizer->bands_max);
            printf("      }");

            if (info->has_equalizer_presets_info) {
                printf(",\n      \"equalizer_presets_count\": %d", info->equalizer_presets->count);
                printf(",\n      \"equalizer_presets\": {\n");
                for (int i = 0; i < info->equalizer_presets->count; i++) {
                    EqualizerPreset* presets = info->equalizer_presets->presets;
                    printf("        \"%s\": [ ", presets[i].name);
                    for (int j = 0; j < info->equalizer->bands_count; j++) {
                        printf("%.1f", presets[i].values[j]);
                        if (j < info->equalizer->bands_count - 1)
                            printf(", ");
                    }
                    printf(" ]");
                    if (i < info->equalizer_presets->count - 1)
                        printf(",\n");
                }
                printf("\n      }");
            }
        }

        if (info->has_parametric_equalizer_info) {
            printf(",\n      \"parametric_equalizer\": {\n");
            printf("        \"bands\": %d,\n", info->parametric_equalizer->bands_count);
            printf("        \"gain\": {\n");
            printf("          \"step\": %g,\n", info->parametric_equalizer->gain_step);
            printf("          \"min\": %g,\n", info->parametric_equalizer->gain_min);
            printf("          \"max\": %g,\n", info->parametric_equalizer->gain_max);
            printf("          \"base\": %g\n", info->parametric_equalizer->gain_base);
            printf("        },\n");
            printf("        \"q_factor\": {\n");
            printf("          \"min\": %g,\n", info->parametric_equalizer->q_factor_min);
            printf("          \"max\": %g\n", info->parametric_equalizer->q_factor_max);
            printf("        },\n");
            printf("        \"frequency\": {\n");
            printf("          \"min\": %d,\n", info->parametric_equalizer->freq_min);
            printf("          \"max\": %d\n", info->parametric_equalizer->freq_max);
            printf("        },\n");
            printf("        \"filter_types\": [\n");
            int first = true;
            for (int i = 0; i < NUM_EQ_FILTER_TYPES; i++) {
                if (has_capability(info->parametric_equalizer->filter_types, i)) {
                    if (!first) { // print first without a leading comma
                        printf(",\n");
                    }
                    printf("          \"%s\"", equalizer_filter_type_str[i]);
                    first = 0;
                }
            }
            printf("\n        ]\n");
            printf("      }");
        }

        if (info->has_chatmix_info) {
            printf(",\n      \"chatmix\": %d", info->chatmix);
        }

        // Start of errors object
        if (info->error_count > 0) {
            printf(",\n      \"errors\": {\n");
            for (int j = 0; j < info->error_count; ++j) {
                json_print_key_value(info->errors[j].source, info->errors[j].message, 8);
                if (j < info->error_count - 1) {
                    printf(",\n");
                }
            }
            printf("\n      }"); // End of errors object
        }

        printf("\n    }"); // Close the device object
        if (i < status->device_count - 1) {
            printf(",\n");
        }
    }
    printf("\n  ]\n"); // Close the devices array
    printf("}\n"); // Close the JSON object
}

static const char* yaml_replace_spaces_with_dash(const char* str);

void yaml_print(const char* key, const char* value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    if (strlen(value) == 0)
        printf("%s:\n", yaml_replace_spaces_with_dash(key));
    else
        printf("%s: \"%s\"\n", yaml_replace_spaces_with_dash(key), value);
}

void yaml_printw(const char* key, const wchar_t* value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }

    printf("%s: \"%ls\"\n", yaml_replace_spaces_with_dash(key), value);
}

void yaml_printint(const char* key, const int value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("%s: %d\n", yaml_replace_spaces_with_dash(key), value);
}

void yaml_printfloat(const char* key, const float value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("%s: %g\n", yaml_replace_spaces_with_dash(key), value);
}

const char* yaml_replace_spaces_with_dash(const char* str)
{
    assert(strlen(str) < 64 && "replace_spaces_with_dash: too long");

    static char result[64];
    int i = 0;
    while (str[i] != '\0') {
        // replace if space, and dont replace the space when a list started with dash
        if (str[i] == ' ' && !(i == 1 && str[0] == '-')) {
            result[i] = '-';
        } else {
            result[i] = str[i];
        }
        i++;
    }
    result[i] = '\0';
    return result;
}

void yaml_print_listitem(const char* value, int indent, bool newline)
{
    if (newline) {
        for (int i = 0; i < indent; i++) {
            putchar(' ');
        }
    }

    printf("- %s", value);

    if (newline) {
        putchar('\n');
    } else {
        putchar(' ');
    }
}

void yaml_print_listitemfloat(const float value, int indent, bool newline)
{
    if (newline) {
        for (int i = 0; i < indent; i++) {
            putchar(' ');
        }
    }

    printf("- %.1f", value);

    if (newline) {
        putchar('\n');
    } else {
        putchar(' ');
    }
}

void output_yaml(HeadsetControlStatus* status, HeadsetInfo* infos)
{
    printf("---\n");
    yaml_print("name", status->name, 0);
    yaml_print("version", status->version, 0);
    yaml_print("api_version", status->api_version, 0);
    yaml_print("hidapi_version", status->hid_version, 0);

    if (infos != NULL) {
        for (int j = 0; j < status->device_count; j++) {
            HeadsetInfo* info = &infos[j];
            if (info->action_count > 0) {
                yaml_print("actions", "", 0);
                for (int i = 0; i < info->action_count; i++) {
                    yaml_print("- capability", info->actions[i].capability, 2);
                    yaml_print("device", info->actions[i].device, 4);
                    yaml_print("status", status_to_string(info->actions[i].status), 4);
                    if (info->actions[i].value > 0)
                        yaml_printint("value", info->actions[i].value, 4);
                    if (info->actions[i].error_message != NULL && strlen(info->actions[i].error_message) > 0) {
                        yaml_print("error_message", info->actions[i].error_message, 4);
                    }
                }
            }
        }
    }

    yaml_printint("device_count", status->device_count, 0);

    if (status->device_count > 0) {
        yaml_print("devices", "", 0);
    }
    for (int i = 0; i < status->device_count; i++) {
        HeadsetInfo* info = &infos[i];
        yaml_print("- status", status_to_string(info->status), 2);
        yaml_print("device", info->device_name, 4);
        yaml_printw("vendor", info->vendor_name, 4);
        yaml_printw("product", info->product_name, 4);
        yaml_print("id_vendor", info->idVendor, 4);
        yaml_print("id_product", info->idProduct, 4);

        yaml_print("capabilities", "", 4);
        for (int j = 0; j < info->capabilities_amount; j++) {
            yaml_print_listitem(info->capabilities[j], 6, true);
        }

        yaml_print("capabilities_str", "", 4);
        for (int j = 0; j < info->capabilities_amount; j++) {
            yaml_print_listitem(info->capabilities_str[j], 6, true);
        }

        if (info->has_battery_info) {
            yaml_print("battery", "", 4);
            yaml_print("status", battery_status_to_string(info->battery_status), 6);
            yaml_printint("level", info->battery_level, 6);
        }

        if (info->has_equalizer_info) {
            yaml_print("equalizer", "", 4);
            yaml_printint("bands", info->equalizer->bands_count, 6);
            yaml_printint("baseline", info->equalizer->bands_baseline, 6);
            yaml_printint("step", info->equalizer->bands_step, 6);
            yaml_printint("min", info->equalizer->bands_min, 6);
            yaml_printint("max", info->equalizer->bands_max, 6);

            if (info->has_equalizer_presets_info) {
                yaml_printint("equalizer_presets_count", info->equalizer_presets->count, 4);
                yaml_print("equalizer_presets", "", 4);
                for (int i = 0; i < info->equalizer_presets->count; i++) {
                    EqualizerPreset* presets = info->equalizer_presets->presets;

                    yaml_print_listitem(presets[i].name, 6, true);

                    // Spaces for the list
                    for (int i = 0; i < 8; i++) {
                        putchar(' ');
                    }
                    for (int j = 0; j < info->equalizer->bands_count; j++) {
                        yaml_print_listitemfloat(presets[i].values[j], 8, false);
                    }
                    putchar('\n');
                }
            }
        }

        if (info->has_parametric_equalizer_info) {
            yaml_print("parametric_equalizer", "", 4);
            yaml_printint("bands", info->parametric_equalizer->bands_count, 6);
            yaml_print("gain", "", 6);
            yaml_printfloat("step", info->parametric_equalizer->gain_step, 8);
            yaml_printfloat("min", info->parametric_equalizer->gain_min, 8);
            yaml_printfloat("max", info->parametric_equalizer->gain_max, 8);
            yaml_printfloat("base", info->parametric_equalizer->gain_base, 8);
            yaml_print("q_factor", "", 6);
            yaml_printfloat("min", info->parametric_equalizer->q_factor_min, 8);
            yaml_printfloat("max", info->parametric_equalizer->q_factor_max, 8);
            yaml_print("frequency", "", 6);
            yaml_printint("min", info->parametric_equalizer->freq_min, 8);
            yaml_printint("max", info->parametric_equalizer->freq_max, 8);
            yaml_print("filter_types", "", 6);
            for (int i = 0; i < NUM_EQ_FILTER_TYPES; i++) {
                if (has_capability(info->parametric_equalizer->filter_types, i)) {
                    yaml_print_listitem(equalizer_filter_type_str[i], 8, true);
                }
            }
        }

        if (info->has_chatmix_info) {
            yaml_printint("chatmix", info->chatmix, 4);
        }

        if (info->error_count > 0) {
            yaml_print("errors", "", 4);
            for (int j = 0; j < info->error_count; ++j) {
                yaml_print(info->errors[j].source, info->errors[j].message, 6);
            }
        }
    }
}

static const char* env_format_key(const char* str);

void env_print(const char* key, const char* value)
{
    printf("%s=\"%s\"\n", env_format_key(key), value);
}

void env_printw(const char* key, const wchar_t* value)
{
    printf("%s=\"%ls\"\n", env_format_key(key), value);
}

void env_printint(const char* key, const int value)
{
    printf("%s=%d\n", env_format_key(key), value);
}

const char* env_format_key(const char* str)
{
    static char result[128];
    int i = 0, j = 0;
    while (str[i] != '\0' && j < (int)(sizeof(result) - 1)) {
        if (str[i] == ' ' || str[i] == '-') {
            result[j++] = '_';
        } else {
            result[j++] = (unsigned char)toupper((unsigned char)str[i]);
        }
        i++;
    }
    result[j] = '\0';
    return result;
}

void output_env(HeadsetControlStatus* status, HeadsetInfo* infos)
{
    env_print("HEADSETCONTROL_NAME", status->name);
    env_print("HEADSETCONTROL_VERSION", status->version);
    env_print("HEADSETCONTROL_API_VERSION", status->api_version);
    env_print("HEADSETCONTROL_HIDAPI_VERSION", status->hid_version);

    if (infos != NULL) {
        for (int j = 0; j < status->device_count; j++) {
            HeadsetInfo* info = &infos[j];
            if (info->action_count > 0) {
                env_printint("ACTION_COUNT", info->action_count);
                for (int i = 0; i < info->action_count; i++) {
                    char prefix[64];
                    sprintf(prefix, "ACTION_%d", i);

                    char key[128];

                    sprintf(key, "%s_CAPABILITY", prefix);
                    env_print(key, info->actions[i].capability);
                    sprintf(key, "%s_DEVICE", prefix);
                    env_print(key, info->actions[i].device);
                    sprintf(key, "%s_STATUS", prefix);
                    env_print(key, status_to_string(info->actions[i].status));

                    if (info->actions[i].value > 0) {
                        sprintf(key, "%s_VALUE", prefix);
                        env_printint(key, info->actions[i].value);
                    }

                    if (info->actions[i].error_message != NULL && strlen(info->actions[i].error_message) > 0) {
                        sprintf(key, "%s_ERROR_MESSAGE", prefix);
                        env_print(key, info->actions[i].error_message);
                    }
                }
            }
        }
    } else {
        env_printint("ACTION_COUNT", 0);
    }

    env_printint("DEVICE_COUNT", status->device_count);
    for (int i = 0; i < status->device_count; i++) {
        HeadsetInfo* info = &infos[i];
        char prefix[64];
        sprintf(prefix, "DEVICE_%d", i);

        env_print(prefix, info->device_name);

        char key[128];
        sprintf(key, "%s_CAPABILITIES_AMOUNT", prefix);
        env_printint(key, info->capabilities_amount);
        for (int j = 0; j < info->capabilities_amount; j++) {
            sprintf(key, "%s_CAPABILITY_%d", prefix, j);
            env_print(key, info->capabilities[j]);
        }

        if (info->has_battery_info) {
            sprintf(key, "%s_BATTERY_STATUS", prefix);
            env_print(key, battery_status_to_string(info->battery_status));
            sprintf(key, "%s_BATTERY_LEVEL", prefix);
            env_printint(key, info->battery_level);
        }

        // Output chatmix information
        if (info->has_chatmix_info) {
            sprintf(key, "%s_CHATMIX", prefix);
            env_printint(key, info->chatmix);
        }

        // Output error information
        snprintf(key, sizeof(key), "%s_ERROR_COUNT", prefix);
        env_printint(key, info->error_count);
        for (int j = 0; j < info->error_count; j++) {
            sprintf(key, "%s_ERROR_%d_SOURCE", prefix, j + 1);
            env_print(key, info->errors[j].source);

            sprintf(key, "%s_ERROR_%d_MESSAGE", prefix, j + 1);
            env_print(key, info->errors[j].message);
        }
    }
}

void output_standard(HeadsetControlStatus* status, HeadsetInfo* infos, bool print_capabilities)
{
    if (status->device_count == 0) {
        printf("No supported device found\n");
        return;
    }
    bool outputted = false;

    printf("Found %d supported device(s):\n", status->device_count);

    for (int i = 0; i < status->device_count; i++) {
        HeadsetInfo* info = &infos[i];
        if (info->product_name != NULL && wcslen(info->product_name) > 0)
            printf(" %s (%ls) [%s:%s]\n", info->device_name, info->product_name, info->idVendor, info->idProduct);
        else
            printf(" %s [%s:%s]\n", info->device_name, info->idVendor, info->idProduct);

        if (print_capabilities) {
            printf("\tCapabilities:\n");
            for (int j = 0; j < info->capabilities_amount; j++) {
                printf("\t* %s\n", info->capabilities_str[j]);
            }

            outputted = true;
            printf("\n");
            continue;
        }

        if (info->has_battery_info) {
            printf("Battery:\n");
            printf("\tStatus: %s\n", battery_status_to_string(info->battery_status));
            if (info->battery_status != BATTERY_UNAVAILABLE)
                printf("\tLevel: %d%%\n", info->battery_level);

            outputted = true;
        }

        if (info->has_chatmix_info) {
            printf("Chatmix: %d\n", infos->chatmix);

            outputted = true;
        }

        for (int j = 0; j < info->error_count; ++j) {
            printf("Error: [%s] %s\n", info->errors[j].source, info->errors[j].message);

            outputted = true;
        }
    }
    if (print_capabilities) {
        printf("Hint: Use --help while the device is connected to get a filtered list of parameters\n");
    }

    for (int j = 0; j < status->device_count; j++) {
        HeadsetInfo* info = &infos[j];
        for (int i = 0; i < info->action_count; i++) {
            outputted = true;

            if (info->actions[i].status == STATUS_SUCCESS) {
                printf("\nSuccessfully set %s!", info->actions[i].capability_str);
            } else {
                printf("\n%s", info->actions[i].error_message);
            }

            printf("\n");
        }
    }

    if (!outputted) {
        printf("\nHeadsetControl (%s) written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd/HeadsetControl\n\n", VERSION);
        printf("You didn't set any arguments, so nothing happened.\nUse -h for help.\n");
    }
}

void output_short(HeadsetControlStatus* status, HeadsetInfo* info, bool print_capabilities)
{
    if (status->device_count == 0) {
        fprintf(stderr, "No supported headset found\n");
    }

    for (int i = 0; i < status->device_count; i++) {
        if (i > 0) {
            fprintf(stderr, "\nWarning: multiple headsets connected but not supported by short output\n");
            break;
        }

        if (info->error_count > 0) {
            for (int j = 0; j < info->error_count; ++j)
                fprintf(stderr, "Error: [%s]: %s\n", info->errors[j].source, info->errors[j].message);

            break;
        }

        if (print_capabilities) {
            for (int j = 0; j < info->capabilities_amount; j++) {
                if (capabilities_str_short[info->capabilities_enum[j]] != '\0')
                    printf("%c", capabilities_str_short[info->capabilities_enum[j]]);
            }

            continue;
        }

        if (info->has_battery_info) {
            if (info->battery_status == BATTERY_CHARGING)
                printf("-1");
            else if (info->battery_status == BATTERY_UNAVAILABLE)
                printf("-2");
            else
                printf("%d", info->battery_level);
        } else if (info->has_chatmix_info) {
            printf("%d", info->chatmix);
        }
    }

    // Could in theory break scripts who rely on stderr for checking other errors
    // So we put it in the end hoping that the respective script does not do it after getting results it needs
    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "\nWarning: short output deprecated, use the -o option instead\n");
}

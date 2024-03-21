#include "output.h"
#include "utility.h"
#include "version.h"

#include <assert.h>
#include <ctype.h>
#include <hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* APIVERSION          = "1.0";
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
    HeadsetInfo* infos          = calloc(num_devices, sizeof(HeadsetInfo));

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
    status.hid_version          = hid_version_str();
    status.device_count         = num_devices;
    return status;
}

void initializeHeadsetInfo(HeadsetInfo* info, struct device* device)
{
    info->status = STATUS_SUCCESS;
    asprintf(&info->idVendor, "0x%04x", device->idVendor);
    asprintf(&info->idProduct, "0x%04x", device->idProduct);
    info->device_name  = device->device_name;
    info->vendor_name  = device->device_hid_vendorname;
    info->product_name = device->device_hid_productname;

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

    if (infos->action_count > 0) {
        printf("  \"actions\": [\n");
        for (int i = 0; i < infos->action_count; i++) {
            printf("    {\n");

            json_print_key_value("capability", infos->actions[i].capability, 6);
            printf(",\n");
            json_print_key_value("device", infos->actions[i].device, 6);
            printf(",\n");
            json_print_key_value("status", status_to_string(infos->actions[i].status), 6);

            if (infos->actions[i].value > 0) {
                printf(",\n");
                json_printint_key_value("value", infos->actions[i].value, 6);
            }

            if (infos->actions[i].error_message != NULL && strlen(infos->actions[i].error_message) > 0) {
                printf(",\n");
                json_print_key_value("error_message", infos->actions[i].error_message, 6);
            }

            printf("\n    }");
            if (i < infos->action_count - 1) {
                printf(",\n");
            }
        }
        printf("\n  ],\n");
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

void yaml_print_listitem(const char* value, int indent)
{
    for (int i = 0; i < indent; i++) {
        putchar(' ');
    }
    printf("- %s\n", value);
}

void output_yaml(HeadsetControlStatus* status, HeadsetInfo* infos)
{
    printf("---\n");
    yaml_print("name", status->name, 0);
    yaml_print("version", status->version, 0);
    yaml_print("api_version", status->api_version, 0);
    yaml_print("hidapi_version", status->hid_version, 0);

    if (infos->action_count > 0) {
        yaml_print("actions", "", 0);

        for (int i = 0; i < infos->action_count; i++) {
            yaml_print("- capability", infos->actions[i].capability, 2);
            yaml_print("device", infos->actions[i].device, 4);
            yaml_print("status", status_to_string(infos->actions[i].status), 4);
            if (infos->actions[i].value > 0)
                yaml_printint("value", infos->actions[i].value, 4);
            if (infos->actions[i].error_message != NULL && strlen(infos->actions[i].error_message) > 0) {
                yaml_print("error_message", infos->actions[i].error_message, 4);
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
            yaml_print_listitem(info->capabilities[j], 6);
        }

        yaml_print("capabilities_str", "", 4);
        for (int j = 0; j < info->capabilities_amount; j++) {
            yaml_print_listitem(info->capabilities_str[j], 6);
        }

        if (info->has_battery_info) {
            yaml_print("battery", "", 4);
            yaml_print("status", battery_status_to_string(info->battery_status), 6);
            yaml_printint("level", info->battery_level, 6);
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
    while (str[i] != '\0' && j < sizeof(result) - 1) {
        if (str[i] == ' ' || str[i] == '-') {
            result[j++] = '_';
        } else {
            result[j++] = toupper((unsigned char)str[i]);
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

    env_printint("ACTION_COUNT", infos->action_count);
    for (int i = 0; i < infos->action_count; i++) {
        char prefix[64];
        sprintf(prefix, "ACTION_%d", i);

        char key[128];

        sprintf(key, "%s_CAPABILITY", prefix);
        env_print(key, infos->actions[i].capability);
        sprintf(key, "%s_DEVICE", prefix);
        env_print(key, infos->actions[i].device);
        sprintf(key, "%s_STATUS", prefix);
        env_print(key, status_to_string(infos->actions[i].status));

        if (infos->actions[i].value > 0) {
            sprintf(key, "%s_VALUE", prefix);
            env_printint(key, infos->actions[i].value);
        }

        if (infos->actions[i].error_message != NULL && strlen(infos->actions[i].error_message) > 0) {
            sprintf(key, "%s_ERROR_MESSAGE", prefix);
            env_print(key, infos->actions[i].error_message);
        }
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

    for (int i = 0; i < status->device_count; i++) {
        HeadsetInfo* info = &infos[i];
        if (info->product_name != NULL && wcslen(info->product_name) > 0)
            printf("Found %s (%ls)!\n\n", info->device_name, info->product_name);
        else
            printf("Found %s!\n\n", info->device_name);

        if (print_capabilities) {
            printf("Capabilities:\n");
            for (int j = 0; j < info->capabilities_amount; j++) {
                printf("* %s\n", info->capabilities_str[j]);
            }

            outputted = true;

            printf("\nHint: Use --help while the device is connected to get a filtered list of parameters\n");
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

        break; // list info only one device supported for now
    }

    for (int i = 0; i < infos->action_count; i++) {
        outputted = true;

        if (infos->actions[i].status == STATUS_SUCCESS) {
            printf("Successfully set %s!\n", infos->actions[i].capability_str);
        } else {
            printf("%s\n", infos->actions[i].error_message);
        }
    }

    if (!outputted) {
        printf("HeadsetControl (%s) written by Sapd (Denis Arnst)\n\thttps://github.com/Sapd/HeadsetControl\n\n", VERSION);
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

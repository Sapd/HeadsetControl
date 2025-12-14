#include "headsetcontrol_c.h"
#include "headsetcontrol.hpp"

#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Internal State
// ============================================================================

namespace {

// Store discovered headsets and their string names
struct HeadsetWrapper {
    headsetcontrol::Headset headset;
    std::string name_str; // Persistent storage for C string

    explicit HeadsetWrapper(headsetcontrol::Headset&& h)
        : headset(std::move(h))
        , name_str(headset.name())
    {
    }
};

// Cache for supported device names
std::vector<std::string> g_supported_device_names;
bool g_supported_devices_cached = false;

void ensureSupportedDevicesCached()
{
    if (!g_supported_devices_cached) {
        g_supported_device_names.clear();
        for (auto name : headsetcontrol::supportedDevices()) {
            g_supported_device_names.emplace_back(name);
        }
        g_supported_devices_cached = true;
    }
}

// Convert C++ error to C error code
hsc_result_t toErrorCode(const headsetcontrol::DeviceError& error)
{
    switch (error.code) {
    case headsetcontrol::DeviceError::Code::Success:
        return HSC_RESULT_OK;
    case headsetcontrol::DeviceError::Code::Timeout:
        return HSC_RESULT_TIMEOUT;
    case headsetcontrol::DeviceError::Code::DeviceOffline:
        return HSC_RESULT_DEVICE_OFFLINE;
    case headsetcontrol::DeviceError::Code::NotSupported:
        return HSC_RESULT_NOT_SUPPORTED;
    case headsetcontrol::DeviceError::Code::InvalidParameter:
        return HSC_RESULT_INVALID_PARAM;
    case headsetcontrol::DeviceError::Code::HIDError:
        return HSC_RESULT_HID_ERROR;
    default:
        return HSC_RESULT_ERROR;
    }
}

// Version string storage
std::string g_version_str;

} // namespace

// ============================================================================
// Discovery Functions
// ============================================================================

extern "C" {

int hsc_discover(hsc_headset_t** headsets)
{
    if (!headsets) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto discovered = headsetcontrol::discover();

    if (discovered.empty()) {
        *headsets = nullptr;
        return 0;
    }

    // Allocate array of handles
    *headsets = static_cast<hsc_headset_t*>(
        malloc(discovered.size() * sizeof(hsc_headset_t)));

    if (!*headsets) {
        return HSC_RESULT_ERROR;
    }

    // Create wrappers for each headset
    for (size_t i = 0; i < discovered.size(); i++) {
        (*headsets)[i] = new HeadsetWrapper(std::move(discovered[i]));
    }

    return static_cast<int>(discovered.size());
}

void hsc_free_headsets(hsc_headset_t* headsets, int count)
{
    if (!headsets) {
        return;
    }

    for (int i = 0; i < count; i++) {
        delete static_cast<HeadsetWrapper*>(headsets[i]);
    }

    // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
    free(headsets);
}

const char* hsc_version(void)
{
    if (g_version_str.empty()) {
        g_version_str = std::string(headsetcontrol::version());
    }
    return g_version_str.c_str();
}

int hsc_supported_device_count(void)
{
    ensureSupportedDevicesCached();
    return static_cast<int>(g_supported_device_names.size());
}

const char* hsc_supported_device_name(int index)
{
    ensureSupportedDevicesCached();
    if (index < 0 || index >= static_cast<int>(g_supported_device_names.size())) {
        return nullptr;
    }
    return g_supported_device_names[index].c_str();
}

// ============================================================================
// Headset Information
// ============================================================================

const char* hsc_get_name(hsc_headset_t headset)
{
    if (!headset) {
        return nullptr;
    }
    return static_cast<HeadsetWrapper*>(headset)->name_str.c_str();
}

uint16_t hsc_get_vendor_id(hsc_headset_t headset)
{
    if (!headset) {
        return 0;
    }
    return static_cast<HeadsetWrapper*>(headset)->headset.vendorId();
}

uint16_t hsc_get_product_id(hsc_headset_t headset)
{
    if (!headset) {
        return 0;
    }
    return static_cast<HeadsetWrapper*>(headset)->headset.productId();
}

bool hsc_supports(hsc_headset_t headset, hsc_capability_t cap)
{
    if (!headset) {
        return false;
    }
    return static_cast<HeadsetWrapper*>(headset)->headset.supports(
        static_cast<capabilities>(cap));
}

int hsc_get_capabilities(hsc_headset_t headset)
{
    if (!headset) {
        return 0;
    }
    return static_cast<HeadsetWrapper*>(headset)->headset.capabilitiesMask();
}

// ============================================================================
// Battery & Status
// ============================================================================

hsc_result_t hsc_get_battery(hsc_headset_t headset, hsc_battery_t* battery)
{
    if (!headset || !battery) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.getBattery();

    if (!result) {
        return toErrorCode(result.error());
    }

    battery->level_percent     = result->level_percent;
    battery->status            = static_cast<hsc_battery_status_t>(result->status);
    battery->voltage_mv        = result->voltage_mv.value_or(-1);
    battery->time_to_full_min  = result->time_to_full_min.value_or(-1);
    battery->time_to_empty_min = result->time_to_empty_min.value_or(-1);

    return HSC_RESULT_OK;
}

hsc_result_t hsc_get_chatmix(hsc_headset_t headset, hsc_chatmix_t* chatmix)
{
    if (!headset || !chatmix) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.getChatmix();

    if (!result) {
        return toErrorCode(result.error());
    }

    chatmix->level               = result->level;
    chatmix->game_volume_percent = result->game_volume_percent;
    chatmix->chat_volume_percent = result->chat_volume_percent;

    return HSC_RESULT_OK;
}

// ============================================================================
// Audio Controls
// ============================================================================

hsc_result_t hsc_set_sidetone(hsc_headset_t headset, uint8_t level, hsc_sidetone_t* result_out)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setSidetone(level);

    if (!result) {
        return toErrorCode(result.error());
    }

    if (result_out) {
        result_out->current_level = result->current_level;
        result_out->min_level     = result->min_level;
        result_out->max_level     = result->max_level;
    }

    return HSC_RESULT_OK;
}

hsc_result_t hsc_set_volume_limiter(hsc_headset_t headset, bool enabled)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setVolumeLimiter(enabled);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

// ============================================================================
// Equalizer
// ============================================================================

hsc_result_t hsc_set_equalizer_preset(hsc_headset_t headset, uint8_t preset)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setEqualizerPreset(preset);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

hsc_result_t hsc_set_equalizer(hsc_headset_t headset, const float* bands, int num_bands)
{
    if (!headset || !bands || num_bands <= 0) {
        return HSC_RESULT_INVALID_PARAM;
    }

    EqualizerSettings settings({ bands, bands + num_bands });
    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setEqualizer(settings);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

int hsc_get_equalizer_presets_count(hsc_headset_t headset)
{
    if (!headset) {
        return 0;
    }
    return static_cast<HeadsetWrapper*>(headset)->headset.getEqualizerPresetsCount();
}

// ============================================================================
// Microphone
// ============================================================================

hsc_result_t hsc_set_mic_volume(hsc_headset_t headset, uint8_t volume)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setMicVolume(volume);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

hsc_result_t hsc_set_mic_mute_led_brightness(hsc_headset_t headset, uint8_t brightness)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setMicMuteLedBrightness(brightness);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

hsc_result_t hsc_set_rotate_to_mute(hsc_headset_t headset, bool enabled)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setRotateToMute(enabled);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

// ============================================================================
// Lights & Audio Cues
// ============================================================================

hsc_result_t hsc_set_lights(hsc_headset_t headset, bool enabled)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setLights(enabled);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

hsc_result_t hsc_set_voice_prompts(hsc_headset_t headset, bool enabled)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setVoicePrompts(enabled);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

hsc_result_t hsc_play_notification_sound(hsc_headset_t headset, uint8_t sound_id)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.playNotificationSound(sound_id);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

// ============================================================================
// Power & Bluetooth
// ============================================================================

hsc_result_t hsc_set_inactive_time(hsc_headset_t headset, uint8_t minutes, hsc_inactive_time_t* result_out)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setInactiveTime(minutes);

    if (!result) {
        return toErrorCode(result.error());
    }

    if (result_out) {
        result_out->minutes     = result->minutes;
        result_out->min_minutes = result->min_minutes;
        result_out->max_minutes = result->max_minutes;
    }

    return HSC_RESULT_OK;
}

hsc_result_t hsc_set_bluetooth_when_powered_on(hsc_headset_t headset, bool enabled)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setBluetoothWhenPoweredOn(enabled);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

hsc_result_t hsc_set_bluetooth_call_volume(hsc_headset_t headset, uint8_t volume)
{
    if (!headset) {
        return HSC_RESULT_INVALID_PARAM;
    }

    auto result = static_cast<HeadsetWrapper*>(headset)->headset.setBluetoothCallVolume(volume);
    return result ? HSC_RESULT_OK : toErrorCode(result.error());
}

// ============================================================================
// Test Device Mode
// ============================================================================

void hsc_enable_test_device(bool enabled)
{
    headsetcontrol::enableTestDevice(enabled);
}

bool hsc_is_test_device_enabled(void)
{
    return headsetcontrol::isTestDeviceEnabled();
}

// ============================================================================
// Configuration
// ============================================================================

void hsc_set_device_timeout(int timeout_ms)
{
    headsetcontrol::setDeviceTimeout(timeout_ms);
}

int hsc_get_device_timeout(void)
{
    return headsetcontrol::getDeviceTimeout();
}

void hsc_set_test_profile(int profile)
{
    headsetcontrol::setTestProfile(profile);
}

int hsc_get_test_profile(void)
{
    return headsetcontrol::getTestProfile();
}

} // extern "C"

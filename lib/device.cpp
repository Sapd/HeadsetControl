#include "device.hpp"

// Order must match capabilities enum in device.hpp
const char* const capabilities_str[NUM_CAPABILITIES] = {
    "sidetone",                      // CAP_SIDETONE
    "battery",                       // CAP_BATTERY_STATUS
    "notification sound",            // CAP_NOTIFICATION_SOUND
    "lights",                        // CAP_LIGHTS
    "inactive time",                 // CAP_INACTIVE_TIME
    "chatmix",                       // CAP_CHATMIX_STATUS
    "voice prompts",                 // CAP_VOICE_PROMPTS
    "rotate to mute",                // CAP_ROTATE_TO_MUTE
    "equalizer preset",              // CAP_EQUALIZER_PRESET
    "equalizer",                     // CAP_EQUALIZER
    "parametric equalizer",          // CAP_PARAMETRIC_EQUALIZER
    "microphone mute led brightness", // CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
    "microphone volume",             // CAP_MICROPHONE_VOLUME
    "volume limiter",                // CAP_VOLUME_LIMITER
    "bluetooth when powered on",     // CAP_BT_WHEN_POWERED_ON
    "bluetooth call volume"          // CAP_BT_CALL_VOLUME
};

// Order must match capabilities enum in device.hpp
const char* const capabilities_str_enum[NUM_CAPABILITIES] = {
    "CAP_SIDETONE",                       // CAP_SIDETONE
    "CAP_BATTERY_STATUS",                 // CAP_BATTERY_STATUS
    "CAP_NOTIFICATION_SOUND",             // CAP_NOTIFICATION_SOUND
    "CAP_LIGHTS",                         // CAP_LIGHTS
    "CAP_INACTIVE_TIME",                  // CAP_INACTIVE_TIME
    "CAP_CHATMIX_STATUS",                 // CAP_CHATMIX_STATUS
    "CAP_VOICE_PROMPTS",                  // CAP_VOICE_PROMPTS
    "CAP_ROTATE_TO_MUTE",                 // CAP_ROTATE_TO_MUTE
    "CAP_EQUALIZER_PRESET",               // CAP_EQUALIZER_PRESET
    "CAP_EQUALIZER",                      // CAP_EQUALIZER
    "CAP_PARAMETRIC_EQUALIZER",           // CAP_PARAMETRIC_EQUALIZER
    "CAP_MICROPHONE_MUTE_LED_BRIGHTNESS", // CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
    "CAP_MICROPHONE_VOLUME",              // CAP_MICROPHONE_VOLUME
    "CAP_VOLUME_LIMITER",                 // CAP_VOLUME_LIMITER
    "CAP_BT_WHEN_POWERED_ON",             // CAP_BT_WHEN_POWERED_ON
    "CAP_BT_CALL_VOLUME"                  // CAP_BT_CALL_VOLUME
};

// Order must match capabilities enum in device.hpp
const char capabilities_str_short[NUM_CAPABILITIES] = {
    's',  // CAP_SIDETONE
    'b',  // CAP_BATTERY_STATUS
    'n',  // CAP_NOTIFICATION_SOUND
    'l',  // CAP_LIGHTS
    'i',  // CAP_INACTIVE_TIME
    'm',  // CAP_CHATMIX_STATUS
    'v',  // CAP_VOICE_PROMPTS
    'r',  // CAP_ROTATE_TO_MUTE
    'p',  // CAP_EQUALIZER_PRESET
    'e',  // CAP_EQUALIZER
    'q',  // CAP_PARAMETRIC_EQUALIZER
    't',  // CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
    'o',  // CAP_MICROPHONE_VOLUME
    '\0', // CAP_VOLUME_LIMITER (new capabilities since short output was deprecated)
    '\0', // CAP_BT_WHEN_POWERED_ON
    '\0'  // CAP_BT_CALL_VOLUME
};

const char* const equalizer_filter_type_str[NUM_EQ_FILTER_TYPES]
    = {
          "lowshelf", // LowShelf
          "lowpass", // LowPass
          "peaking", // Peaking
          "highpass", // HighPass
          "highshelf", // HighShelf
          "notch" // Notch
      };

bool has_capability(int capability_mask, capabilities cap)
{
    return (capability_mask & B(cap)) == B(cap);
}

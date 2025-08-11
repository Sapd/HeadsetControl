#include "device.h"

const char* const capabilities_str[NUM_CAPABILITIES]
    = {
          [CAP_SIDETONE]                       = "sidetone",
          [CAP_BATTERY_STATUS]                 = "battery",
          [CAP_NOTIFICATION_SOUND]             = "notification sound",
          [CAP_LIGHTS]                         = "lights",
          [CAP_INACTIVE_TIME]                  = "inactive time",
          [CAP_CHATMIX_STATUS]                 = "chatmix",
          [CAP_VOICE_PROMPTS]                  = "voice prompts",
          [CAP_ROTATE_TO_MUTE]                 = "rotate to mute",
          [CAP_EQUALIZER_PRESET]               = "equalizer preset",
          [CAP_EQUALIZER]                      = "equalizer",
          [CAP_PARAMETRIC_EQUALIZER]           = "parametric equalizer",
          [CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = "microphone mute led brightness",
          [CAP_MICROPHONE_VOLUME]              = "microphone volume",
          [CAP_VOLUME_LIMITER]                 = "volume limiter",
          [CAP_BT_WHEN_POWERED_ON]             = "bluetooth when powered on",
          [CAP_BT_CALL_VOLUME]                 = "bluetooth call volume"
      };

const char* const capabilities_str_enum[NUM_CAPABILITIES]
    = {
          [CAP_SIDETONE]                       = "CAP_SIDETONE",
          [CAP_BATTERY_STATUS]                 = "CAP_BATTERY_STATUS",
          [CAP_NOTIFICATION_SOUND]             = "CAP_NOTIFICATION_SOUND",
          [CAP_LIGHTS]                         = "CAP_LIGHTS",
          [CAP_INACTIVE_TIME]                  = "CAP_INACTIVE_TIME",
          [CAP_CHATMIX_STATUS]                 = "CAP_CHATMIX_STATUS",
          [CAP_VOICE_PROMPTS]                  = "CAP_VOICE_PROMPTS",
          [CAP_ROTATE_TO_MUTE]                 = "CAP_ROTATE_TO_MUTE",
          [CAP_EQUALIZER_PRESET]               = "CAP_EQUALIZER_PRESET",
          [CAP_EQUALIZER]                      = "CAP_EQUALIZER",
          [CAP_PARAMETRIC_EQUALIZER]           = "CAP_PARAMETRIC_EQUALIZER",
          [CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = "CAP_MICROPHONE_MUTE_LED_BRIGHTNESS",
          [CAP_MICROPHONE_VOLUME]              = "CAP_MICROPHONE_VOLUME",
          [CAP_VOLUME_LIMITER]                 = "CAP_VOLUME_LIMITER",
          [CAP_BT_WHEN_POWERED_ON]             = "CAP_BT_WHEN_POWERED_ON",
          [CAP_BT_CALL_VOLUME]                 = "CAP_BT_CALL_VOLUME"
      };

const char capabilities_str_short[NUM_CAPABILITIES]
    = {
          [CAP_SIDETONE]                       = 's',
          [CAP_BATTERY_STATUS]                 = 'b',
          [CAP_NOTIFICATION_SOUND]             = 'n',
          [CAP_LIGHTS]                         = 'l',
          [CAP_INACTIVE_TIME]                  = 'i',
          [CAP_CHATMIX_STATUS]                 = 'm',
          [CAP_VOICE_PROMPTS]                  = 'v',
          [CAP_ROTATE_TO_MUTE]                 = 'r',
          [CAP_EQUALIZER_PRESET]               = 'p',
          [CAP_EQUALIZER]                      = 'e',
          [CAP_PARAMETRIC_EQUALIZER]           = 'q',
          [CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = 't',
          [CAP_MICROPHONE_VOLUME]              = 'o',
          // new capabilities since short output was deprecated
          [CAP_VOLUME_LIMITER]     = '\0',
          [CAP_BT_WHEN_POWERED_ON] = '\0',
          [CAP_BT_CALL_VOLUME]     = '\0'
      };

const char* const equalizer_filter_type_str[NUM_EQ_FILTER_TYPES]
    = {
          [EQ_FILTER_LOWSHELF]  = "lowshelf",
          [EQ_FILTER_LOWPASS]   = "lowpass",
          [EQ_FILTER_PEAKING]   = "peaking",
          [EQ_FILTER_HIGHPASS]  = "highpass",
          [EQ_FILTER_HIGHSHELF] = "highshelf",
          [EQ_FILTER_NOTCH]     = "notch"

      };

bool device_check_ids(struct device* device, uint16_t vid, uint16_t pid)
{
    return device->idVendor == vid && device->idProduct == pid;
}

bool device_has_capability(struct device* device, enum capabilities cap)
{
    if (device == NULL)
        return false;
    return (device->capabilities & B(cap)) == B(cap);
}

bool has_capability(int capabilities, enum capabilities cap)
{
    return (capabilities & B(cap)) == B(cap);
}

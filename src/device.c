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
          [CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = "microphone mute led brightness",
          [CAP_MICROPHONE_VOLUME]              = "microphone volume"
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
          [CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = "CAP_MICROPHONE_MUTE_LED_BRIGHTNESS",
          [CAP_MICROPHONE_VOLUME]              = "CAP_MICROPHONE_VOLUME"
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
          [CAP_MICROPHONE_MUTE_LED_BRIGHTNESS] = 't',
          [CAP_MICROPHONE_VOLUME]              = 'o'
      };

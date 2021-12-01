#include "device.h"

const char* const capabilities_str[NUM_CAPABILITIES]
    = {
          [CAP_SIDETONE]           = "sidetone",
          [CAP_BATTERY_STATUS]     = "battery status",
          [CAP_NOTIFICATION_SOUND] = "notification sound",
          [CAP_LIGHTS]             = "lights",
          [CAP_INACTIVE_TIME]      = "inactive time",
          [CAP_CHATMIX_STATUS]     = "chatmix",
          [CAP_VOICE_PROMPTS]      = "voice prompts",
          [CAP_ROTATE_TO_MUTE]     = "rotate to mute",
      };

const char capabilities_str_short[NUM_CAPABILITIES]
    = {
          [CAP_SIDETONE]           = 's',
          [CAP_BATTERY_STATUS]     = 'b',
          [CAP_NOTIFICATION_SOUND] = 'n',
          [CAP_LIGHTS]             = 'l',
          [CAP_INACTIVE_TIME]      = 'i',
          [CAP_CHATMIX_STATUS]     = 'm',
          [CAP_VOICE_PROMPTS]      = 'v',
          [CAP_ROTATE_TO_MUTE]     = 'r',
      };
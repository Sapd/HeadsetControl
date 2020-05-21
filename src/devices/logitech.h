#pragma once

// +---------------------------------------------------------------------+
// ------------------------ HID++ Definitions ----------------------------
// +---------------------------------------------------------------------+

// See https://lekensteyn.nl/files/logitech
// The definitions here are for now valid for HID++ version 1 and 2

// +---------------------------------------------------------------------------+
// | Index | 0              | 1                  | 2          | 3 to 6 or 19   |
// | Field | Report ID      | Device Index       | Sub ID     | Parameter      |
// | Desc  | Message length | Destination/Origin | Msg number | Data           |
// +---------------------------------------------------------------------------+

// Message length identifier (Report ID)
#define HIDPP_SHORT_MESSAGE 0x10
#define HIDPP_LONG_MESSAGE  0x11

#define HIDPP_SHORT_MESSAGE_LENGTH 7
#define HIDPP_LONG_MESSAGE_LENGTH  20

// Device index
#define HIDPP_DEVICE_RECEIVER 0xff

# Using HeadsetControl as a Library

There are two ways to integrate HeadsetControl into your application:

1. **CLI Output Parsing** (Simple) - Run `headsetcontrol` as a subprocess and parse JSON/YAML output
2. **Library Linking** (Advanced) - Link against `libheadsetcontrol` for direct C/C++ integration

## CLI Output API

The simplest way to use HeadsetControl is via its structured output formats. This works from any language that can spawn processes.

### Output Formats

```bash
# JSON (recommended for parsing)
headsetcontrol -o json

# YAML
headsetcontrol -o yaml

# Environment variables (for shell scripts)
headsetcontrol -o env
```

### JSON Output Structure

```json
{
  "name": "HeadsetControl",
  "version": "3.0.0",
  "api_version": "1.4",
  "device_count": 1,
  "devices": [
    {
      "status": "success",
      "device": "Corsair VOID Pro",
      "id_vendor": "0x1b1c",
      "id_product": "0x1b27",
      "capabilities": ["CAP_SIDETONE", "CAP_BATTERY_STATUS", "CAP_LIGHTS"],
      "capabilities_str": ["sidetone", "battery", "lights"],
      "battery": {
        "status": "BATTERY_AVAILABLE",
        "level": 85
      }
    }
  ]
}
```

### Status Values

| Status | Description |
|--------|-------------|
| `success` | All queries completed normally |
| `partial` | Some queries failed |
| `failure` | Device communication failed |

### Battery Status Values

| Status | Description |
|--------|-------------|
| `BATTERY_AVAILABLE` | Battery level available |
| `BATTERY_CHARGING` | Currently charging (level may be -1) |
| `BATTERY_UNAVAILABLE` | Device unavailable/off |

### Performing Actions

Query and set values in one call:

```bash
# Set sidetone and get battery
headsetcontrol -s 64 -b -o json
```

Action results include status:
```json
{
  "devices": [{
    "sidetone": {
      "status": "success",
      "level": 64
    },
    "battery": {
      "status": "BATTERY_AVAILABLE",
      "level": 85
    }
  }]
}
```

### API Versioning

The `api_version` field uses semantic versioning:
- First number increments on **breaking changes**
- Second number increments on **additions**

Check this to ensure compatibility with your parser.

### Shell Script Example

```bash
#!/bin/bash
# Get battery level using jq

BATTERY=$(headsetcontrol -o json | jq -r '.devices[0].battery.level // "N/A"')
echo "Battery: $BATTERY%"
```

### Python Example (subprocess)

```python
import json
import subprocess

result = subprocess.run(['headsetcontrol', '-o', 'json'], capture_output=True, text=True)
data = json.loads(result.stdout)

for device in data.get('devices', []):
    print(f"{device['device']}: {device.get('battery', {}).get('level', 'N/A')}%")
```

---

## C++ Library API

For tighter integration, link against the HeadsetControl library directly. The library provides a high-level API that completely abstracts away HID details - you just work with `Headset` objects.

## Quick Start

```cpp
#include <headsetcontrol.hpp>
#include <iostream>

int main() {
    // Discover connected headsets (library auto-initializes)
    auto headsets = headsetcontrol::discover();

    if (headsets.empty()) {
        std::cout << "No supported headsets found\n";
        return 1;
    }

    for (auto& headset : headsets) {
        std::cout << "Found: " << headset.name() << "\n";

        // Check battery if supported
        if (headset.supports(CAP_BATTERY_STATUS)) {
            auto battery = headset.getBattery();
            if (battery) {
                std::cout << "  Battery: " << battery->level_percent << "%\n";
            }
        }

        // Set sidetone if supported
        if (headset.supports(CAP_SIDETONE)) {
            auto result = headset.setSidetone(64);
            if (result) {
                std::cout << "  Sidetone set to 64\n";
            }
        }
    }

    return 0;
}
```

## Building the Library

### Static Library (default)

```bash
mkdir build && cd build
cmake ..
make headsetcontrol_lib
```

This produces `libheadsetcontrol.a` (or `.lib` on Windows).

### Shared Library (for FFI bindings)

```bash
mkdir build && cd build
cmake -DBUILD_SHARED_LIBRARY=ON ..
make
```

This produces both `libheadsetcontrol.a` (static) and `libheadsetcontrol.dylib/.so/.dll` (shared).
The shared library is versioned with SOVERSION for proper linking.

## Installation

```bash
sudo make install

# Installs:
# - libheadsetcontrol.a to /usr/local/lib/
# - headsetcontrol.hpp, device.hpp, result_types.hpp to /usr/local/include/headsetcontrol/
```

## Linking with CMake

### Option 1: As a subdirectory (recommended)

```cmake
cmake_minimum_required(VERSION 3.12)
project(my_headset_app)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add HeadsetControl as subdirectory
add_subdirectory(HeadsetControl)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE headsetcontrol_lib)
```

### Option 2: After installation

```cmake
cmake_minimum_required(VERSION 3.12)
project(my_headset_app)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(HEADSETCONTROL_LIB headsetcontrol)
find_package(hidapi REQUIRED)

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE /usr/local/include/headsetcontrol)
target_link_libraries(my_app ${HEADSETCONTROL_LIB} ${HIDAPI_LIBRARIES} m)
```

## API Reference

### Discovery

```cpp
// Discover connected headsets (one per model)
std::vector<headsetcontrol::Headset> headsets = headsetcontrol::discover();

// Discover all (including duplicates of same model)
std::vector<headsetcontrol::Headset> all = headsetcontrol::discoverAll();

// Get library version
std::string_view ver = headsetcontrol::version();

// List all supported device names (even if not connected)
std::vector<std::string_view> supported = headsetcontrol::supportedDevices();
```

### Headset Information

```cpp
headsetcontrol::Headset& headset = headsets[0];

// Device info
std::string_view name = headset.name();           // "Logitech G535"
uint16_t vendor = headset.vendorId();             // 0x046d
uint16_t product = headset.productId();           // 0x0ac4

// Capabilities
bool hasBattery = headset.supports(CAP_BATTERY_STATUS);
int capsMask = headset.capabilitiesMask();        // Bitmask
std::vector<std::string_view> capNames = headset.capabilityNames();
```

### Battery & Status

```cpp
// Get battery status
if (headset.supports(CAP_BATTERY_STATUS)) {
    auto result = headset.getBattery();
    if (result) {
        std::cout << "Level: " << result->level_percent << "%\n";

        if (result->status == BATTERY_CHARGING) {
            std::cout << "Charging\n";
        }

        // Optional extended info (device-dependent)
        if (result->voltage_mv) {
            std::cout << "Voltage: " << *result->voltage_mv << " mV\n";
        }
    } else {
        std::cerr << "Error: " << result.error().message << "\n";
    }
}

// Get chat-mix level
if (headset.supports(CAP_CHATMIX_STATUS)) {
    auto result = headset.getChatmix();
    if (result) {
        std::cout << "Chat-mix: " << result->level << "\n";
    }
}
```

### Audio Controls

```cpp
// Sidetone (0-128, 0 = off)
if (headset.supports(CAP_SIDETONE)) {
    auto result = headset.setSidetone(64);
    if (result) {
        std::cout << "Sidetone range: " << (int)result->min_level
                  << "-" << (int)result->max_level << "\n";
    }
}

// Volume limiter
if (headset.supports(CAP_VOLUME_LIMITER)) {
    headset.setVolumeLimiter(true);  // Enable
}
```

### Equalizer

```cpp
// Set preset
if (headset.supports(CAP_EQUALIZER_PRESET)) {
    headset.setEqualizerPreset(2);  // Preset #2

    int presetCount = headset.getEqualizerPresetsCount();
}

// Custom EQ curve
if (headset.supports(CAP_EQUALIZER)) {
    EqualizerSettings eq({ 0.0f, 2.0f, 4.0f, 2.0f, 0.0f, -2.0f });
    headset.setEqualizer(eq);

    // Get EQ info
    if (auto info = headset.getEqualizerInfo()) {
        std::cout << "Bands: " << info->bands_count << "\n";
        std::cout << "Range: " << info->bands_min << " to " << info->bands_max << "\n";
    }
}

// Parametric EQ
if (headset.supports(CAP_PARAMETRIC_EQUALIZER)) {
    ParametricEqualizerSettings peq;
    peq.bands.push_back({
        .frequency = 1000.0f,
        .gain = 3.0f,
        .q_factor = 1.0f,
        .type = EqualizerFilterType::Peaking
    });
    headset.setParametricEqualizer(peq);
}
```

### Microphone

```cpp
// Mic volume (0-128)
if (headset.supports(CAP_MICROPHONE_VOLUME)) {
    headset.setMicVolume(100);
}

// Mute LED brightness (0-3)
if (headset.supports(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS)) {
    headset.setMicMuteLedBrightness(2);
}

// Rotate-to-mute
if (headset.supports(CAP_ROTATE_TO_MUTE)) {
    headset.setRotateToMute(true);
}
```

### Lights & Audio Cues

```cpp
// Lights on/off
if (headset.supports(CAP_LIGHTS)) {
    headset.setLights(true);
}

// Voice prompts on/off
if (headset.supports(CAP_VOICE_PROMPTS)) {
    headset.setVoicePrompts(false);
}

// Play notification sound
if (headset.supports(CAP_NOTIFICATION_SOUND)) {
    headset.playNotificationSound(1);  // Sound ID 1
}
```

### Power & Bluetooth

```cpp
// Auto power-off (0 = disabled, 1-90 minutes)
if (headset.supports(CAP_INACTIVE_TIME)) {
    headset.setInactiveTime(30);  // 30 minutes
}

// Bluetooth settings
if (headset.supports(CAP_BT_WHEN_POWERED_ON)) {
    headset.setBluetoothWhenPoweredOn(true);
}

if (headset.supports(CAP_BT_CALL_VOLUME)) {
    headset.setBluetoothCallVolume(2);
}
```

## Error Handling

All feature methods return `Result<T>` for proper error handling:

```cpp
auto result = headset.getBattery();

if (result.hasValue()) {
    // Success - access the value
    const auto& battery = result.value();
    // Or use: *result, result->level_percent
} else {
    // Error - handle it
    const auto& error = result.error();

    switch (error.code) {
    case DeviceError::Code::Timeout:
        std::cerr << "Device didn't respond\n";
        break;
    case DeviceError::Code::DeviceOffline:
        std::cerr << "Headset is offline\n";
        break;
    case DeviceError::Code::NotSupported:
        std::cerr << "Feature not supported\n";
        break;
    default:
        std::cerr << "Error: " << error.message << "\n";
    }
}

// Or use the boolean conversion
if (result) {
    std::cout << "Battery: " << result->level_percent << "%\n";
}
```

## Complete Example

```cpp
#include <headsetcontrol.hpp>
#include <iostream>
#include <iomanip>

void printHeadsetInfo(headsetcontrol::Headset& headset) {
    std::cout << "=== " << headset.name() << " ===\n";
    std::cout << "Vendor:  0x" << std::hex << headset.vendorId() << "\n";
    std::cout << "Product: 0x" << std::hex << headset.productId() << std::dec << "\n";

    std::cout << "Capabilities: ";
    for (auto cap : headset.capabilityNames()) {
        std::cout << cap << " ";
    }
    std::cout << "\n\n";

    // Battery
    if (headset.supports(CAP_BATTERY_STATUS)) {
        auto battery = headset.getBattery();
        if (battery) {
            std::cout << "Battery: " << battery->level_percent << "%";
            if (battery->status == BATTERY_CHARGING) {
                std::cout << " (charging)";
            }
            std::cout << "\n";
        }
    }

    // Chat-mix
    if (headset.supports(CAP_CHATMIX_STATUS)) {
        auto chatmix = headset.getChatmix();
        if (chatmix) {
            std::cout << "Chat-mix: " << chatmix->level << "\n";
        }
    }
}

int main() {
    std::cout << "HeadsetControl Library v" << headsetcontrol::version() << "\n\n";

    auto headsets = headsetcontrol::discover();

    if (headsets.empty()) {
        std::cout << "No supported headsets found.\n\n";
        std::cout << "Supported devices:\n";
        for (auto name : headsetcontrol::supportedDevices()) {
            std::cout << "  - " << name << "\n";
        }
        return 1;
    }

    for (auto& headset : headsets) {
        printHeadsetInfo(headset);
    }

    return 0;
}
```

## Test Device Mode

For development and testing without physical hardware, enable the test device:

```cpp
// C++ API
headsetcontrol::enableTestDevice(true);
auto headsets = headsetcontrol::discover();  // Includes test device (0xF00B:0xA00C)

// Check if test device is enabled
bool enabled = headsetcontrol::isTestDeviceEnabled();
```

```c
// C API
hsc_enable_test_device(true);
hsc_headset_t* headsets;
int count = hsc_discover(&headsets);  // Includes test device
```

The test device implements all capabilities and returns predictable values, making it useful for:
- Testing library integration
- Developing GUI applications
- CI/CD pipelines
- Unit tests

## Thread Safety

- `discover()` and `discoverAll()` are thread-safe
- Individual `Headset` objects are NOT thread-safe
- Use external synchronization if accessing the same headset from multiple threads
- Each thread should have its own `Headset` object if possible

## Platform Notes

### Linux
- Requires udev rules for non-root access
- Generate with: `headsetcontrol -u > /etc/udev/rules.d/70-headset.rules`
- Reload: `sudo udevadm control --reload-rules && sudo udevadm trigger`

### macOS
- No special permissions needed

### Windows
- Uses SetupAPI for device access
- May require running as Administrator for some devices

## Dependencies

When linking manually, you need:
- **HIDAPI**: `libhidapi` (automatically linked when using CMake subdirectory)
- **Math library**: `-lm` on Linux/macOS

---

# C API

HeadsetControl also provides a pure C API for use from C programs or for FFI bindings from other languages (Python, Rust, etc.).

## C Quick Start

```c
#include <headsetcontrol_c.h>
#include <stdio.h>

int main() {
    // Discover headsets
    hsc_headset_t* headsets = NULL;
    int count = hsc_discover(&headsets);

    if (count <= 0) {
        printf("No headsets found\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        printf("Found: %s\n", hsc_get_name(headsets[i]));

        // Check battery
        if (hsc_supports(headsets[i], HSC_CAP_BATTERY_STATUS)) {
            hsc_battery_t battery;
            if (hsc_get_battery(headsets[i], &battery) == HSC_RESULT_OK) {
                printf("  Battery: %d%%\n", battery.level_percent);
            }
        }

        // Set sidetone
        if (hsc_supports(headsets[i], HSC_CAP_SIDETONE)) {
            if (hsc_set_sidetone(headsets[i], 64, NULL) == HSC_RESULT_OK) {
                printf("  Sidetone set to 64\n");
            }
        }
    }

    // Cleanup
    hsc_free_headsets(headsets, count);
    return 0;
}
```

## C API Reference

### Discovery

```c
// Discover connected headsets
hsc_headset_t* headsets = NULL;
int count = hsc_discover(&headsets);  // Returns count or negative error

// Free when done
hsc_free_headsets(headsets, count);

// Get library version
const char* version = hsc_version();

// List supported devices
int num_devices = hsc_supported_device_count();
for (int i = 0; i < num_devices; i++) {
    printf("%s\n", hsc_supported_device_name(i));
}
```

### Headset Information

```c
const char* name = hsc_get_name(headset);
uint16_t vendor = hsc_get_vendor_id(headset);
uint16_t product = hsc_get_product_id(headset);

// Check capability
if (hsc_supports(headset, HSC_CAP_BATTERY_STATUS)) {
    // ...
}

// Get all capabilities as bitmask
int caps = hsc_get_capabilities(headset);
```

### Battery

```c
hsc_battery_t battery;
hsc_result_t result = hsc_get_battery(headset, &battery);

if (result == HSC_RESULT_OK) {
    printf("Level: %d%%\n", battery.level_percent);

    if (battery.status == HSC_BATTERY_CHARGING) {
        printf("Charging\n");
    }

    if (battery.voltage_mv >= 0) {
        printf("Voltage: %d mV\n", battery.voltage_mv);
    }
}
```

### Setting Features

```c
// Sidetone (0-128)
hsc_sidetone_t sidetone_result;
hsc_set_sidetone(headset, 64, &sidetone_result);  // result param is optional (NULL ok)

// Lights
hsc_set_lights(headset, true);   // on
hsc_set_lights(headset, false);  // off

// Inactive time (minutes, 0 = disabled)
hsc_inactive_time_t time_result;
hsc_set_inactive_time(headset, 30, &time_result);

// Equalizer preset
hsc_set_equalizer_preset(headset, 2);

// Custom EQ
float bands[] = { 0.0f, 2.0f, 4.0f, 2.0f, 0.0f, -2.0f };
hsc_set_equalizer(headset, bands, 6);

// Mic volume
hsc_set_mic_volume(headset, 100);

// Voice prompts
hsc_set_voice_prompts(headset, false);
```

### Error Handling

```c
hsc_result_t result = hsc_get_battery(headset, &battery);

switch (result) {
case HSC_RESULT_OK:
    // Success
    break;
case HSC_RESULT_NOT_SUPPORTED:
    printf("Feature not supported\n");
    break;
case HSC_RESULT_TIMEOUT:
    printf("Device timeout\n");
    break;
case HSC_RESULT_DEVICE_OFFLINE:
    printf("Device offline\n");
    break;
case HSC_RESULT_HID_ERROR:
    printf("HID communication error\n");
    break;
default:
    printf("Error: %d\n", result);
}
```

### Capabilities

```c
// Available capability constants
HSC_CAP_SIDETONE
HSC_CAP_BATTERY_STATUS
HSC_CAP_NOTIFICATION_SOUND
HSC_CAP_LIGHTS
HSC_CAP_INACTIVE_TIME
HSC_CAP_CHATMIX_STATUS
HSC_CAP_VOICE_PROMPTS
HSC_CAP_ROTATE_TO_MUTE
HSC_CAP_EQUALIZER_PRESET
HSC_CAP_EQUALIZER
HSC_CAP_PARAMETRIC_EQUALIZER
HSC_CAP_MICROPHONE_MUTE_LED_BRIGHTNESS
HSC_CAP_MICROPHONE_VOLUME
HSC_CAP_VOLUME_LIMITER
HSC_CAP_BT_WHEN_POWERED_ON
HSC_CAP_BT_CALL_VOLUME
```

## Compiling C Programs

```bash
# After installing the library
gcc -o my_app my_app.c -lheadsetcontrol -lhidapi -lm -lstdc++

# With pkg-config (if configured)
gcc -o my_app my_app.c $(pkg-config --cflags --libs headsetcontrol)
```

## FFI Bindings

The C API is designed for easy FFI bindings. Key points:

- All handles are opaque `void*` pointers
- All strings returned are owned by the library (don't free them)
- Memory allocated by `hsc_discover()` must be freed with `hsc_free_headsets()`
- All functions are `extern "C"` with C linkage
- No exceptions - all errors are returned as `hsc_result_t` codes

### Python Example (ctypes)

```python
#!/usr/bin/env python3
"""
HeadsetControl Python bindings using ctypes.

Requires: libheadsetcontrol.so/.dylib/.dll in library path
"""

import ctypes
from ctypes import c_int, c_uint8, c_uint16, c_char_p, c_void_p, c_bool, c_float, POINTER, Structure
from enum import IntEnum
from typing import Optional, List
import platform

# Load the library
if platform.system() == "Darwin":
    _lib = ctypes.CDLL("libheadsetcontrol.dylib")
elif platform.system() == "Windows":
    _lib = ctypes.CDLL("headsetcontrol.dll")
else:
    _lib = ctypes.CDLL("libheadsetcontrol.so")


# Result codes
class Result(IntEnum):
    OK = 0
    ERROR = -1
    NOT_SUPPORTED = -2
    DEVICE_OFFLINE = -3
    TIMEOUT = -4
    HID_ERROR = -5
    INVALID_PARAM = -6


# Capabilities
class Capability(IntEnum):
    SIDETONE = 0
    BATTERY_STATUS = 1
    NOTIFICATION_SOUND = 2
    LIGHTS = 3
    INACTIVE_TIME = 4
    CHATMIX_STATUS = 5
    VOICE_PROMPTS = 6
    ROTATE_TO_MUTE = 7
    EQUALIZER_PRESET = 8
    EQUALIZER = 9
    PARAMETRIC_EQUALIZER = 10
    MICROPHONE_MUTE_LED_BRIGHTNESS = 11
    MICROPHONE_VOLUME = 12
    VOLUME_LIMITER = 13
    BT_WHEN_POWERED_ON = 14
    BT_CALL_VOLUME = 15


# Battery status
class BatteryStatus(IntEnum):
    UNAVAILABLE = -1
    CHARGING = -2
    AVAILABLE = 0


# Structures
class BatteryInfo(Structure):
    _fields_ = [
        ("level_percent", c_int),
        ("status", c_int),
        ("voltage_mv", c_int),
        ("time_to_full_min", c_int),
        ("time_to_empty_min", c_int),
    ]


class SidetoneInfo(Structure):
    _fields_ = [
        ("current_level", c_uint8),
        ("min_level", c_uint8),
        ("max_level", c_uint8),
    ]


class ChatmixInfo(Structure):
    _fields_ = [
        ("level", c_int),
        ("game_volume_percent", c_int),
        ("chat_volume_percent", c_int),
    ]


# Function signatures
_lib.hsc_discover.argtypes = [POINTER(c_void_p)]
_lib.hsc_discover.restype = c_int

_lib.hsc_free_headsets.argtypes = [c_void_p, c_int]
_lib.hsc_free_headsets.restype = None

_lib.hsc_version.argtypes = []
_lib.hsc_version.restype = c_char_p

_lib.hsc_get_name.argtypes = [c_void_p]
_lib.hsc_get_name.restype = c_char_p

_lib.hsc_get_vendor_id.argtypes = [c_void_p]
_lib.hsc_get_vendor_id.restype = c_uint16

_lib.hsc_get_product_id.argtypes = [c_void_p]
_lib.hsc_get_product_id.restype = c_uint16

_lib.hsc_supports.argtypes = [c_void_p, c_int]
_lib.hsc_supports.restype = c_bool

_lib.hsc_get_capabilities.argtypes = [c_void_p]
_lib.hsc_get_capabilities.restype = c_int

_lib.hsc_get_battery.argtypes = [c_void_p, POINTER(BatteryInfo)]
_lib.hsc_get_battery.restype = c_int

_lib.hsc_get_chatmix.argtypes = [c_void_p, POINTER(ChatmixInfo)]
_lib.hsc_get_chatmix.restype = c_int

_lib.hsc_set_sidetone.argtypes = [c_void_p, c_uint8, POINTER(SidetoneInfo)]
_lib.hsc_set_sidetone.restype = c_int

_lib.hsc_set_lights.argtypes = [c_void_p, c_bool]
_lib.hsc_set_lights.restype = c_int

_lib.hsc_set_inactive_time.argtypes = [c_void_p, c_uint8, c_void_p]
_lib.hsc_set_inactive_time.restype = c_int

_lib.hsc_set_voice_prompts.argtypes = [c_void_p, c_bool]
_lib.hsc_set_voice_prompts.restype = c_int

_lib.hsc_set_equalizer_preset.argtypes = [c_void_p, c_uint8]
_lib.hsc_set_equalizer_preset.restype = c_int

_lib.hsc_set_equalizer.argtypes = [c_void_p, POINTER(c_float), c_int]
_lib.hsc_set_equalizer.restype = c_int


class Headset:
    """Wrapper for a HeadsetControl device."""

    def __init__(self, handle: c_void_p):
        self._handle = handle

    @property
    def name(self) -> str:
        return _lib.hsc_get_name(self._handle).decode("utf-8")

    @property
    def vendor_id(self) -> int:
        return _lib.hsc_get_vendor_id(self._handle)

    @property
    def product_id(self) -> int:
        return _lib.hsc_get_product_id(self._handle)

    def supports(self, cap: Capability) -> bool:
        return _lib.hsc_supports(self._handle, cap)

    def get_battery(self) -> Optional[BatteryInfo]:
        """Get battery status. Returns None on error."""
        if not self.supports(Capability.BATTERY_STATUS):
            return None
        info = BatteryInfo()
        if _lib.hsc_get_battery(self._handle, ctypes.byref(info)) == Result.OK:
            return info
        return None

    def get_chatmix(self) -> Optional[ChatmixInfo]:
        """Get chat-mix level. Returns None on error."""
        if not self.supports(Capability.CHATMIX_STATUS):
            return None
        info = ChatmixInfo()
        if _lib.hsc_get_chatmix(self._handle, ctypes.byref(info)) == Result.OK:
            return info
        return None

    def set_sidetone(self, level: int) -> bool:
        """Set sidetone level (0-128). Returns True on success."""
        if not self.supports(Capability.SIDETONE):
            return False
        return _lib.hsc_set_sidetone(self._handle, level, None) == Result.OK

    def set_lights(self, enabled: bool) -> bool:
        """Set lights on/off. Returns True on success."""
        if not self.supports(Capability.LIGHTS):
            return False
        return _lib.hsc_set_lights(self._handle, enabled) == Result.OK

    def set_inactive_time(self, minutes: int) -> bool:
        """Set auto power-off time (0 = disabled). Returns True on success."""
        if not self.supports(Capability.INACTIVE_TIME):
            return False
        return _lib.hsc_set_inactive_time(self._handle, minutes, None) == Result.OK

    def set_voice_prompts(self, enabled: bool) -> bool:
        """Set voice prompts on/off. Returns True on success."""
        if not self.supports(Capability.VOICE_PROMPTS):
            return False
        return _lib.hsc_set_voice_prompts(self._handle, enabled) == Result.OK

    def set_equalizer_preset(self, preset: int) -> bool:
        """Set equalizer preset. Returns True on success."""
        if not self.supports(Capability.EQUALIZER_PRESET):
            return False
        return _lib.hsc_set_equalizer_preset(self._handle, preset) == Result.OK

    def set_equalizer(self, bands: List[float]) -> bool:
        """Set custom equalizer curve. Returns True on success."""
        if not self.supports(Capability.EQUALIZER):
            return False
        arr = (c_float * len(bands))(*bands)
        return _lib.hsc_set_equalizer(self._handle, arr, len(bands)) == Result.OK


class HeadsetControl:
    """Main interface to HeadsetControl library."""

    def __init__(self):
        self._headsets_ptr = None
        self._count = 0
        self._headsets: List[Headset] = []

    def __enter__(self):
        self.discover()
        return self

    def __exit__(self, *args):
        self.close()

    def discover(self) -> List[Headset]:
        """Discover connected headsets."""
        self.close()  # Free any previous discovery

        ptr = c_void_p()
        count = _lib.hsc_discover(ctypes.byref(ptr))

        if count <= 0:
            return []

        self._headsets_ptr = ptr
        self._count = count

        # Create array of void pointers from the returned pointer
        arr = ctypes.cast(ptr, POINTER(c_void_p * count)).contents
        self._headsets = [Headset(arr[i]) for i in range(count)]

        return self._headsets

    def close(self):
        """Free discovered headsets."""
        if self._headsets_ptr and self._count > 0:
            _lib.hsc_free_headsets(self._headsets_ptr, self._count)
            self._headsets_ptr = None
            self._count = 0
            self._headsets = []

    @staticmethod
    def version() -> str:
        """Get library version."""
        return _lib.hsc_version().decode("utf-8")


# Example usage
if __name__ == "__main__":
    print(f"HeadsetControl v{HeadsetControl.version()}")

    with HeadsetControl() as hsc:
        headsets = hsc.discover()

        if not headsets:
            print("No headsets found")
            exit(1)

        for headset in headsets:
            print(f"\n=== {headset.name} ===")
            print(f"Vendor:  0x{headset.vendor_id:04x}")
            print(f"Product: 0x{headset.product_id:04x}")

            # Battery
            battery = headset.get_battery()
            if battery:
                status = BatteryStatus(battery.status).name
                print(f"Battery: {battery.level_percent}% ({status})")
                if battery.voltage_mv >= 0:
                    print(f"Voltage: {battery.voltage_mv} mV")

            # Chat-mix
            chatmix = headset.get_chatmix()
            if chatmix:
                print(f"Chat-mix: {chatmix.level} (Game: {chatmix.game_volume_percent}%, Chat: {chatmix.chat_volume_percent}%)")

            # Set sidetone example (commented out)
            # if headset.supports(Capability.SIDETONE):
            #     headset.set_sidetone(64)
            #     print("Sidetone set to 64")
```

Save this as `headsetcontrol.py` and run:

```bash
# Make sure library is in path
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH  # Linux
export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH  # macOS

python3 headsetcontrol.py
```

### Rust Example

For Rust, you can use the `bindgen` crate to auto-generate bindings from `headsetcontrol_c.h`, or manually define them:

```rust
// Cargo.toml:
// [dependencies]
// libc = "0.2"

use libc::{c_char, c_int, c_void};
use std::ffi::CStr;

#[repr(C)]
pub struct HscBattery {
    pub level_percent: c_int,
    pub status: c_int,
    pub voltage_mv: c_int,
    pub time_to_full_min: c_int,
    pub time_to_empty_min: c_int,
}

#[link(name = "headsetcontrol")]
extern "C" {
    fn hsc_discover(headsets: *mut *mut c_void) -> c_int;
    fn hsc_free_headsets(headsets: *mut c_void, count: c_int);
    fn hsc_get_name(headset: *mut c_void) -> *const c_char;
    fn hsc_supports(headset: *mut c_void, cap: c_int) -> bool;
    fn hsc_get_battery(headset: *mut c_void, battery: *mut HscBattery) -> c_int;
    fn hsc_set_sidetone(headset: *mut c_void, level: u8, result: *mut c_void) -> c_int;
}

fn main() {
    unsafe {
        let mut headsets: *mut c_void = std::ptr::null_mut();
        let count = hsc_discover(&mut headsets);

        if count <= 0 {
            println!("No headsets found");
            return;
        }

        let headset_array = std::slice::from_raw_parts(headsets as *const *mut c_void, count as usize);

        for &headset in headset_array {
            let name = CStr::from_ptr(hsc_get_name(headset));
            println!("Found: {}", name.to_string_lossy());

            // Battery (capability 1)
            if hsc_supports(headset, 1) {
                let mut battery = HscBattery {
                    level_percent: 0,
                    status: 0,
                    voltage_mv: -1,
                    time_to_full_min: -1,
                    time_to_empty_min: -1,
                };
                if hsc_get_battery(headset, &mut battery) == 0 {
                    println!("  Battery: {}%", battery.level_percent);
                }
            }
        }

        hsc_free_headsets(headsets, count);
    }
}
```

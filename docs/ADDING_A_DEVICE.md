# Adding a New Device to HeadsetControl

This guide explains how to add support for a new USB headset to HeadsetControl.

## Before You Start

### Check ALSA Mixer (Linux)

Some headsets expose sidetone as an audio channel. Check with `alsamixer` first:
```bash
alsamixer
# Press F6 to select your headset
# Look for a "Sidetone" control
```

If sidetone is available in ALSA, you can't implement it via HID (but you can still add battery and other features).

### Check for Similar Devices

Your headset might be a variant of an already-supported model:
```bash
./headsetcontrol --dev -- --list
```

If you find a similar device (same vendor, similar product ID), you may only need to add your product ID to an existing implementation.

## Two Paths

### Path 1: Adding a Product ID to an Existing Device

If your headset uses the same protocol as an existing one (common for wireless variants):

1. Find the existing device file in `lib/devices/`
2. Add your product ID to the `PRODUCT_IDS` array
3. Update the array size if needed
4. Rebuild and test

**Note:** You don't need to modify `device_registry.cpp` - the device is already registered there.

**Example:** Adding a new Corsair headset that uses the same protocol:
```cpp
// In lib/devices/corsair_void_rich.hpp
static constexpr std::array<uint16_t, 5> PRODUCT_IDS {
    0x1b27, 0x1b2a, 0x1b23,
    0x1b2f,  // <- Add your new product ID here
};
```

### Path 2: Implementing a New Device

For headsets with unknown protocols, continue with the full guide below.

## Prerequisites

- USB Vendor ID and Product ID of your headset
- USB capture software (Wireshark with USBPcap on Windows, or usbmon on Linux)
- The manufacturer's Windows software
- Basic C++20 knowledge

## Step 1: Capture USB Traffic

### Setup

1. Install Wireshark with USB capture support
2. Connect your headset
3. Install the manufacturer's software (Windows VM with USB passthrough works well)

### Capture Process

1. Start Wireshark, select your USB interface
2. Filter by your device: `usb.idVendor == 0x1b1c && usb.idProduct == 0x1b27`
3. In the manufacturer's software, change a setting (e.g., sidetone)
4. Stop capture and analyze the packets

### Analyze Packets

Look for:
- **SET_REPORT** or **Write** packets when changing settings
- Bytes that change when you adjust values
- The packet length (must send exact byte count)

**Example:** When changing sidetone from 0 to 100, you might see:
```
Before: c9 00 00 00 00 ...
After:  c9 64 00 00 00 ...
        ^^ This byte changed (0x64 = 100)
```

### Test Before Coding

Use developer mode to test your findings:
```bash
# Test sending a packet
./headsetcontrol --dev -- --device 0x1b1c:0x1b27 --send-feature "0xc9, 0x64"

# Or for write (non-feature) packets
./headsetcontrol --dev -- --device 0x1b1c:0x1b27 --send "0xc9, 0x64" --receive
```

## Step 2: Create the Device Header File

Create a new file in `lib/devices/` named after your headset (e.g., `vendor_model.hpp`).

### Basic Structure

```cpp
#pragma once

#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Your Headset Name
 *
 * Features:
 * - List supported features here
 */
class YourHeadset : public HIDDevice {
public:
    // USB Product IDs this device supports
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x1234 };

    uint16_t getVendorId() const override
    {
        return 0xABCD;  // Your vendor ID
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Your Headset Name"sv;
    }

    int getCapabilities() const override
    {
        // Return bitmask of supported capabilities
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    }

    // Override capability details if your device needs specific HID interface/usage
    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        // usagepage and usageid are Windows-specific (0 = use interface)
        // interface_id: which HID interface to use (0 = first enumerated)
        return { .usagepage = 0, .usageid = 0, .interface_id = 3 };
    }

    // Implement the features your device supports...
    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Your implementation here
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Your implementation here
    }
};

} // namespace headsetcontrol
```

## Step 2: Implement Device Features

### Using HID Communication

The `HIDDevice` base class provides modern C++20 abstractions for HID communication:

```cpp
// Write data to the device
std::array<uint8_t, 3> cmd { 0x00, 0x39, level };
auto result = writeHID(device_handle, cmd);
if (!result) {
    return result.error();  // Propagate error
}

// Read with timeout
std::array<uint8_t, 64> buffer {};
auto read_result = readHIDTimeout(device_handle, buffer, hsc_device_timeout);
if (!read_result) {
    return read_result.error();
}
size_t bytes_read = *read_result;

// Feature reports (for persistent settings)
auto feature_result = sendFeatureReport(device_handle, cmd);
auto get_result = getFeatureReport(device_handle, buffer);
```

### Using Protocol Templates (Recommended)

For common protocols, use the provided templates to reduce boilerplate:

#### Logitech HID++ Protocol
```cpp
#include "protocols/hidpp_protocol.hpp"
#include "protocols/logitech_calibrations.hpp"

class LogitechYourDevice : public protocols::HIDPPDevice<LogitechYourDevice> {
    // HIDPPDevice provides sendHIDPPCommand(), requestBatteryHIDPP(), etc.

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Use existing calibration or create your own in logitech_calibrations.hpp
        return requestBatteryHIDPP(device_handle, {0x08, 0x0a}, calibrations::DEFAULT_LOGITECH);
    }
};
```

#### SteelSeries Protocol
```cpp
#include "protocols/steelseries_protocol.hpp"

// For Nova series (Nova 3, Nova 5, Nova 7, Nova Pro)
class SteelSeriesYourDevice : public protocols::SteelSeriesNovaDevice<SteelSeriesYourDevice> {
    // Provides sendCommand(), getBatteryStatus(), getChatmix(), etc.
};

// For older devices (Arctis 1, 7, 9, Pro Wireless)
class SteelSeriesYourDevice : public protocols::SteelSeriesLegacyDevice {
    // Provides basic sendCommand(), getBattery(), etc.
};
```

### Result Types

All feature methods return `Result<T>` types for proper error handling:

```cpp
Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
{
    // Map user level (0-128) to device range
    uint8_t device_level = map(level, 0, 128, 0, 100);

    std::array<uint8_t, 3> cmd { 0x00, 0x1d, device_level };
    auto result = writeHID(device_handle, cmd);
    if (!result) {
        return result.error();
    }

    return SidetoneResult {
        .current_level = level,
        .min_level = 0,
        .max_level = 128,
        .device_min = 0,
        .device_max = 100
    };
}

Result<BatteryResult> getBattery(hid_device* device_handle) override
{
    std::array<uint8_t, 64> buffer {};
    buffer[0] = 0x05;  // Report ID

    auto result = getFeatureReport(device_handle, buffer);
    if (!result) {
        return result.error();
    }

    // Parse the response
    int level = buffer[2];
    bool charging = buffer[3] & 0x01;

    return BatteryResult {
        .level_percent = level,
        .status = charging ? BATTERY_CHARGING : BATTERY_AVAILABLE,
        .voltage_mv = std::nullopt  // Optional: set if device reports voltage
    };
}
```

### Error Handling

Use the `DeviceError` factory methods:

```cpp
// Different error types
return DeviceError::timeout("Battery request timed out");
return DeviceError::hidError("Failed to write command");
return DeviceError::protocolError("Unexpected response");
return DeviceError::invalidParameter("Level out of range");
return DeviceError::notSupported("Feature not available");
```

## Step 3: Register the Device

Add your device to `lib/device_registry.cpp`:

```cpp
// Add include at the top
#include "devices/your_headset.hpp"

// Add registration in DeviceRegistry::initialize()
void DeviceRegistry::initialize()
{
    // ... existing devices ...

    // Your device
    registerDevice(std::make_unique<YourHeadset>());
}
```

## Step 4: Update CMakeLists

If your device has a `.cpp` file (not header-only), add it to `lib/CMakeLists.txt`:

```cmake
set(LIBRARY_SOURCES
    # ... existing sources ...
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/your_headset.cpp
)
```

## Step 5: Test Your Device

Build and test:

```bash
cd build
cmake ..
make

# Test with your device connected
./headsetcontrol -b  # Battery
./headsetcontrol -s 50  # Sidetone

# Use the test device for development
./headsetcontrol --test-device -b
```

## Helper Utilities

HeadsetControl provides utilities in `lib/devices/device_utils.hpp`:

```cpp
#include "devices/device_utils.hpp"

// Map ranges
uint8_t device_level = map(level, 0, 128, 0, 100);

// Map to discrete levels
std::array<uint8_t, 4> levels { 0, 1, 2, 3 };
uint8_t discrete = mapDiscrete(level, levels);

// Byte manipulation
uint16_t voltage = bytes_to_uint16_be(buffer[0], buffer[1]);
auto [high, low] = uint16_to_bytes_be(0x1234);

// Battery calibration
auto percent = voltageToPercent(voltage_mv, calibration_points);

// Capability details helper
return makeCapabilityDetail(0xffc0, 0x1, 3);
```

## Available Capabilities

| Capability | Type | Description |
|------------|------|-------------|
| `CAP_SIDETONE` | Action | Microphone feedback level |
| `CAP_BATTERY_STATUS` | Info | Battery level and charging status |
| `CAP_NOTIFICATION_SOUND` | Action | Play notification sounds |
| `CAP_LIGHTS` | Action | LED/RGB control |
| `CAP_INACTIVE_TIME` | Action | Auto power-off timer |
| `CAP_CHATMIX_STATUS` | Info | Game/chat audio balance |
| `CAP_VOICE_PROMPTS` | Action | Voice feedback toggle |
| `CAP_ROTATE_TO_MUTE` | Action | Boom arm mute toggle |
| `CAP_EQUALIZER_PRESET` | Action | Built-in EQ presets |
| `CAP_EQUALIZER` | Action | Custom EQ curve |
| `CAP_PARAMETRIC_EQUALIZER` | Action | Parametric EQ bands |
| `CAP_MICROPHONE_MUTE_LED_BRIGHTNESS` | Action | Mute LED brightness |
| `CAP_MICROPHONE_VOLUME` | Action | Mic gain level |
| `CAP_VOLUME_LIMITER` | Action | Volume limiter toggle |
| `CAP_BT_WHEN_POWERED_ON` | Action | Bluetooth auto-connect |
| `CAP_BT_CALL_VOLUME` | Action | Bluetooth call volume |

## Example: Complete Device Implementation

See `lib/devices/logitech_g535.hpp` for a complete example using the HID++ protocol, or `lib/devices/headsetcontrol_test.hpp` for a reference implementation of all capabilities.

## Tips

1. **USB Sniffing**: Use Wireshark with USBPcap (Windows) or usbmon (Linux) to capture HID traffic
2. **Existing Implementations**: Study similar devices in `lib/devices/` for patterns
3. **Protocol Templates**: Reuse protocol implementations when possible
4. **Test Device**: Use `--test-device` during development to test output formatting
5. **Dev Mode**: Use `--dev -- --list` to explore HID interfaces on your device

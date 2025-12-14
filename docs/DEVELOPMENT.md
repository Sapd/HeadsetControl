# Development Guide

This guide covers contributing to HeadsetControl, adding new devices, and understanding the codebase architecture.

## Requirements

- **C++20 compiler** (GCC 10+, Clang 10+, MSVC 2019+)
- **CMake** 3.12+
- **HIDAPI** library
- **clang-format 18** (for code formatting)

## Project Structure

```
HeadsetControl/
├── lib/                    # Core library
│   ├── devices/            # Device implementations
│   │   ├── hid_device.hpp  # Base class for all devices
│   │   ├── protocols/      # Protocol templates (HID++, SteelSeries)
│   │   └── *.hpp           # Device-specific implementations
│   ├── device.hpp          # Capability enums and structs
│   ├── device_registry.hpp # Device lookup singleton
│   ├── result_types.hpp    # Result<T> error handling
│   ├── headsetcontrol.hpp  # Public C++ API
│   └── headsetcontrol_c.h  # Public C API (for FFI)
├── cli/                    # Command-line interface
│   ├── main.cpp            # Entry point
│   ├── argument_parser.hpp # CLI argument parsing
│   ├── dev.cpp             # Developer/debug mode
│   └── output/             # JSON/YAML/ENV serializers
├── tests/                  # Unit and integration tests
└── docs/                   # Documentation
```

## Building for Development

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_CLANG_FORMAT=ON ..
make
```

### Running Tests

```bash
make check              # Build and run all tests
ctest --verbose         # Run tests with output
./headsetcontrol_tests  # Run unit tests directly
```

### Code Formatting

The project uses **WebKit style** via clang-format. CI requires version 18:

```bash
# Install clang-format 18
brew install llvm@18        # macOS
apt install clang-format-18 # Debian/Ubuntu

# Format all code
make format
```

### Static Analysis

```bash
cmake -DENABLE_CLANG_TIDY=ON ..
make tidy
```

## Architecture

### Device Registration

All devices are registered in `lib/device_registry.cpp`:

```cpp
void DeviceRegistry::initialize() {
    // Each device is created with make_unique
    // Vendor/product IDs are defined inside each device class
    registerDevice(std::make_unique<CorsairVoidRich>());
    registerDevice(std::make_unique<LogitechG533>());
    registerDevice(std::make_unique<SteelSeriesArctisNova7>());
    // ...
}
```

### HIDDevice Base Class

All devices inherit from `HIDDevice` (`lib/devices/hid_device.hpp`):

```cpp
class MyDevice : public HIDDevice {
public:
    std::string_view getDeviceName() const override { return "My Headset"; }
    uint16_t getVendorId() const override { return 0x1234; }
    int getCapabilities() const override { return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS); }

    Result<BatteryResult> getBattery(hid_device* handle) override {
        // Implementation
    }

    Result<SidetoneResult> setSidetone(hid_device* handle, uint8_t level) override {
        // Implementation
    }
};
```

### Result<T> Error Handling

All device methods return `Result<T>` for proper error handling:

```cpp
Result<BatteryResult> getBattery(hid_device* handle) override {
    std::array<uint8_t, 64> buffer{};
    buffer[0] = 0xC9;

    auto result = writeHID(handle, buffer);
    if (!result) {
        return result.error();  // Propagate error
    }

    result = readHIDTimeout(handle, buffer, 5000);
    if (!result) {
        return DeviceError::timeout("Battery request timed out");
    }

    return BatteryResult{
        .level_percent = buffer[2],
        .status = BATTERY_AVAILABLE
    };
}
```

Error types:
- `DeviceError::timeout(msg)` - HID read timeout
- `DeviceError::hidError(msg)` - HID communication error
- `DeviceError::protocolError(msg)` - Unexpected device response
- `DeviceError::notSupported(msg)` - Feature not supported

### Protocol Templates

Common protocols have reusable templates in `lib/devices/protocols/`:

- **HID++ Protocol** (`hidpp_protocol.hpp`) - Logitech devices
- **SteelSeries Protocol** (`steelseries_protocol.hpp`) - SteelSeries devices

Example using a protocol template:

```cpp
class LogitechG535 : public HIDPPDevice {
public:
    // Protocol template provides common HID++ functionality
    // Just override device-specific details
};
```

### Capability System

Capabilities are defined in `lib/device.hpp`:

```cpp
enum capabilities {
    CAP_SIDETONE,
    CAP_BATTERY_STATUS,
    CAP_LIGHTS,
    CAP_INACTIVE_TIME,
    // ... more capabilities
    NUM_CAPABILITIES
};

// Use B() macro for bitmask
int caps = B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
```

## Adding a New Device

See **[ADDING_A_DEVICE.md](ADDING_A_DEVICE.md)** for a complete step-by-step guide with code examples.

Quick overview:
1. Find device IDs with `./headsetcontrol --dev -- --list`
2. Capture USB traffic with Wireshark
3. Create device class in `lib/devices/yourdevice.hpp`
4. Register in `lib/device_registry.cpp`
5. Test and generate docs with `./headsetcontrol --readme-helper`

## Adding a New Capability

See **[ADDING_A_CAPABILITY.md](ADDING_A_CAPABILITY.md)** for a complete step-by-step guide.

Quick overview:
1. Add capability enum in `lib/device.hpp`
2. Add descriptor in `lib/capability_descriptors.hpp`
3. Add result type in `lib/result_types.hpp`
4. Add virtual method in `lib/devices/hid_device.hpp`
5. Register handler in `lib/feature_handlers.hpp`
6. Add CLI argument in `cli/main.cpp`
7. Implement in device classes

## Testing Without Hardware

Use the test device for development:

```bash
./headsetcontrol --test-device -b
./headsetcontrol --test-device -o json
```

The test device (`0xF00B:0xA00C`) implements all capabilities with predictable values.

## Developer Mode

For low-level HID debugging:

```bash
# List all HID devices
./headsetcontrol --dev -- --list

# List specific device interfaces
./headsetcontrol --dev -- --list --device 0x1b1c:0x1b27

# Send raw data and receive response
./headsetcontrol --dev -- --device 0x1b1c:0x1b27 \
    --send "0xC9, 0x64" --receive --timeout 100

# Send feature report
./headsetcontrol --dev -- --device 0x1b1c:0x1b27 \
    --send-feature "0x05, 0x00, 0x01"

# Repeat command every 2 seconds
./headsetcontrol --dev -- --device 0x1b1c:0x1b27 \
    --send "0xC9" --receive --repeat 2
```

## Windows-Specific Notes

Windows HID implementation differs from Linux/macOS:

1. **Usage Page/ID Required**: Windows needs exact HID usage page and usage ID, not just interface number:
   ```cpp
   constexpr capability_detail getCapabilityDetail(capabilities cap) const override {
       return { .usagepage = 0xFF00, .usageid = 0x0001 };
   }
   ```

2. **Exact Byte Count**: Windows requires sending the exact expected packet size.

3. **Common Errors**:
   - "Incorrect function" → Wrong HID endpoint (usage page/ID)
   - "Incorrect parameter" → Wrong packet size

## Code Style

- **C++20** with modern features (`std::format`, `std::span`, `std::optional`)
- **RAII** for resource management
- **`[[nodiscard]]`** on error-returning functions
- **Designated initializers** for structs
- No raw `new`/`delete` - use smart pointers
- Header-only device implementations

## Submitting Changes

1. Fork the repository
2. Create a feature branch
3. Run `make format` before committing
4. Ensure `make check` passes
5. Update documentation if needed
6. Submit a pull request

For major changes, open an issue first to discuss the approach.

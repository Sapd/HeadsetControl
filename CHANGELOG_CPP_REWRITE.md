# Changelog: C++ Modernization

This document describes the changes in the C++ modernization branch.

## Overview

Complete rewrite of HeadsetControl from C to modern C++20, introducing:
- Type-safe error handling with `Result<T>`
- RAII resource management
- Protocol abstraction templates
- High-level library API
- Comprehensive test suite

## Breaking Changes

- **Minimum C++ standard**: Now requires C++20 (GCC 10+, Clang 10+, MSVC 2019+)
- **Project structure**: Code reorganized into `lib/`, `cli/`, `tests/`
- **Header extensions**: Changed from `.h` to `.hpp` for C++ headers

## New Features

### High-Level Library API

New `headsetcontrol.hpp` provides a simple, HID-abstracted interface:

```cpp
#include <headsetcontrol.hpp>

auto headsets = headsetcontrol::discover();
for (auto& headset : headsets) {
    if (headset.supports(CAP_BATTERY_STATUS)) {
        auto battery = headset.getBattery();
        if (battery) {
            std::cout << battery->level_percent << "%\n";
        }
    }
}
```

### C API for FFI

New `headsetcontrol_c.h` provides a pure C interface for bindings:

```c
hsc_headset_t* headsets;
int count = hsc_discover(&headsets);
// ... use headsets ...
hsc_free_headsets(headsets, count);
```

### Result<T> Error Handling

All device methods now return `Result<T>` with rich error information:

```cpp
auto result = device->getBattery(handle);
if (result) {
    // Success: access result->level_percent, result->status, etc.
} else {
    // Error: result.error().code, result.error().message
}
```

### Protocol Templates

Reusable protocol implementations reduce device code by 60-80%:

- `HIDPPDevice<T>` - Logitech HID++ protocol
- `SteelSeriesDevice<T>` - SteelSeries protocol family
- `CorsairDevice<T>` - Corsair iCUE protocol

### Rich Result Types

New structured result types with extended information:

- `BatteryResult` - level, status, voltage, time estimates
- `SidetoneResult` - level with device range info
- `ChatmixResult` - level with game/chat percentages
- `EqualizerInfo` - bands, range, step size

### Data-Driven Feature System

New capability descriptor and handler registry system:

- `CapabilityDescriptor` - Single source of truth for all capability metadata (CLI flags, descriptions, value ranges)
- `FeatureHandlerRegistry` - Dispatch table replacing giant switch statements
- Automatic parameter validation from descriptors
- Auto-generated help text value hints from descriptors

```cpp
// All capability metadata in one place
inline constexpr std::array<CapabilityDescriptor, NUM_CAPABILITIES> CAPABILITY_DESCRIPTORS = {{
    {CAP_SIDETONE, CAPABILITYTYPE_ACTION, "sidetone", "-s", "Set sidetone level", 0, 128, "<0-128>"},
    // ...
}};

// Feature execution via registry
auto result = FeatureHandlerRegistry::instance().execute(cap, device, handle, param);
```

### Shared Library Support

- `BUILD_SHARED_LIBRARY` CMake option for creating dynamic library
- Windows DLL export macros (`HSC_API`)
- Versioned shared library with SOVERSION

### Comprehensive Test Suite

- Unit tests for utility functions (67 tests)
- Mock device tests
- Integration tests
- Test device with all capabilities

## Architecture Changes

### Project Structure

```
HeadsetControl/
├── lib/                    # Core library
│   ├── devices/            # Device implementations
│   │   ├── protocols/      # Protocol templates
│   │   └── *.hpp           # Device classes
│   ├── output/             # Serialization
│   ├── headsetcontrol.hpp  # High-level C++ API
│   ├── headsetcontrol_c.h  # C API
│   ├── result_types.hpp    # Result<T> and error types
│   └── device.hpp          # Capability enums
├── cli/                    # Command-line interface
│   ├── main.cpp
│   └── argument_parser.hpp
├── tests/                  # Test suite
└── docs/                   # Documentation
```

### Device Implementation Pattern

Old (C):
```c
// 200+ lines per device
struct device corsair_void_init() {
    struct device dev;
    dev.idVendor = 0x1b1c;
    dev.idProductsSupported = product_ids;
    dev.send_sidetone = &corsair_void_send_sidetone;
    // ... many more function pointers ...
    return dev;
}
```

New (C++):
```cpp
// 50-100 lines per device
class CorsairVoid : public HIDDevice {
public:
    uint16_t getVendorId() const override { return 0x1b1c; }
    std::vector<uint16_t> getProductIds() const override { return {0x0a14, ...}; }

    Result<SidetoneResult> setSidetone(hid_device* h, uint8_t level) override {
        // Implementation with proper error handling
    }
};
```

### Modern C++ Features Used

- `std::format` for string formatting
- `std::span` for buffer views
- `std::optional` for nullable values
- `std::string_view` for zero-copy strings
- `std::chrono` for time handling
- `[[nodiscard]]` for error checking
- Designated initializers for structs
- Concepts for type constraints
- CTAD (Class Template Argument Deduction)

## File Changes Summary

### Renamed/Moved
- `src/*.c` → `lib/*.cpp`
- `src/*.h` → `lib/*.hpp`
- `src/devices/*.c` → `lib/devices/*.hpp`
- `src/main.c` → `cli/main.cpp`

### New Files
- `lib/headsetcontrol.hpp` - High-level C++ API
- `lib/headsetcontrol.cpp` - API implementation
- `lib/headsetcontrol_c.h` - C API header
- `lib/headsetcontrol_c.cpp` - C API implementation
- `lib/result_types.hpp` - Result<T> type
- `lib/capability_descriptors.hpp` - Capability metadata (single source of truth)
- `lib/feature_handlers.hpp` - Feature handler registry
- `lib/feature_utils.hpp` - Feature helper functions
- `lib/string_utils.hpp` - String utilities
- `lib/devices/device_utils.hpp` - Device utilities
- `lib/devices/hid_device.hpp` - Base device class
- `lib/devices/hid_interface.hpp` - HID abstraction
- `lib/devices/protocols/*.hpp` - Protocol templates
- `lib/output/serializers.hpp` - Output serialization
- `lib/output/output_data.hpp` - Output data models
- `cli/argument_parser.hpp` - CLI argument parsing
- `tests/*.cpp` - Test files
- `docs/ADDING_A_DEVICE.md` - Device development guide
- `docs/LIBRARY_USAGE.md` - Library integration guide

### Deleted
- All `src/devices/*.c` files (replaced by `.hpp`)
- `src/device_registry.c` (replaced by `lib/device_registry.cpp`)
- `src/output.c` (replaced by `lib/output/`)

## Documentation

New documentation added:
- `docs/ADDING_A_DEVICE.md` - How to add new device support
- `docs/LIBRARY_USAGE.md` - Using HeadsetControl as a library (C++, C, Python, Rust examples)
- Updated `CLAUDE.md` - Developer guidance

## Build System

- CMake structure updated for `lib/`, `cli/`, `tests/` layout
- Library target: `headsetcontrol_lib`
- CLI target: `headsetcontrol`
- Test target: `headsetcontrol_tests`
- Install targets for library and headers

## Migration Notes

### For Users
No changes - CLI interface remains the same.

### For Developers Adding Devices
1. Create `lib/devices/vendor_model.hpp`
2. Inherit from `HIDDevice` or protocol template
3. Implement virtual methods
4. Register in `lib/device_registry.cpp`

See `docs/ADDING_A_DEVICE.md` for details.

### For Library Users
New high-level API available:
- C++: `#include <headsetcontrol.hpp>`
- C: `#include <headsetcontrol_c.h>`

See `docs/LIBRARY_USAGE.md` for integration guide.

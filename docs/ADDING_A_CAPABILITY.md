# Adding a New Capability

This guide explains how to add a new capability (feature) to HeadsetControl.

## Overview

A capability is a feature like sidetone, battery status, or LED control. Adding one involves:

1. Add to CAPABILITIES_XLIST (enum + strings auto-generated)
2. Add metadata (CLI flags, description, value range)
3. Create a result type
4. Add the virtual method to HIDDevice base class
5. Register a handler
6. Add CLI argument parsing
7. Implement in device classes
8. Bump `API_VERSION`

## Files You'll Modify

| File | Purpose |
|------|---------|
| `lib/device.hpp` | Add to CAPABILITIES_XLIST (generates enum + strings) |
| `lib/capability_descriptors.hpp` | CLI metadata (flags, description, validation) |
| `lib/result_types.hpp` | Result struct for the feature |
| `lib/devices/hid_device.hpp` | Virtual method in base class |
| `lib/feature_handlers.hpp` | Handler registration |
| `cli/main.cpp` | CLI argument parsing |
| `lib/devices/*.hpp` | Device implementations |
| `cli/output/output.cpp` | `API_VERSION` |
| `tests/test_output_formats.cpp` | `API_VERSION` in the fixture |
| `docs/LIBRARY_USAGE.md` | `api_version` in the sample output |

## Step-by-Step Guide

### 1. Add Capability to X-Macro (`lib/device.hpp`)

Add a single line to `CAPABILITIES_XLIST`. The enum, string name, and short char are all generated automatically:

```cpp
#define CAPABILITIES_XLIST \
    X(CAP_SIDETONE,        "sidetone",        's')  \
    X(CAP_BATTERY_STATUS,  "battery",         'b')  \
    // ... existing capabilities ...
    X(CAP_YOUR_FEATURE,    "your feature",    '\0') // <- Add here (use '\0' for no short char)
```

That's it! The enum value `CAP_YOUR_FEATURE` and all string conversion functions are generated from this single line.

### 2. Add Descriptor (`lib/capability_descriptors.hpp`)

Find the `CAPABILITY_DESCRIPTORS` array and add your capability:

```cpp
inline constexpr std::array<CapabilityDescriptor, NUM_CAPABILITIES> CAPABILITY_DESCRIPTORS = {{
    // ... existing descriptors ...

    // CAP_YOUR_FEATURE
    {
        .cap = CAP_YOUR_FEATURE,
        .type = CAPABILITYTYPE_ACTION,     // ACTION = takes a value, INFO = query only
        .name = "your-feature",            // CLI long flag: --your-feature
        .short_flag = "-y",                // CLI short flag
        .description = "Set your feature", // Help text
        .min_value = 0,                    // Min value (nullopt if no limit)
        .max_value = 100,                  // Max value (nullopt if no limit)
        .value_hint = "<0-100>"            // Shown in help text
    },
}};
```

**Capability types:**
- `CAPABILITYTYPE_ACTION` - Takes a parameter (sidetone, lights, inactive time)
- `CAPABILITYTYPE_INFO` - Query only, no parameter (battery, chatmix)

### 3. Add Result Type (`lib/result_types.hpp`)

```cpp
struct YourFeatureResult {
    int value;
    // Add fields as needed
};
```

### 4. Add Virtual Method (`lib/devices/hid_device.hpp`)

```cpp
class HIDDevice {
public:
    // ... existing methods ...

    // Default returns "not supported" - devices override if they support it
    virtual Result<YourFeatureResult> setYourFeature(hid_device* handle, uint8_t value)
    {
        return DeviceError::notSupported("your-feature not supported");
    }
};
```

### 5. Register Handler (`lib/feature_handlers.hpp`)

Find `registerAllHandlers()` and add. Each capability has two slots: `CAPABILITYTYPE_ACTION`
for setting it and `CAPABILITYTYPE_INFO` for reading it. Use the one matching the
descriptor's `.type` (info-only capabilities such as battery use `CAPABILITYTYPE_INFO`):

```cpp
void FeatureHandlerRegistry::registerAllHandlers()
{
    // ... existing handlers ...

    // CAP_YOUR_FEATURE
    registerHandler(CAP_YOUR_FEATURE, CAPABILITYTYPE_ACTION,
        [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
            auto r = dev->setYourFeature(h, detail::getUint8(p));
            if (r.hasError())
                return r.error();
            return FeatureOutput::success(r->value);
        });
}
```

**Helper functions in `detail` namespace:**
- `getInt(p)` - Get int from param
- `getUint8(p)` - Get uint8_t from param
- `getBool(p)` - Get bool from param (int != 0)
- `getEqualizer(p)` - Get EqualizerSettings
- `getParametricEq(p)` - Get ParametricEqualizerSettings

### Reading a setting back

Don't add a `*_STATUS` capability for reading back something that already has a setter.
Readability is a property of the capability:

1. Add a `getYourFeature(hid_device*)` virtual next to the setter in `hid_device.hpp`,
   returning `notSupported` by default.
2. Register a second handler for the same capability under `CAPABILITYTYPE_INFO`:

```cpp
registerHandler(CAP_YOUR_FEATURE, CAPABILITYTYPE_INFO,
    [](HIDDevice* dev, hid_device* h, const FeatureParam&) -> Result<FeatureOutput> {
        auto r = dev->getYourFeature(h);
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->value);
    });
```

3. Add a `CAPABILITYTYPE_INFO` row for it to the CLI request table in `cli/main.cpp`, next
   to the `CAPABILITYTYPE_ACTION` one. `--volume` (`CAP_HEADSET_VOLUME`) sets with a value
   and reads without one.
4. Devices that implement the getter list the capability in `getReadableCapabilities()`
   as well as `getCapabilities()`.

`CAP_SIDETONE_STATUS` predates this and is kept for compatibility: a device with
`CAP_SIDETONE` in `getReadableCapabilities()` reports it automatically.

### 6. Add CLI Argument (`cli/main.cpp`)

Find `buildArgumentParser()` and add your option:

```cpp
ArgumentParser buildArgumentParser(Options& opts)
{
    return ArgumentParser("headsetcontrol")
        // ... existing options ...
        .addOption<int>('y', "your-feature", opts.your_feature, "Set your feature (0-100)")
        ;
}
```

Also add the field to the `Options` struct:

```cpp
struct Options {
    // ... existing fields ...
    std::optional<int> your_feature;
};
```

And handle it in the feature processing loop:

```cpp
if (opts.your_feature) {
    requests.push_back({CAP_YOUR_FEATURE, *opts.your_feature});
}
```

### 7. Implement in Devices (`lib/devices/*.hpp`)

Override the method in devices that support your feature:

```cpp
class MyDevice : public HIDDevice {
public:
    int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_YOUR_FEATURE);  // Add to bitmask
    }

    Result<YourFeatureResult> setYourFeature(hid_device* handle, uint8_t value) override
    {
        // Send command to device
        std::array<uint8_t, 3> cmd { 0x06, 0x42, value };
        auto result = writeHID(handle, cmd);
        if (!result) {
            return result.error();
        }

        return YourFeatureResult { .value = value };
    }
};
```

### 8. Bump `API_VERSION` (`cli/output/output.cpp`)

The capability list is part of the structured output, so adding a capability is an
addition to the output format. Raise the second number and say what changed:

```cpp
// 1.8: structured output gained the additive "readable_capabilities" list and a
// "headset_volume" field for devices that support reading it back.
constexpr std::string_view API_VERSION = "1.8";
```

The same version appears in `tests/test_output_formats.cpp` and in the sample output in
`docs/LIBRARY_USAGE.md`; all three have to move together. Raise the first number instead
if you change or remove an existing field.

## Example: Adding Volume Limiter

Here's a real example from the codebase:

**device.hpp** (add to CAPABILITIES_XLIST):
```cpp
X(CAP_VOLUME_LIMITER, "volume limiter", '\0')
```

**capability_descriptors.hpp:**
```cpp
{
    .cap = CAP_VOLUME_LIMITER,
    .type = CAPABILITYTYPE_ACTION,
    .name = "volume-limiter",
    .short_flag = "",  // No short flag
    .description = "Enable/disable volume limiter",
    .min_value = 0,
    .max_value = 1,
    .value_hint = "<0|1>"
},
```

**result_types.hpp:**
```cpp
struct VolumeLimiterResult {
    bool enabled;
};
```

**hid_device.hpp:**
```cpp
virtual Result<VolumeLimiterResult> setVolumeLimiter(hid_device*, bool enabled)
{
    return DeviceError::notSupported("volume-limiter");
}
```

**feature_handlers.hpp:**
```cpp
registerHandler(CAP_VOLUME_LIMITER, CAPABILITYTYPE_ACTION,
    [](HIDDevice* dev, hid_device* h, const FeatureParam& p) -> Result<FeatureOutput> {
        auto r = dev->setVolumeLimiter(h, detail::getBool(p));
        if (r.hasError())
            return r.error();
        return FeatureOutput::success(r->enabled ? 1 : 0);
    });
```

## Testing

```bash
# Build
cd build && make

# Test with test device (implements all capabilities)
./headsetcontrol --test-device --your-feature 50

# Test help text
./headsetcontrol --help-all

# Test with real device
./headsetcontrol --your-feature 50
```

## Validation

There are two bounds to set, and they have to agree.

The CLI enforces the range passed to the parser in `cli/main.cpp`:

```bash
$ ./headsetcontrol --sidetone 200
Error: sidetone: value 200 out of range [0, 128]
```

The descriptor's `min_value` and `max_value` feed the help text, and for the normalised
0-128 levels they are also the bound `Headset::setSidetone()`, `setMicVolume()` and
`setHeadsetVolume()` apply, since library callers never go through the parser:

```cpp
headset.setHeadsetVolume(200);  // invalid parameter: volume must be 0-128 (got 200)
```

Anything narrower than that, or specific to one device, belongs in the device
implementation, which returns `DeviceError::invalidParameter()` itself - a device whose
hardware only has four steps validates that, not the descriptor.

## Tips

1. **Look at similar capabilities** - Find an existing capability similar to yours and use it as a template
2. **Test device first** - The test device (`lib/devices/headsetcontrol_test.hpp`) implements all capabilities - add yours there first
3. **Check output formats** - Make sure JSON/YAML output includes your new data if needed
4. **Update README** - Run `./headsetcontrol --readme-helper` to generate the device table and replace the old one wholesale (`x*` marks capabilities only some product IDs of a device report)

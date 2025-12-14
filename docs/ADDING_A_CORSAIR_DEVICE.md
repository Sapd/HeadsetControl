# Adding a Corsair Device

This guide explains how to add support for a Corsair headset to HeadsetControl.

## Quick Check

Most Corsair headsets use the **same protocol**, so your device might work by simply adding its Product ID to an existing implementation.

**Exception:** The Virtuoso and HS series use different protocols and may need separate implementations.

## Step 1: Find Your Device ID

```bash
./headsetcontrol --dev -- --list
```

Look for your Corsair headset (Vendor ID `0x1b1c`) and note the **Product ID**.

## Step 2: Check ALSA (Linux)

```bash
alsamixer
# Press F6, select your headset
# Look for "Sidetone" control
```

If sidetone appears in ALSA, it can't be controlled via HID (but battery and other features may still work).

## Step 3: Add Your Product ID

Most Corsair headsets can be added to `lib/devices/corsair_void_rich.hpp`:

```cpp
// Find the PRODUCT_IDS array and add your ID
static constexpr std::array<uint16_t, N> PRODUCT_IDS {
    0x1b27,  // VOID PRO
    0x1b2a,  // VOID PRO Wireless
    0x1b23,  // VOID USB
    0x1b2f,  // <- Add your product ID here
};
```

Also update the array size `N` to match the new count.

**Note:** You don't need to modify `device_registry.cpp` - the device is already registered there. Just adding the product ID to the array is enough.

## Step 4: Build and Test

```bash
cd build
cmake ..
make

# Test your headset
./headsetcontrol -b      # Battery
./headsetcontrol -s 64   # Sidetone
./headsetcontrol -l 1    # Lights on
```

## If It Doesn't Work

If the standard Corsair protocol doesn't work for your headset:

1. Use developer mode to explore:
   ```bash
   ./headsetcontrol --dev -- --list --device 0x1b1c:YOUR_ID
   ```

2. Capture USB traffic with Wireshark (see [ADDING_A_DEVICE.md](ADDING_A_DEVICE.md))

3. You may need to create a new device class - use `corsair_void_rich.hpp` as a template

## Contributing

1. Fork the repository
2. Add only your device changes
3. Test all features that should work
4. Submit a pull request

If you have questions, open an issue on GitHub.

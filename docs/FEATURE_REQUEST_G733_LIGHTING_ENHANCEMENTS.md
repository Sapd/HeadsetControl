## Feature Request: Logitech G733 Advanced Lighting Control

### Summary
Add full lighting controls for Logitech G733 (and related Logitech G633 family devices), including:

- Light color control (named colors, hex, RGB tuple)
- Light brightness control (1-100)
- Light mode selection (`static`, `breathing`, `wave`)
- Light speed control for animated modes (`breathing`, `wave`)

This extends current light support beyond simple on/off behavior.

---

### Problem
Current lighting control is limited to toggling LEDs on or off.
For G733 users, this is not enough because the headset supports richer effects and parameterized lighting.

---

### Proposed Solution
Introduce dedicated light capabilities and CLI options:

1. **Color**
   - New capability: `CAP_LIGHT_COLOR`
   - New option: `--light-color`
   - Accepts:
     - named colors: `red`, `green`, `blue`, `light-blue`, `yellow`, `purple`, `teal`, `orange`, etc.
     - hex: `#RRGGBB`
     - tuple: `R,G,B`

2. **Brightness**
   - New capability: `CAP_LIGHT_BRIGHTNESS`
   - New option: `--light-brightness <1-100>`

3. **Mode**
   - New capability: `CAP_LIGHT_MODE`
   - New option: `--light-mode <static|breathing|wave>`

4. **Speed**
   - New capability: `CAP_LIGHT_SPEED`
   - New option: `--light-speed <1-100>`
   - Applied only to animated modes (`breathing`, `wave`)

---

### Implementation Notes
- Added new light-related capabilities in core capability enum and descriptors.
- Added handler registry execution paths for each new capability.
- Added base virtual APIs in `HIDDevice` for:
  - `setLightColor()`
  - `setLightBrightness()`
  - `setLightMode()`
  - `setLightSpeed()`
- Implemented Logitech G633 family behavior in `logitech_g633_g933_935.hpp`.
- Corrected G733 color channel mapping so color output matches requested values.
- Added help output entries so new options show up in CLI help.
- Updated C API capability enum indexes in `lib/headsetcontrol_c.h` to stay aligned with C++ capabilities.

---

### CLI Usage Examples
```bash
# Basic color
headsetcontrol --light-color red
headsetcontrol --light-color "#00aaff"
headsetcontrol --light-color 255,128,0

# Brightness
headsetcontrol --light-brightness 60

# Mode
headsetcontrol --light-mode static
headsetcontrol --light-mode breathing
headsetcontrol --light-mode wave

# Mode speed (animated modes)
headsetcontrol --light-mode breathing --light-speed 25
headsetcontrol --light-mode wave --light-speed 80

```

---

### Backward Compatibility
- Existing `-l/--light` on/off behavior remains available.
- New options are additive.
- `--light-speed` is meaningful for animated modes and ignored for `static`.

---

### Test Plan
1. Verify color accuracy on G733:
   - red, green, blue, yellow, purple, cyan/light-blue
2. Verify brightness range:
   - test min/mid/max: 1, 50, 100
3. Verify mode switching:
   - static <-> breathing <-> wave
4. Verify speed responsiveness:
   - animated modes with 10, 50, 90
5. Verify old behavior:
   - `--light 0` and `--light 1` still function
6. Verify validation:
   - reject invalid RGB, invalid mode strings, out-of-range speed/brightness

---

### Motivation / User Value
This makes headset lighting actually configurable in a practical way and closes a common gap for G733 users migrating from vendor software.
It improves parity with device capabilities and makes HeadsetControl significantly more useful for daily customization.

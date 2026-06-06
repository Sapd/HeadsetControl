# ASTRO A50 Gen 5 — additional device features (not in this PR)

The device file `lib/devices/logitech_astro_a50.hpp` implements the A50 features that map to
**existing** HeadsetControl capabilities: battery, chatmix, sidetone, lights, parametric
equalizer and microphone noise filter.

The A50 base station exposes several other fully reverse-engineered, working controls over the
same vendor HID protocol that **do not map to any existing HeadsetControl capability**. They are
intentionally **left out of this PR**: adding them would require new *core* capabilities, and per
the contribution guidelines that should be discussed/approved with the maintainer first rather
than introduced unilaterally in a device PR.

| Feature | A50 command | Why it isn't here |
|---------|-------------|-------------------|
| Master (headset) volume | `0x08` (`02 0c 05 00 08 1b ff <0..21>`) | HeadsetControl has no output-volume capability — volume is handled OS-side |
| Microphone EQ | `0x0d` with `byte6=00` (mic target) | the parametric EQ capability targets the **headphone** output only |
| Stream / broadcast mix | `0x0c` (handle hi-nibble 6) | no "per-source broadcast mix" capability (porta/mic/game/BT/voice levels) |
| Device state (mic/BT/online) | `0x0c` prop 2, `0x0e` | read-only state used for UI gating; not a controllable capability |

## Companion GUI

A standalone GTK tray application — **HeadsetControl-A50-GUI**
(https://github.com/lluiseduardo-silva/HeadsetControlA50Gui) — implements **all** of the above
plus the capabilities in this PR, by talking to the device directly over `/dev/hidrawN` and to
PipeWire (for the Game/Voice split, chatmix balance and mic volume). It serves as a reference for
the full protocol while these features have no upstream home.

If the maintainer is open to any of these becoming first-class HeadsetControl capabilities, I'm
glad to contribute them upstream.

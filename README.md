# HeadsetControl

A cross-platform tool to control USB gaming headsets on **Linux**, **macOS**, and **Windows**. Manage sidetone, battery status, LED lights, equalizers, and more.

## Features

- **Sidetone** - Hear your own voice without latency (unlike software loopback)
- **Battery Status** - Monitor charge level, voltage, and time remaining
- **LED Control** - Toggle lights on/off
- **Equalizer** - Presets and custom EQ curves (including parametric EQ)
- **Inactive Time** - Auto power-off timer
- **Chat-Mix** - Game/chat audio balance
- **Microphone** - Volume, mute LED brightness, rotate-to-mute
- **Voice Prompts** - Enable/disable audio cues
- **Bluetooth** - Power-on behavior, call volume

## Supported Devices

| Device | Platform | sidetone | battery | notification sound | lights | inactive time | chatmix | voice prompts | rotate to mute | equalizer preset | equalizer | parametric equalizer | microphone mute led brightness | microphone volume | volume limiter | bluetooth when powered on | bluetooth call volume |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Logitech G533 | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G535 | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G633/G635/G733/G933/G935 | All | x | x |   | x |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G432/G433 | All | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G930 | All | x | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G PRO Series | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech Zone Wired/Zone 750 | All | x |   |   |   |   |   | x | x |   |   |   |   |   |   |   |   |
| Corsair Headset Device | All | x | x | x | x |   |   |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis (1/7X/7P) Wireless | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis (7/Pro) | All | x | x |   | x | x | x |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis 9 | All | x | x |   |   | x | x |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis Pro Wireless | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis Nova 3 | All | x |   |   |   |   |   |   |   | x | x |   | x | x |   |   |   |
| SteelSeries Arctis Nova (5/5X) | All | x | x |   |   | x | x |   |   | x | x | x | x | x | x |   |   |
| SteelSeries Arctis Nova 7 | All | x | x |   |   | x | x |   |   | x | x |   | x | x | x | x | x |
| SteelSeries Arctis Nova 7P | All |   | x |   |   | x |   |   |   | x | x |   | x | x | x | x | x |
| SteelSeries Arctis 7+ | All | x | x |   |   | x | x |   |   | x | x |   |   |   |   |   |   |
| SteelSeries Arctis Nova Pro Wireless | All | x | x |   | x | x |   |   |   | x | x |   |   |   |   |   |   |
| SteelSeries Arctis Nova 3P Wireless | L/M | x | x |   |   | x |   |   |   | x | x | x |   | x |   |   |   |
| HyperX Cloud Alpha Wireless | All | x | x |   |   | x |   | x |   |   |   |   |   |   |   |   |   |
| HyperX Cloud Flight Wireless | All |   | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| HyperX Cloud II Wireless | All |   | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| HyperX Cloud 3 | All | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| ROCCAT Elo 7.1 Air | All |   |   |   | x | x |   |   |   |   |   |   |   |   |   |   |   |
| ROCCAT Elo 7.1 USB | All |   |   |   | x |   |   |   |   |   |   |   |   |   |   |   |   |
| Audeze Maxwell | All | x | x |   |   | x | x | x |   | x |   |   |   |   | x |   |   |
| HeadsetControl Test device | All | x | x | x | x | x | x | x | x | x | x | x | x | x | x | x | x |

**Platform:** All = Linux, macOS, Windows | L/M = Linux and macOS only

> **Note:** Some Corsair headsets may need additional configuration - see [Adding a Corsair device](docs/ADDING_A_CORSAIR_DEVICE.md). Some headsets (HS80, HS70 wired, RGB Elite, Virtuoso) expose sidetone via ALSA mixer instead.

## Installation

### Package Managers

#### macOS (Homebrew)
```bash
brew install sapd/headsetcontrol/headsetcontrol --HEAD
```

#### NixOS
```nix
# configuration.nix
environment.systemPackages = [ pkgs.headsetcontrol ];
services.udev.packages = [ pkgs.headsetcontrol ];  # For udev rules
```

Or run without installing: `nix run nixpkgs#headsetcontrol`

#### Gentoo ([nitratesky](https://github.com/VTimofeenko/nitratesky) overlay)
```bash
eselect repository enable nitratesky
emerge -a app-misc/headsetcontrol
```

### Building from Source

#### Requirements

- **C++20 compiler** (GCC 10+, Clang 10+, MSVC 2019+)
- **CMake** 3.12+
- **HIDAPI** library

#### Install Dependencies

<details>
<summary><b>Linux</b></summary>

**Debian / Ubuntu**
```bash
apt-get install build-essential git cmake libhidapi-dev
```

**Fedora**
```bash
dnf install cmake hidapi-devel g++
```

**Arch Linux**
```bash
pacman -S git cmake hidapi
```

**CentOS / RHEL**
```bash
yum install epel-release
yum groupinstall "Development tools"
yum install git cmake hidapi-devel
```

**openSUSE**
```bash
zypper in -t pattern devel_basis
zypper in cmake libhidapi-devel
```

**FreeBSD**
```bash
pkg install hidapi cmake
```

</details>

<details>
<summary><b>macOS</b></summary>

```bash
brew install hidapi cmake
```
Note: Xcode (from App Store) is required for compilers.

</details>

<details>
<summary><b>Windows</b></summary>

Pre-built binaries are available on the [releases](https://github.com/Sapd/HeadsetControl/releases) page.

For compilation using MSYS2/MinGW, see the [Development Guide](docs/DEVELOPMENT.md#windows-specific-notes).

</details>

#### Build

```bash
git clone https://github.com/Sapd/HeadsetControl && cd HeadsetControl
mkdir build && cd build
cmake ..
make
```

#### Install

```bash
sudo make install
```

On **Linux**, this also installs udev rules for non-root access. Reload them with:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Usage

```bash
# Show available options for your headset
headsetcontrol -h

# Show all options
headsetcontrol --help-all

# Get battery status
headsetcontrol -b

# Set sidetone level (0-128)
headsetcontrol -s 64

# Turn off LEDs
headsetcontrol -l 0

# Set auto-off timer (minutes, 0 = disabled)
headsetcontrol -i 30

# List device capabilities
headsetcontrol --capabilities
# or shorthand
headsetcontrol --caps
```

### Output Formats

For scripting and integration with other tools:

```bash
# JSON output
headsetcontrol -o json

# YAML output
headsetcontrol -o yaml

# Environment variables
headsetcontrol -o env
```

See [docs/LIBRARY_USAGE.md](docs/LIBRARY_USAGE.md) for building applications on top of HeadsetControl.

### Developer Mode

For debugging and reverse-engineering headset protocols:

```bash
# List all HID devices
headsetcontrol --dev -- --list

# Send raw HID data
headsetcontrol --dev -- --device 0x1b1c:0x1b27 --send "0xC9, 0x64" --receive
```

## Library API

HeadsetControl can be used as a library with C++ and C APIs. Build with shared library support:

```bash
cmake -DBUILD_SHARED_LIBRARY=ON ..
make
```

See [docs/LIBRARY_USAGE.md](docs/LIBRARY_USAGE.md) for complete documentation.

### Test Device

For development without hardware:
```bash
headsetcontrol --test-device -b
```

## GUI Applications

### Linux
- [gnome-shell-extension-HeadsetControl](https://github.com/ChrisLauinger77/gnome-shell-extension-HeadsetControl/) - GNOME shell extension (GNOME 42+)
- [headset-charge-indicator](https://github.com/centic9/headset-charge-indicator/) - System tray with controls (Python)
- [headset-battery-indicator](https://github.com/ruflas/headset-battery-indicator) - Tray icon with ChatMix, Sidetone controls (Python/Qt)
- [headsetcontrol-notificationd](https://github.com/Manawyrm/headsetcontrol-notificationd) - Battery notifications (PHP)

### macOS
- [HeadsetControl-MacOSTray](https://github.com/ChrisLauinger77/HeadsetControl-MacOSTray) - Menu bar app (macOS 14+)

### Windows
- [QontrolPanel](https://github.com/Odizinne/QontrolPanel) - Quick access / settings panel (Qt C++/QML, also works on Linux)
- [HeadsetControl-GUI](https://github.com/LeoKlaus/HeadsetControl-GUI) - Simple GUI (Qt C++)
- [HeadsetControl-SystemTray](https://github.com/zampierilucas/HeadsetControl-SystemTray) - System tray (Python)
- [headset-battery-indicator](https://github.com/aarol/headset-battery-indicator) - Native tray icon (Rust)

## Contributing

Want to add support for a new headset or improve the software? See the [Development Guide](docs/DEVELOPMENT.md).

Adding a new device requires capturing USB traffic between the headset and its Windows software using Wireshark or USBlyzer (a VM with USB passthrough works well).

## License

Released under [GPL v3](LICENSE).

HeadsetControl is distributed WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

---

If you find this useful, please star the repository!
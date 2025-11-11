## Summary

A tool to control certain aspects of USB-connected headsets on Linux. Currently, support is provided for adjusting sidetone, getting battery state, controlling LEDs, and setting the inactive time. See below for which headset supports which feature.

### Sidetone

Want to use your Headset under Linux or Mac OS X, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Supported Headsets

| Device | Platform | sidetone | battery | notification sound | lights | inactive time | chatmix | voice prompts | rotate to mute | equalizer preset | equalizer | parametric equalizer | microphone mute led brightness | microphone volume | volume limiter | bluetooth when powered on | bluetooth call volume |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Audeze Maxwell | All | x | x |   |   | x | x | x |   | x |   |   |   |   | x |   |   |
| Corsair Headset Device | All | x | x | x | x |   |   |   |   |   |   |   |   |   |   |   |   |
| HyperX Cloud Alpha Wireless | All | x | x |   |   | x |   | x |   |   |   |   |   |   |   |   |   |
| HyperX Cloud Flight Wireless | All |   | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| HyperX Cloud II Wireless | All |   | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| HyperX Cloud 3 | All | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G430 | All | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G432/G433 | All | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G533 | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G535 | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G930 | All | x | x |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G633/G635/G733/G933/G935 | All | x | x |   | x |   |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G PRO Series | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech G PRO X 2 | All | x |   |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| Logitech Zone Wired/Zone 750 | All | x |   |   |   |   |   | x | x |   |   |   |   |   |   |   |   |
| SteelSeries Arctis (1/7X/7P) Wireless | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis (7/Pro) | All | x | x |   | x | x | x |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis 9 | All | x | x |   |   | x | x |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis Pro Wireless | All | x | x |   |   | x |   |   |   |   |   |   |   |   |   |   |   |
| ROCCAT Elo 7.1 Air | All |   |   |   | x | x |   |   |   |   |   |   |   |   |   |   |   |
| ROCCAT Elo 7.1 USB | All |   |   |   | x |   |   |   |   |   |   |   |   |   |   |   |   |
| SteelSeries Arctis Nova 3 | All | x |   |   |   |   |   |   |   | x | x |   | x | x |   |   |   |
| SteelSeries Arctis Nova 3P Wireless | L/M | x | x |   |   | x |   |   |   | x | x | x |   | x |   |   |   |
| SteelSeries Arctis Nova (5/5X) | All | x | x |   |   | x | x |   |   | x | x | x | x | x | x |   |   |
| SteelSeries Arctis Nova 7 | All | x | x |   |   | x | x |   |   | x | x |   | x | x | x | x | x |
| SteelSeries Arctis Nova 7 Wireless Gen 2 | All | x | x |   |   | x | x |   |   | x | x |   | x | x | x | x | x |
| SteelSeries Arctis 7+ | All | x | x |   |   | x | x |   |   | x | x |   |   |   |   |   |   |
| SteelSeries Arctis Nova Pro Wireless | All | x | x |   | x | x |   |   |   | x | x |   |   |   |   |   |   |
| HeadsetControl Test device | All | x | x | x | x | x | x | x | x | x | x | x | x | x | x | x | x |

**Platform Legend:**
- **All**: Linux, macOS, and Windows
- **L/M**: Linux and macOS only (Windows not supported due to HID implementation differences)

For non-supported headsets on Linux: There is a chance that you can set the sidetone via AlsaMixer

&ast; *If your Corsair headset is not recognized - or you have a very similar headset to an existing one, see [Adding a corsair device](https://github.com/Sapd/HeadsetControl/wiki/Adding-a-Corsair-device).* HS80 and HS70 wired, RGB Elite, and Virtuso is not supported, but you can change its sidetone in Alsamixer.

For more features or other headsets, the protocol of the respective headset must be analyzed further. This can be done by capturing the USB traffic between the device and the original Windows software and analyzing it with WireShark or USBlyzer. For that, you can also use a virtual machine with USB passthrough. The [wiki](https://github.com/Sapd/HeadsetControl/wiki/Development) provides a tutorial.

Some headsets expose sidetone as audio-channel volume and as such can be changed in Alsamixer.

## Building

### Prerequisites

Before building, ensure you have the necessary dependencies installed, including HIDAPI, C compilers, and CMake. These dependencies can usually be installed via your system's package manager.

#### Debian / Ubuntu

`apt-get install build-essential git cmake libhidapi-dev`

#### CentOS / RHEL (RedHat based)

RHEL and CentOS also require the epel-repository.

`yum install epel-release`  
`yum groupinstall "Development tools"`  
`yum install git cmake hidapi-devel`

#### Fedora

`dnf install cmake hidapi-devel g++`

#### openSUSE

`zypper in -t pattern devel_basis`  
`zypper in cmake libhidapi-devel`

#### Sabayon

`equo i hidapi cmake`

#### Arch Linux

`pacman -S git cmake hidapi`

#### FreeBSD

`pkg install hidapi cmake`

#### Gentoo

1. Enable [nitratesky](https://github.com/VTimofeenko/nitratesky) overlay:

    `eselect repository enable nitratesky`
2. Install:

    `emerge -a app-misc/headsetcontrol`

#### NixOS

`headsetcontrol` is included in nixpkgs. To use it without installing, use:

`nix run nixpkgs#headsetcontrol`

To install it globally, add the following to your `configuration.nix`:
```nix
environment.systemPackages = [ pkgs.headsetcontrol ];
```

For the udev rules, add the following to your `configuration.nix`:
```nix
services.udev.packages = [ pkgs.headsetcontrol ];
```

### Compiling

```bash
git clone https://github.com/Sapd/HeadsetControl && cd HeadsetControl
mkdir build && cd build
cmake ..
make
```

To make `headsetcontrol` accessible globally, run:

```bash
sudo make install

# On LINUX, to access without root reboot your computer or run
sudo udevadm control --reload-rules && sudo udevadm trigger
```

This command installs the binary in a location that is globally accessible via your system's PATH. On Linux it also runs `headsetcontrol -u` for generating udev files and stores them in `/etc/udev/rules.d/` (used to allow non-root access)

### OS X

Recommendation: Use [Homebrew](https://brew.sh).

* To automatically compile and install the latest version:

```bash
brew install sapd/headsetcontrol/headsetcontrol --HEAD
```

* To manually compile, first install the dependencies:
`brew install hidapi cmake`

Note: Xcode must be downloaded via the Mac App Store for the compilers.

### Windows

* Binaries are available on the [releases](https://github.com/Sapd/HeadsetControl/releases) page.
* For compilation instructions using MSYS2/MinGW refer to the [wiki](https://github.com/Sapd/HeadsetControl/wiki/Development#windows).

## Usage

To view available options for your device, use:

```bash
headsetcontrol -h
```

For a complete list of all options, run:

```bash
headsetcontrol --help-all
```

To use headsetcontrol in scripts or other applications, explore:

```bash
headsetcontrol --output
```

(and the wiki article about [API development](https://github.com/Sapd/HeadsetControl/wiki/API-%E2%80%90-Building-Applications-on-top-of-HeadsetControl))

Note: When running the application from the current directory, prefix commands with `./`

### Third Party

The following additional software can be used to enable control via a GUI

#### OS X

[HeadsetControl-MacOSTray](https://github.com/ChrisLauinger77/HeadsetControl-MacOSTray) adds a system tray icon, displaying headset information. Also provides controls via submenus (OS X 14 and later)

#### Linux

[headsetcontrol-notifcationd](https://github.com/Manawyrm/headsetcontrol-notificationd) provides notifications on the battery status of connected headsets (PHP based)

[headset-charge-indicator](https://github.com/centic9/headset-charge-indicator/) adds a system tray icon, displaying the current amount of battery. Also provides controls via the icon's menu (Python based)

[gnome-shell-extension-HeadsetControl](https://github.com/ChrisLauinger77/gnome-shell-extension-HeadsetControl/) adds a system tray icon, displaying headset information. Also provides controls via the icon's menu (gnome-shell 42 and later)

[headset-battery-indicator](https://github.com/ruflas/headset-battery-indicator) adds a system tray icon for managing USB headset features, including battery level, ChatMix, Sidetone, and Auto-Off time. (Python/Qt based)

#### Windows

[HeadsetControl-GUI](https://github.com/LeoKlaus/HeadsetControl-GUI)  a simply GUI tool to manage your headset and check battery status. (Qt C++ based)

[HeadsetControl-SystemTray](https://github.com/zampierilucas/HeadsetControl-SystemTray) adds a system tray icon, displaying the current amount of battery. (Python based)

[HeadsetControl-Qt](https://github.com/Odizinne/HeadsetControl-Qt) adds a system tray icon, GUI with various settings, Linux compatible. (Qt C++ based)

[aarol/headset-battery-indicator](https://github.com/aarol/headset-battery-indicator) adds a small native-looking system tray icon displaying the battery level. (Rust based)

## Development

Look at the [wiki](https://github.com/Sapd/HeadsetControl/wiki/Development) if you want to contribute and implement another device or improve the software.

## Release Cycle

HeadsetControl is designed to be a rolling-release software, with minor versions (0.x.0) providing new features in the software itself, and patch versions (0.0.x) fixing issues or adding support for new headsets. Major versions are reserved for bigger rewrites.

If you want to build and provide packages for it, we recommend building and providing the current git `HEAD` in most cases. This ensures that users have access to the latest features and fixes.

## Notice

HeadsetControl is distributed in the hope that it will be useful,\
but WITHOUT ANY WARRANTY; without even the implied warranty of\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\
GNU General Public License for more details.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repository.

## Summary

A tool to control certain aspects of USB-connected headsets on Linux. Currently, support is provided for adjusting sidetone, getting battery state, controlling LEDs, and setting the inactive time. See below for which headset supports which feature.

### Sidetone

Want to use your Headset under Linux or Mac OS X, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Supported Headsets

- Corsair Void (Every version*, regardless whether Elite, Pro, HS70, Wireless or Wired)
  - Sidetone, Battery (for Wireless), LED on/off, Notification Sound
- Logitech G430
  - No support in current version (Last working on macOS in commit 41be99379f)
- Logitech G432
  - Sidetone (only tested on Linux)
- Logitech G533
  - Sidetone, Battery (for Wireless)
- Logitech G633 / G635 / G733 / G933 / G935
  - Sidetone, Battery (for Wireless), LED on/off
- Logitech G930
  - Sidetone, Battery
- SteelSeries Arctis 1
  - Sidetone, Inactive time
- SteelSeries Arctis 1 for XBox
  - Sidetone, Battery, Inactive time
- SteelSeries Arctis (7 and Pro)
  - Sidetone, Battery, Inactive time, Chat-Mix level, LED on/off (allows to turn off the blinking LED on the base-station)
- SteelSeries Arctis 9
  - Sidetone, Battery, Inactive time, Chat-Mix level
- Logitech G PRO
  - Sidetone
- Logitech Zone Wired
  - Sidetone, Voice prompts, Rotate to mute
- Roccat Elo 7.1 Air
  - LED on/off, Inactive time (Note for Linux: Sidetone is handled by sound driver => use AlsaMixer)

For non-supported headsets on Linux: There is a chance that you can set the sidetone via AlsaMixer

&ast; *If your Corsair headset is not recognized, see [Adding a corsair device](https://github.com/Sapd/HeadsetControl/wiki/Adding-a-Corsair-device)*

For more features or other headsets, the protocol of the respective headset must be analyzed further. This can be done by capturing the USB traffic between the device and the original Windows software and analyzing it with WireShark or USBlyzer. For that, you can also use a virtual machine with USB passthrough. The [wiki](https://github.com/Sapd/HeadsetControl/wiki/Development) provides a tutorial.

## Building

### Prerequisites

You will need hidapi, c compilers and cmake. All usually installable via package managers.

#### Debian / Ubuntu

`apt-get install build-essential git cmake libhidapi-dev`

#### CentOS / RHEL (RedHat based)

RHEL and CentOS also require the epel-repository: `yum install epel-release`. Please inform yourself about the consequences of activating the epel-repository.

`yum groupinstall "Development tools"`
`yum install git cmake hidapi-devel`

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

#### Mac OS X

I recommend using [Homebrew](https://brew.sh).

You can automatically compile and install the latest version of the software, by using
`brew install sapd/headsetcontrol/headsetcontrol --HEAD`.

If you wish to compile it manually, you can install the dependencies with  `brew install hidapi cmake`.

Also you have to download Xcode via the Mac App Store for the compilers.

#### Windows

Windows support is a bit experimental and might not work in all cases. You can find binaries in the [releases](https://github.com/Sapd/HeadsetControl/releases) page, or compile instructions via MSYS2/MinGW in the [wiki](https://github.com/Sapd/HeadsetControl/wiki/Development#windows).

### Compiling

```
git clone https://github.com/Sapd/HeadsetControl && cd HeadsetControl
mkdir build && cd build
cmake ..
make
```

If you want to be able to call HeadsetControl from every folder type:

```bash
make install
```

This will copy the binary to a folder globally accessible via path.

### Access without root

Also in Linux, you need udev rules if you don't want to start the application with root. Those rules are generated via `headsetcontrol -u`. Typing `make install` on Linux generates and writes them automatically to /etc/udev/rules.d/.

You can reload udev configuration without reboot via `sudo udevadm control --reload-rules && sudo udevadm trigger`

## Usage

Type `headsetcontrol -h` to get all available options.\
(Don't forget to prefix it with `./` when the application resides in the current folder)

Type `headsetcontrol -?` to get a list of supported capabilities for the currently detected headset.

`headsetcontrol -s 128` sets the sidetone to 128 (REAL loud). You can silence it with `0`. I recommend a loudness of 16.

Following options don't work on all devices yet:

`headsetcontrol -b` check battery level. Returns a value from 0 to 100 or loading.

`headsetcontrol -n 0|1` sends a notification sound, made by the headset. 0 or 1 are currently supported as values.

`headsetcontrol -l 0|1` switches LED off/on (off almost doubles battery lifetime!).

`headsetcontrol -c` cut unnecessary output, for reading by other scripts or applications.

`headsetcontrol -i 0-90` sets inactive time in minutes, time must be between 0 and 90, 0 disables the feature.

`headsetcontrol -m` retrieves the current chat-mix-dial level setting.

`headsetcontrol -u` Generates and outputs udev-rules for Linux.

### Third Party

The following additional software can be used to enable control via a GUI

#### Linux

[headsetcontrol-notifcationd](https://github.com/Manawyrm/headsetcontrol-notificationd) provides notifications on the battery status of connected headsets (PHP based)

[headset-charge-indicator](https://github.com/centic9/headset-charge-indicator/) adds a system tray icon, displaying the current amount of battery. Also provides controls via the icon's menu (Python based)

#### Windows

[HeadsetControl-SystemTray](https://github.com/zampierilucas/HeadsetControl-SystemTray) adds a system tray icon, displaying the current amount of battery. (Python based)

## Development

Look at the [wiki](https://github.com/Sapd/HeadsetControl/wiki/Development) if you want to contribute and implement another device or improve the software.

## Notice

HeadsetControl is distributed in the hope that it will be useful,\
but WITHOUT ANY WARRANTY; without even the implied warranty of\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\
GNU General Public License for more details.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repository.

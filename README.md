## Summary

Want to use your Headset under Linux or Mac OS X, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Supported Headsets

### Sidetone

- Corsair Void (Wireless & Wired)
- Corsair Void Pro (Wireless & Wired)
- Logitech G930
- Logitech G633
- Logitech G533
- Logitech G430 (Last working on macOS in commit 41be99379f)
- SteelSeries Arctis 7
- SteelSeries Arctis Pro 2019 Edition

### Other Features

Corsair Void (Pro) also supports checking of the battery and switching LED off/on

For more features (like getting battery percentage of other devices, or settings LEDs etc. of specific devices), the protocol of the respective headset must be analyzed further. This can be done by capturing the USB traffic and analyzing it with WireShark or USBlyzer.

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

#### Mac OS X

I recommend using [Homebrew](https://brew.sh) for the dependencies.\
With homebrew you can simply install them by typing `brew install hidapi cmake`.

Also you will have to download Xcode via the Mac App Store for the compilers.

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
This will copy the binary to a folder globally accessable via path.

Also in Linux, you need udev rules if you don't want to start the application with root. Those rules reside in the udev folder of this repository. Typing make install on Linux copies them automatically to /etc/udev/rules.d/.

## Usage

Type `HeadsetControl -h` to get all available options.\
(Don't forget to prefix it with `./` when the application resides in the current folder)

`HeadsetControl -s 128` sets the sidetone to 128 (REAL loud). You can silence it with `0`. I recommend a loudness of 16.

Following options don't work on all devices yet:

`HeadsetControl -b` check battery level. Returns a value from 0 to 100 or loading.

`HeadsetControl -n 0|1` sends a notification sound, made by the headset. 0 or 1 are currently supported as values.

`HeadsetControl -l 0|1` switches LED off/on (off almost doubles battery lifetime!).

## Notice

HeadsetControl is distributed in the hope that it will be useful,\
but WITHOUT ANY WARRANTY; without even the implied warranty of\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\
GNU General Public License for more details.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repository.

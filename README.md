## Summary

Want to use your Headset under Linux or Mac OSX, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Supported Headsets

- Corsair Void
- Logitech G930
- Logitech G533
- Logitech G430 (Working on macOS but not Linux)

The wired versions of the wireless headsets should also be supported, but need testing and maybe the product id's need to be adjusted (make an issue for that).

## Other Features

Currently only setting sidetone is supported, but other features could be implemented as well (like getting battery percentage, or settings LEDs etc.). However for this to be implemented, the protocol of the headsets need to be analyzed further. This could be done by capturing the usb traffic and analyzing it logically (like I did for the sidetone support).

## Building

Building is really simple, you will only need libusb-1.0, c-compilers and cmake.  All usually installable via the systems package manager.
Debian based: `apt-get install build-essential cmake libusb-1.0-0-dev`
RedHat based: `yum groupinstall "Development tools"` `yum install cmake libusb1-devel`

Compiling steps:
```
mkdir build
cd build
cmake ..
make
```

If you want to be able to call HeadsetControl from every folder type:
```
make install
```
This whille copy the binary to a local folder globally accessable via path. You may need to run it with sudo/root on Linux.

Also in Linux, you need udev rules if you don't want to start the application with root. Those rules reside in the udev folder of this repository. Typing make install on Linux copies them automatically to /etc/udev/rules.d/.

### Building on Mac OSX

You need to install libusb first. I recommend using homebrew.
With homebrew you can simply install it by typing `brew install libusb`. Then you can procceed with the steps above.

## Usage

Type `headsetcontrol -h` to get all available options.
(Don't forget to prefix it with `./` when the application is in the current folder)

`headsetcontrol -s 128` sets the sidetone to 128 (REAL looud). You can silence it with `0`. I recommend a loudness of 16.

## Notice
Headsetcontrol is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repository.

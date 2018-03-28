## Summary

Want to use your Headset under Linux or Mac OSX, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Supported Headsets

Sidetone:
- Corsair Void
- Corsair Void Pro
- Logitech G930
- Logitech G533
- Logitech G430 (Last working on macOS in commit 41be99379f)

## Other Features

Corsair Void (Pro) also supports checking of battery.\
Other features could be implemented as well (like getting battery percentage of other devices, or settings LEDs etc.). However for this to be implemented, the protocol of the headsets need to be analyzed further. This could be done by capturing the usb traffic and analyzing it logically (like I did for the sidetone support).

## Building

Building is really simple, you will need hidapi, c-compilers and cmake.  All usually installable via the systems package manager.\
Debian/Ubuntu: `apt-get install build-essential cmake libhidapi-dev`

RHEL and CentOS also require the epel-repository: `yum install epel-release`. Please inform yourself on the consequences of activating epel-repository.\
RedHat based: `yum groupinstall "Development tools"` `yum install cmake hidapi-devel`

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

You need to install hidapi first. I recommend using homebrew.\
With homebrew you can simply install it by typing `brew install hidapi`. Then you can procceed with the steps above.

## Usage

Type `headsetcontrol -h` to get all available options.\
(Don't forget to prefix it with `./` when the application is in the current folder)

`headsetcontrol -s 128` sets the sidetone to 128 (REAL looud). You can silence it with `0`. I recommend a loudness of 16.

Following options don't work on all devices yet:

`headsetcontrol -b` check battery level. Returns a value from 0 to 100 or loading.

`headsetcontrol -n 0|1` sends a notification sound, made by the headset. 0 or 1 are currently supported as values. 

## Notice
Headsetcontrol is distributed in the hope that it will be useful,\
but WITHOUT ANY WARRANTY; without even the implied warranty of\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\
GNU General Public License for more details.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repository.

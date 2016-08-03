## Summary

Want to use your Headset under Linux, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Supported Headsets

- Corsair Void
- Logitech G930

The wired versions should also be supported, but need testing and maybe the product id's need to be adjusted (make an issue for that).

## Other Features

Currently only setting sidetone is supported, but other features could be implemented as well (like getting battery percentage, or settings LEDs etc.). However for this to be implemented, the protocol of the headsets need to be analyzed further. This could be done by capturing the usb traffic and analyzing it logically (like I did for the sidetone support).

## Building

Building is really simple, you will only need libusb-1.0 (which is already installed on most systems). Just type make to build the C program.
You also need a udev rule if you don't want to start it via root, type make install to install the udev rule which comes with this repository. Installing will also place the application in /usr/bin

## Usage

Just type headsetcontrol 128 if you want REAL loud sidetone (it's normally louder than with the native software on windows), or headsetcontrol 0 if you want to silence it completely. I recommend a loudness of 16.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repository.

## Synopsis

Want to use your G930 Headset under Linux, but you shout while talking because there is no support for sidetone? With sidetone, sometimes also called loopback, you can hear your own voice while
talking. This differs from a simple loopback via PulseAudio as you won't have any disturbing latency.

## Building

Building is really simple, you will only need libusb-1.0 (which is already installed on most systems). Just type make to build the C program.
There will be also a binary in the downloads section.
You also need a udev rule if you don't want to start it via root, type make install to install the udev rule which comes with this repository.

## Usage

Just type ./G930Sidetone 128 if you want REAL loud sidetone (it's louder than with the native software on windows), or ./G930Sidetone 0 if you want to silence it completly. I recommend a loudness of 16.

## License

Released under GPL v3.

## Like it?

If you like my software please star the repo.

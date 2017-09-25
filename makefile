default:
	gcc main.c -L"/usr/lib" -lusb-1.0 -std=gnu99 -o headsetcontrol
clean:
	rm headsetcontrol
install:
	cp udev/logitechg930.rules /etc/udev/rules.d/
	cp udev/logitech_g430.rules /etc/udev/rules.d/
	cp udev/corsairvoid.rules /etc/udev/rules.d/
	cp headsetcontrol /usr/bin

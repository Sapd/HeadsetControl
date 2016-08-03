default:
	gcc main.c -L"/usr/lib" -lusb-1.0 -std=gnu99 -o G930Sidetone
clean:
	rm G930Sidetone
install:
	cp udev/logitechg930.rules /etc/udev/rules.d/
	cp udev/corsairvoid.rules /etc/udev/rules.d/

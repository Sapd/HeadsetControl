default:
	gcc main.c -L"/usr/lib" -lusb-1.0 -o G930Sidetone
	./G930Sidetone

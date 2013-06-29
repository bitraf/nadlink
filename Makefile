all: nadlink

clean:
	rm -f nadlink

install: nadlink
	install -m 755 -o root -g root -d $(DESTDIR)/usr
	install -m 755 -o root -g root -d $(DESTDIR)/usr/bin
	install -m 755 -o root -g root nadlink $(DESTDIR)/usr/bin/nadlink

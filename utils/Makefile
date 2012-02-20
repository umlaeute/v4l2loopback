PREFIX = /usr
BINDIR = $(PREFIX)/bin

INSTALL = install
INSTALL_PROGRAM = $(INSTALL) -p -m 755
INSTALL_DIR     = $(INSTALL) -p -m 755 -d


all: v4l2loopback_io

v4l2loopback:
	@touch $@

v4l2loopback_io: v4l2loopback_io.c Makefile
	$(CC) -o $@ $<

install: v4l2loopback v4l2loopback_io
	$(INSTALL_DIR) "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) $^ "$(DESTDIR)$(BINDIR)"

clean:
	rm -f *~ v4l2loopback_io

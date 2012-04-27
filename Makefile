KERNEL_VERSION	:= `uname -r`
KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build
PWD		:= $(shell pwd)
obj-m		:= v4l2loopback.o

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
MAN1DIR = $(MANDIR)/man1
INSTALL = install
INSTALL_PROGRAM = $(INSTALL) -p -m 755
INSTALL_DIR     = $(INSTALL) -p -m 755 -d
INSTALL_DATA    = $(INSTALL) -m 644

MODULE_OPTIONS = devices=2

##########################################
# note on build targets
#
# module-assistant makes some assumptions about targets, namely
#  <modulename>: must be present and build the module <modulename>
#                <modulename>.ko is not enough
# install: must be present (and should only install the module)
#
# we therefore make <modulename> a .PHONY alias to <modulename>.ko
# and remove utils-installation from 'install'
# call 'make install-all' if you want to install everything
##########################################


.PHONY: all install clean distclean
.PHONY: install-all install-utils install-man
.PHONY: modprobe v4l2loopback

all: v4l2loopback.ko
v4l2loopback: v4l2loopback.ko
v4l2loopback.ko:
	@echo "Building v4l2-loopback driver..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
install-all: install install-utils install-man
install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -ae
install-utils: utils/v4l2loopback-ctl
	$(INSTALL_DIR) "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) $< "$(DESTDIR)$(BINDIR)"
install-man: man/v4l2loopback-ctl.1
	$(INSTALL_DIR) "$(DESTDIR)$(MAN1DIR)"
	$(INSTALL_DATA) $< "$(DESTDIR)$(MAN1DIR)"
clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
distclean: clean
	rm -f man/v4l2loopback-ctl.1

modprobe: v4l2loopback.ko
	chmod a+r v4l2loopback.ko
	sudo modprobe videodev
	-sudo rmmod v4l2loopback
	sudo insmod ./v4l2loopback.ko $(MODULE_OPTIONS)


man/v4l2loopback-ctl.1: utils/v4l2loopback-ctl
	help2man -N --name "control v4l2 loopback devices" $^ > $@

KERNEL_VERSION	:= `uname -r`
KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build
PWD		:= $(shell pwd)
obj-m		:= v4l2loopback.o

PREFIX = /usr
BINDIR = $(PREFIX)/bin
INSTALL = install
INSTALL_PROGRAM = $(INSTALL) -p -m 755
INSTALL_DIR     = $(INSTALL) -p -m 755 -d

MODULE_OPTIONS = devices=2

.PHONY: all install install-utils clean modprobe


all: v4l2loopback.ko
v4l2loopback.ko:
	@echo "Building v4l2-loopback driver..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
install: install-utils
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -ae
install-utils: utils/v4l2loopback-ctl
	$(INSTALL_DIR) "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) $< "$(DESTDIR)$(BINDIR)"
clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

modprobe: v4l2loopback.ko
	chmod a+r v4l2loopback.ko
	sudo modprobe videodev
	-sudo rmmod v4l2loopback
	sudo insmod ./v4l2loopback.ko $(MODULE_OPTIONS)

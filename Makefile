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


all: v4l2loopback
v4l2loopback:
	@echo "Building v4l2-loopback driver..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -ae
	$(INSTALL_DIR) "$(DESTDIR)$(BINDIR)"
	$(INSTALL_PROGRAM) v4l2loopback-ctl "$(DESTDIR)$(BINDIR)"
clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

modprobe: v4l2loopback
	chmod a+r v4l2loopback.ko
	sudo modprobe videodev
	-sudo rmmod $<
	sudo insmod ./v4l2loopback.ko $(MODULE_OPTIONS)

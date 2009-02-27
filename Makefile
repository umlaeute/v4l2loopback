# Makefile for YAVLD
#
# Distributed according to the GPL.
#


MODULE_NAME = yavld
$(MODULE_NAME)-objs = v4l2loopback.o

PWD	:= $(shell pwd)

# First pass, kernel Makefile reads module objects
ifneq ($(KERNELRELEASE),)
obj-m	:= $(MODULE_NAME).o

EXTRA_CFLAGS += -I$(PWD)/../include

# Second pass, the actual build.
else
KDIR	:= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -ae

endif

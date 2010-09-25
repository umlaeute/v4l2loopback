KERNEL_VERSION	:= `uname -r`
KERNEL_DIR	:= /lib/modules/$(KERNEL_VERSION)/build

PWD		:= $(shell pwd)

obj-m		:= v4l2loopback.o

all: v4l2loopback
v4l2loopback:
	@echo "Building vloopback driver..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -ae
clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
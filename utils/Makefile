CPPFLAGS += -I..
ifneq ($(V4L2LOOPBACK_SNAPSHOT_VERSION),)
CPPFLAGS += -DSNAPSHOT_VERSION='"$(V4L2LOOPBACK_SNAPSHOT_VERSION)"'
endif

prefix?=/usr
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
MKDIR_P = /usr/bin/mkdir -p

.PHONY: default clean

programs = v4l2loopback-ctl

default: $(programs)

clean:
	-rm $(programs)
	-rm $(programs:%=%.o)

install:
	$(MKDIR_P) $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) $(programs) $(DESTDIR)$(bindir)

v4l2loopback-ctl.o: v4l2loopback-ctl.c ../v4l2loopback.h
v4l2loopback-ctl: v4l2loopback-ctl.o

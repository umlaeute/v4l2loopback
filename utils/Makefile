CPPFLAGS += -I..
ifneq ($(V4L2LOOPBACK_SNAPSHOT_VERSION),)
CPPFLAGS += -DSNAPSHOT_VERSION='"$(V4L2LOOPBACK_SNAPSHOT_VERSION)"'
endif

.PHONY: default clean

programs = v4l2loopback-ctl

default: $(programs)

clean:
	-rm $(programs)

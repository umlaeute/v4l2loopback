TARGETS=test yuv4mpeg_to_v4l2 ondemandcam yuv420_infiniteloop

.PHONY: all clean

all: $(TARGETS)

ondemandcam: ondemandcam.c
	gcc -o ondemandcam ondemandcam.c -lrt -lpthread

clean:
	-rm $(TARGETS)

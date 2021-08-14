#!/bin/bash

device=${1:-/dev/video0}
echo "Using $device"

run_writers() {
    declare -a nbufs
    nbufs=(10 9 4 5 6)
    #nbufs=(12 12 12 12 12)
    #for i in $(seq 0 4); do
    for i in 1; do
        sleep 1
        gst-launch-1.0 videotestsrc horizontal-speed=1 num-buffers=90 ! v4l2sink device=$device
    done
}

#v4l2-ctl -d $device -c keep_format=1 || exit 1
./utils/v4l2loopback-ctl set-caps "video/x-raw, format=UYVY, width=640, height=480, framerate=(fraction)25/1" $device || exit 1
v4l2-ctl -d "$device" -c sustain_framerate=0 || exit 1
v4l2-ctl -d "$device" -c timeout=2000 || exit 1
gst-launch-1.0 videotestsrc num-buffers=1 ! v4l2sink device=$device || exit 1
{
    run_writers
    sleep 10
    # can see a flash of green here
    v4l2-ctl -d "$device" -c sustain_framerate=1 || exit 1
    run_writers
    sleep 10
    run_writers
} >/dev/null &
gst-launch-1.0 v4l2src device=$device ! timeoverlay ! videoconvert ! autovideosink
kill $! 2>/dev/null
wait

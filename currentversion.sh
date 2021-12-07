#!/bin/sh

function num() {
	grep "^#define V4L2LOOPBACK_VERSION_$1 " v4l2loopback.h | awk '{print $3}'
}

echo `num "MAJOR"`.`num "MINOR"`.`num "BUGFIX"`

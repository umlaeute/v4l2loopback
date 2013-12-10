#!/bin/sh

grep "^#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION" v4l2loopback.c \
| sed -e 's|^#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION||' \
      -e 's|^[^0-9]*||' -e 's|[^0-9]*$||' \
      -e 's|[^0-9][^0-9]*|.|g'

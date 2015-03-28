#!/bin/sh

## usage:
# echo "V4L2_PIX_FMT_MPEG4 MPEG-4 part 2 ES" | $0
## normally it's more like:
# cat /usr/include/linux/videodev2.h \
# | grep "define V4L2_PIX_FMT" \
# | sed -e "s|^#define ||" -e "s|v4l2_fourcc('.', '.', '.', '.')||" -e 's|/\*||' -e 's|\*/||' \
# | $0

DEPTH=0
FLAGS=0

while read FOURCC NAME
do
  echo "#ifdef ${FOURCC}"
  echo "{"
  echo "   .name     = \"${NAME}\","
  echo "     .fourcc   = ${FOURCC},"
  echo "     .depth    = ${DEPTH},"
  echo "     .flags    = ${FLAGS},"
  echo "     },"
  echo "#endif /* ${FOURCC} */"
done

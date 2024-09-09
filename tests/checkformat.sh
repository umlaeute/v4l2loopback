#!/bin/sh

device=${1:-/dev/video0}

if [ ! -e "${device}" ]; then
    echo "Unable to open device ${device}" 1>&2
    exit 1
fi


parse_output() {
    # EAGAIN EBUSY EEXIST EFAULT EINVAL EIO ENODEV ENOMEM ENOSPC ENOSYS ENOTTY
    while read line; do
        if echo "${line}" | grep -q "Invalid argument"; then
            echo "EINVAL"
            return
        fi
        if echo "${line}" | grep -q "Device or resource busy"; then
            echo "EBUSY"
            return
        fi
        if echo "${line}" | grep -q "Width/Height"; then
            echo "${line}" | awk '{print $3}'
            return
        fi
    done
}

parse_enum_fmt() {
    local format
    local count=0
    while read line; do
        if echo "${line}" | grep -q "\[[0-9]*\]"; then
            format=$(printf "%-4s" $(echo "${line}" | awk '{print $2}' | sed -e "s|'||g"))
        fi
        if echo "${line}" | grep -q "Size: "; then
            echo "- ${format}:$(echo $line | awk '{$1=""; $2=""; print $0}' | sed -e 's|[[:space:]]||g')"
            count=$((count+1))
        fi
    done
    [  ${count} -gt 0 ]  || echo "- none"
}

output() {
    echo "| ioctl | capture | output |"
    echo "| ----- | ------- | ------ |"
    echo "| GET | ${G_FMT_CAP} | ${G_FMT_OUT} |"
    echo "| TRY0 | ${TRY_FMT_CAP0} | ${TRY_FMT_OUT0} |"
    echo "| TRY1 | ${TRY_FMT_CAP1:--} | ${TRY_FMT_OUT1:--} |"
    echo "| SET0 | ${S_FMT_CAP0:--} | ${S_FMT_OUT0:--} |"
    echo "| SET1 | ${S_FMT_CAP1:--} | ${S_FMT_OUT1:--} |"
}


G_FMT_CAP=$(v4l2-ctl -d "${device}" -V | parse_output)
G_FMT_OUT=$(v4l2-ctl -d "${device}" -X | parse_output)


TRY_FMT_CAP0=$(v4l2-ctl -d "${device}" --try-fmt-video "width=640,height=480" | parse_output)
TRY_FMT_CAP1=$(v4l2-ctl -d "${device}" --try-fmt-video "width=640,height=4800" | parse_output)
TRY_FMT_OUT0=$(v4l2-ctl -d "${device}" --try-fmt-video-out "width=640,height=480" | parse_output)
TRY_FMT_OUT1=$(v4l2-ctl -d "${device}" --try-fmt-video-out "width=640,height=4800" | parse_output)

S_FMT_CAP0=$(v4l2-ctl -d "${device}" -v "width=640,height=480" | parse_output)
[ -n "${S_FMT_CAP0}" ] || S_FMT_CAP0=$(v4l2-ctl -d "${device}" -V | parse_output)
S_FMT_CAP1=$(v4l2-ctl -d "${device}" -v "width=640,height=4800" | parse_output)
[ -n "${S_FMT_CAP1}" ] || S_FMT_CAP1=$(v4l2-ctl -d "${device}" -V | parse_output)

S_FMT_OUT0=$(v4l2-ctl -d "${device}" -x "width=640,height=480" | parse_output)
[ -n "${S_FMT_OUT0}" ] || S_FMT_OUT0=$(v4l2-ctl -d "${device}" -X | parse_output)
S_FMT_OUT1=$(v4l2-ctl -d "${device}" -x "width=640,height=4800" | parse_output)
[ -n "${S_FMT_OUT1}" ] || S_FMT_OUT1=$(v4l2-ctl -d "${device}" -X | parse_output)



output | column -t

echo
echo "OUTPUT formats"
v4l2-ctl -d "${device}" --list-formats-out-ext | parse_enum_fmt

echo
echo "CAPTURE formats"
v4l2-ctl -d "${device}" --list-formats-ext | parse_enum_fmt

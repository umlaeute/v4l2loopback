/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * v4l2loopback.h
 *
 * Written by IOhannes m zmölnig, 7/1/20.
 *
 * Copyright 2020 by IOhannes m zmölnig.  Redistribution of this file is
 * permitted under the GNU General Public License.
 */
#ifndef _V4L2LOOPBACK_H
#define _V4L2LOOPBACK_H

#define V4L2LOOPBACK_VERSION_MAJOR 0
#define V4L2LOOPBACK_VERSION_MINOR 13
#define V4L2LOOPBACK_VERSION_BUGFIX 2

/* /dev/v4l2loopback interface */

struct v4l2_loopback_config {
	/**
         * the device-number (/dev/video<nr>)
         * V4L2LOOPBACK_CTL_ADD:
         * setting this to a value<0, will allocate an available one
         * if nr>=0 and the device already exists, the ioctl will EEXIST
         * if output_nr and capture_nr are the same, only a single device will be created
	 * NOTE: currently split-devices (where output_nr and capture_nr differ)
	 *   are not implemented yet.
	 *   until then, requesting different device-IDs will result in EINVAL.
         *
         * V4L2LOOPBACK_CTL_QUERY:
         * either both output_nr and capture_nr must refer to the same loopback,
         * or one (and only one) of them must be -1
         *
         */
	__s32 output_nr;
	__s32 unused; /*capture_nr;*/

	/**
         * a nice name for your device
         * if (*card_label)==0, an automatic name is assigned
         */
	char card_label[32];

	/**
         * allowed frame size
         * if too low, default values are used
         */
	__u32 min_width;
	__u32 max_width;
	__u32 min_height;
	__u32 max_height;

	/**
         * number of buffers to allocate for the queue
         * if set to <=2, default values are used
         */
	__u32 max_buffers;

	/**
         * set device debug flags. See V4L2_DEV_DEBUG_* for possible values.
         */
	__u32 debug;

	/**
         * whether to announce OUTPUT/CAPTURE capabilities exclusively
         * for this device or not
         * (!exclusive_caps)
	 * NOTE: this is going to be removed once separate output/capture
	 *       devices are implemented
         */
	__u32 announce_all_caps;
};

/* a pointer to a (struct v4l2_loopback_config) that has all values you wish to impose on the
 * to-be-created device set.
 * if the ptr is NULL, a new device is created with default values at the driver's discretion.
 *
 * returns the device_nr of the OUTPUT device (which can be used with V4L2LOOPBACK_CTL_QUERY,
 * to get more information on the device)
 */
#define V4L2LOOPBACK_CTL_ADD 0x4C80

/* a pointer to a (struct v4l2_loopback_config) that has output_nr and/or capture_nr set
 * (the two values must either refer to video-devices associated with the same loopback device
 *  or exactly one of them must be <0
 */
#define V4L2LOOPBACK_CTL_QUERY 0x4C82

/* the device-number (either CAPTURE or OUTPUT) associated with the loopback-device */
#define V4L2LOOPBACK_CTL_REMOVE 0x4C81

#endif /* _V4L2LOOPBACK_H */

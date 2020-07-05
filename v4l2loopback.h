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

/* /dev/v4l2loopback interface */

struct v4l2_loopback_config {
	/** 
         * the device-number (/dev/video<nr>)
         * V4L2LOOPBACK_CTL_ADD:
         * setting this to "-1" will allocate an available one
         * if nr>=0 and the device already exists, the ioctl will EEXIST
         * if output_nr and capture_nr are the same, only a single device will be created
         *
         * V4L2LOOPBACK_CTL_QUERY:
         * either both output_nr and capture_nr must refer to the same loopback,
         * or one (and only one) of them must be -1
         *
         */
	int output_nr;
	int capture_nr;

	/**
         * a nice name for your device
         * if (*card_label)==0, an automatic name is assigned
         */
	char card_label[32];

	/**
         * maximum allowed frame size
         * if too low, default values are used
         */
	int max_width;
	int max_height;

	/**
         * whether to announce OUTPUT/CAPTURE capabilities exclusively
         * for this device or not
         * (!exclusive_caps)
         */
	int announce_all_caps;

	/**
         * number of buffers to allocate for the queue
         * if set to <=0, default values are used
         */
	int max_buffers;

	/**
         * how many consumers are allowed to open this device concurrently
         * if set to <=0, default values are used
         */
	int max_openers;

	/**
         * set the debugging level for this device
         */
	int debug;
};

#define V4L2LOOPBACK_CTL_ADD 0x4C80
#define V4L2LOOPBACK_CTL_REMOVE 0x4C81
#define V4L2LOOPBACK_CTL_QUERY 0x4C82

#endif /* _V4L2LOOPBACK_H */

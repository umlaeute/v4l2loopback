/*
 *      v4l2loopback.h  --  video 4 linux loopback driver
 *
 *      Copyright (C) 2005-2009
 *          Vasily Levin (vasaka@gmail.com)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

#ifndef _V4L2LOOPBACK_H
#define	_V4L2LOOPBACK_H

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
 /* fixed inner buffers number */
/* TODO(vasaka) make this module parameter */
#define V4L2_LOOPBACK_BUFFERS_NUMBER 4
#define V4L2_LOOPBACK_MAX_OPENERS 10

/* TODO(vasaka) use typenames which are common to kernel, but first find out if
 * it is needed */
/* struct keeping state and settings of loopback device */
struct v4l2_loopback_device {
	struct video_device *vdev;
	/* pixel and stream format */
	struct v4l2_pix_format pix_format;
	struct v4l2_captureparm capture_param;
	/* buffers stuff */
	__u8 *image;         /* pointer to actual buffers data */
	int buffers_number;  /* should not be big, 4 is a good choise */
	struct v4l2_buffer *buffers;	/* inner driver buffers */
	int write_position; /* number of last written frame + 1 */
	long buffer_size;
	/* sync stuff */
	int open_count;
	int ready_for_capture;/* set to true when at least one writer opened
			      * device and negotiated format */
	wait_queue_head_t read_event;
};
/* types of opener shows what opener wants to do with loopback */
enum opener_type {
	UNNEGOTIATED = 0,
	READER = 1,
	WRITER = 2,
};
/* struct keeping state and type of opener */
struct v4l2_loopback_opener {
	enum opener_type type;
	int buffers_number;
	int position; /* number of last processed frame + 1 or
		       * write_position - 1 if reader went out of sinc */
	struct v4l2_buffer *buffers;
};
#endif				/* _V4L2LOOPBACK_H */

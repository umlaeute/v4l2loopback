/* -*- c-file-style: "linux" -*- */
/*
 * common.h  --  some commong functions
 *
 * Copyright (C) 2023 IOhannes m zmoelnig (zmoelnig@iem.at)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <stdio.h>
#include <linux/videodev2.h>

static char *fourcc2str(unsigned int fourcc, char buf[4])
{
	buf[0] = (fourcc >> 0) & 0xFF;
	buf[1] = (fourcc >> 8) & 0xFF;
	buf[2] = (fourcc >> 16) & 0xFF;
	buf[3] = (fourcc >> 24) & 0xFF;

	return buf;
}
static const char *field2str(unsigned int field)
{
	switch (field) {
	case V4L2_FIELD_ANY:
		return "any";
	case V4L2_FIELD_NONE:
		return "none";
	case V4L2_FIELD_TOP:
		return "top";
	case V4L2_FIELD_BOTTOM:
		return "bottom";
	case V4L2_FIELD_INTERLACED:
		return "interlaced";
	case V4L2_FIELD_SEQ_TB:
		return "seq/topbottom";
	case V4L2_FIELD_SEQ_BT:
		return "seq/bottomtop";
	case V4L2_FIELD_ALTERNATE:
		return "alternate";
	case V4L2_FIELD_INTERLACED_TB:
		return "interlaced/topbottom";
	case V4L2_FIELD_INTERLACED_BT:
		return "interlaced/bottomtop";

	default:
		break;
	}
	return "unknown";
}

static const char *buftype2str(unsigned int type)
{
	switch (type) {
	default:
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return "CAPTURE";
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return "CAPTURE(planar)";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return "OUTPUT";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return "OUTPUT(planar)";
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
		return "OUTPUT(overlay)";
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return "OVERLAY";
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return "VBI(capture)";
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		return "VBI(output)";
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		return "SlicedVBI(capture)";
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		return "SlicedVBI(output)";
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		return "SDR(capture)";
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		return "SDR(output)";
	case V4L2_BUF_TYPE_META_CAPTURE:
		return "META(capture)";
	case V4L2_BUF_TYPE_META_OUTPUT:
		return "META(output)";
	case V4L2_BUF_TYPE_PRIVATE:
		return "private";
	}
	return "unknown";
}

static const char *snprintf_format(char *buf, size_t size,
				   struct v4l2_format *fmt)
{
	char fourcc[5];
	fourcc[4] = 0;
	switch (fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		snprintf(buf, size, "%s:%dx%d:%s (%u/%u) field=%s",
			 buftype2str(fmt->type), fmt->fmt.pix.width,
			 fmt->fmt.pix.height,
			 fourcc2str(fmt->fmt.pix.pixelformat, fourcc),
			 fmt->fmt.pix.bytesperline, fmt->fmt.pix.sizeimage,
			 field2str(fmt->fmt.pix.field));
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		snprintf(buf, size, "%s:%dx%d:%s (%d planes) field=%s",
			 buftype2str(fmt->type), fmt->fmt.pix_mp.width,
			 fmt->fmt.pix_mp.height,
			 fourcc2str(fmt->fmt.pix_mp.pixelformat, fourcc),
			 fmt->fmt.pix_mp.num_planes,
			 field2str(fmt->fmt.pix_mp.field));
	default:
		snprintf(buf, size, "TODO: %s(type=%d)", __FUNCTION__,
			 (fmt->type));
	}
	return buf;
}

static const char *snprintf_buffer(char *strbuf, size_t size,
				   struct v4l2_buffer *buf)
{
	snprintf(strbuf, size, "@%p #%d:%s (bytes=%d) field=%s @%ld.%06ld",
		 buf,
		 buf->index, buftype2str(buf->type), buf->bytesused,
		 field2str(buf->field), buf->timestamp.tv_sec,
		 buf->timestamp.tv_usec);
	return strbuf;
}

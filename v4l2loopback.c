/* -*- c-file-style: "linux" -*- */
/*
 * v4l2loopback.c  --  video4linux2 loopback driver
 *
 * Copyright (C) 2005-2009 Vasily Levin (vasaka@gmail.com)
 * Copyright (C) 2010-2019 IOhannes m zmoelnig (zmoelnig@iem.at)
 * Copyright (C) 2011 Stefan Diewald (stefan.diewald@mytum.de)
 * Copyright (C) 2012 Anton Novikov (random.plant@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/eventpoll.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>

#include <linux/miscdevice.h>
#include "v4l2loopback.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
#error This module is not supported on kernels before 4.0.0.
#endif

#if defined(timer_setup) && defined(from_timer)
#define HAVE_TIMER_SETUP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
#define VFL_TYPE_VIDEO VFL_TYPE_GRABBER
#endif

#define V4L2LOOPBACK_VERSION_CODE                                              \
	KERNEL_VERSION(V4L2LOOPBACK_VERSION_MAJOR, V4L2LOOPBACK_VERSION_MINOR, \
		       V4L2LOOPBACK_VERSION_BUGFIX)

MODULE_DESCRIPTION("V4L2 loopback video device");
MODULE_AUTHOR("Vasily Levin, "
	      "IOhannes m zmoelnig <zmoelnig@iem.at>,"
	      "Stefan Diewald,"
	      "Anton Novikov"
	      "et al.");
MODULE_LICENSE("GPL");

/*
 * helpers
 */
#define STRINGIFY(s) #s
#define STRINGIFY2(s) STRINGIFY(s)

#define dprintk(fmt, args...)                                                  \
	do {                                                                   \
		if (debug > 0) {                                               \
			printk(KERN_INFO "v4l2-loopback[" STRINGIFY2(          \
				       __LINE__) "]: " fmt,                    \
			       ##args);                                        \
		}                                                              \
	} while (0)

#define MARK()                                                                 \
	do {                                                                   \
		if (debug > 1) {                                               \
			printk(KERN_INFO "%s:%d[%s]\n", __FILE__, __LINE__,    \
			       __func__);                                      \
		}                                                              \
	} while (0)

#define dprintkrw(fmt, args...)                                                \
	do {                                                                   \
		if (debug > 2) {                                               \
			printk(KERN_INFO "v4l2-loopback[" STRINGIFY2(          \
				       __LINE__) "]: " fmt,                    \
			       ##args);                                        \
		}                                                              \
	} while (0)

static inline void v4l2l_get_timestamp(struct v4l2_buffer *b)
{
	struct timespec64 ts;
	ktime_get_ts64(&ts);

	b->timestamp.tv_sec = ts.tv_sec;
	b->timestamp.tv_usec = (ts.tv_nsec / NSEC_PER_USEC);
}

#if !defined(__poll_t)
typedef unsigned __poll_t;
#endif

/* module constants
 *  can be overridden during he build process using something like
 *	make KCPPFLAGS="-DMAX_DEVICES=100"
 */

/* maximum number of v4l2loopback devices that can be created */
#ifndef MAX_DEVICES
#define MAX_DEVICES 8
#endif

/* when a producer is considered to have gone stale */
#ifndef MAX_TIMEOUT
#define MAX_TIMEOUT (100 * 1000) /* in msecs */
#endif

/* max buffers that can be mapped, actually they
 * are all mapped to max_buffers buffers */
#ifndef MAX_BUFFERS
#define MAX_BUFFERS 32
#endif

/* module parameters */
static int debug = 0;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debugging level (higher values == more verbose)");

#define V4L2LOOPBACK_DEFAULT_MAX_BUFFERS 2
static int max_buffers = V4L2LOOPBACK_DEFAULT_MAX_BUFFERS;
module_param(max_buffers, int, S_IRUGO);
MODULE_PARM_DESC(max_buffers,
		 "how many buffers should be allocated [DEFAULT: " STRINGIFY2(
			 V4L2LOOPBACK_DEFAULT_MAX_BUFFERS) "]");

/* how many times a device can be opened
 * the per-module default value can be overridden on a per-device basis using
 * the /sys/devices interface
 *
 * note that max_openers should be at least 2 in order to get a working system:
 *   one opener for the producer and one opener for the consumer
 *   however, we leave that to the user
 */
#define V4L2LOOPBACK_DEFAULT_MAX_OPENERS 10
static int max_openers = V4L2LOOPBACK_DEFAULT_MAX_OPENERS;
module_param(max_openers, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(
	max_openers,
	"how many users can open the loopback device [DEFAULT: " STRINGIFY2(
		V4L2LOOPBACK_DEFAULT_MAX_OPENERS) "]");

static int devices = -1;
module_param(devices, int, 0);
MODULE_PARM_DESC(devices, "how many devices should be created");

static int video_nr[MAX_DEVICES] = { [0 ...(MAX_DEVICES - 1)] = -1 };
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr,
		 "video device numbers (-1=auto, 0=/dev/video0, etc.)");

static int output_nr[MAX_DEVICES] = { [0 ...(MAX_DEVICES - 1)] = -1 };
module_param_array(output_nr, int, NULL, 0444);
MODULE_PARM_DESC(output_nr,
		 "output device numbers (-1=auto, 0=/dev/video0, etc.)");

static char *card_label[MAX_DEVICES];
module_param_array(card_label, charp, NULL, 0000);
MODULE_PARM_DESC(card_label, "card labels for each device");

/* format specifications */
#define V4L2LOOPBACK_SIZE_MIN_WIDTH 48
#define V4L2LOOPBACK_SIZE_MIN_HEIGHT 32
#define V4L2LOOPBACK_SIZE_DEFAULT_MAX_WIDTH 8192
#define V4L2LOOPBACK_SIZE_DEFAULT_MAX_HEIGHT 8192

#define V4L2LOOPBACK_SIZE_DEFAULT_WIDTH 640
#define V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT 480

static int max_width = V4L2LOOPBACK_SIZE_DEFAULT_MAX_WIDTH;
module_param(max_width, int, S_IRUGO);
MODULE_PARM_DESC(max_width, "maximum allowed frame width [DEFAULT: " STRINGIFY2(
				    V4L2LOOPBACK_SIZE_DEFAULT_MAX_WIDTH) "]");
static int max_height = V4L2LOOPBACK_SIZE_DEFAULT_MAX_HEIGHT;
module_param(max_height, int, S_IRUGO);
MODULE_PARM_DESC(max_height,
		 "maximum allowed frame height [DEFAULT: " STRINGIFY2(
			 V4L2LOOPBACK_SIZE_DEFAULT_MAX_HEIGHT) "]");

/* frame intervals */
#define V4L2LOOPBACK_FPS_MIN 1
#define V4L2LOOPBACK_FPS_MAX 1000

static DEFINE_IDR(v4l2loopback_index_idr);
static DEFINE_MUTEX(v4l2loopback_ctl_mutex);

static int idr_alloc1(struct idr *idr, void *ptr, int *nr)
{
	int err;

	/* allocate id, if @id >= 0, we're requesting that specific id */
	if (*nr >= 0) {
		err = idr_alloc(&v4l2loopback_index_idr, ptr, *nr, *nr + 1,
				GFP_KERNEL);
		if (err == -ENOSPC)
			err = -EEXIST;
	} else {
		err = idr_alloc(&v4l2loopback_index_idr, ptr, 0, 0, GFP_KERNEL);
	}

	if (err < 0)
		return err;

	*nr = err;
	return 0;
}

static int idr_alloc2(struct idr *idr, void *ptr, int *nr1, int *nr2)
{
	int nr1_copy = *nr1;
	int err;

	err = idr_alloc1(idr, ptr, &nr1_copy);
	if (err)
		return err;

	err = idr_alloc1(idr, ptr, nr2);
	if (err)
		idr_remove(idr, nr1_copy);
	else
		*nr1 = nr1_copy;

	return err;
}

/* control IDs */
#define V4L2LOOPBACK_CID_BASE (V4L2_CID_USER_BASE | 0xf000)
#define CID_KEEP_FORMAT (V4L2LOOPBACK_CID_BASE + 0)
#define CID_SUSTAIN_FRAMERATE (V4L2LOOPBACK_CID_BASE + 1)
#define CID_TIMEOUT (V4L2LOOPBACK_CID_BASE + 2)
#define CID_TIMEOUT_IMAGE_IO (V4L2LOOPBACK_CID_BASE + 3)

static int v4l2loopback_s_ctrl(struct v4l2_ctrl *ctrl);
static const struct v4l2_ctrl_ops v4l2loopback_ctrl_ops = {
	.s_ctrl = v4l2loopback_s_ctrl,
};
static const struct v4l2_ctrl_config v4l2loopback_ctrl_keepformat = {
	// clang-format off
	.ops	= &v4l2loopback_ctrl_ops,
	.id	= CID_KEEP_FORMAT,
	.name	= "keep_format",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 0,
	// clang-format on
};
static const struct v4l2_ctrl_config v4l2loopback_ctrl_sustainframerate = {
	// clang-format off
	.ops	= &v4l2loopback_ctrl_ops,
	.id	= CID_SUSTAIN_FRAMERATE,
	.name	= "sustain_framerate",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 0,
	// clang-format on
};
static const struct v4l2_ctrl_config v4l2loopback_ctrl_timeout = {
	// clang-format off
	.ops	= &v4l2loopback_ctrl_ops,
	.id	= CID_TIMEOUT,
	.name	= "timeout",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= MAX_TIMEOUT,
	.step	= 1,
	.def	= 0,
	// clang-format on
};
static const struct v4l2_ctrl_config v4l2loopback_ctrl_timeoutimageio = {
	// clang-format off
	.ops	= &v4l2loopback_ctrl_ops,
	.id	= CID_TIMEOUT_IMAGE_IO,
	.name	= "timeout_image_io",
	.type	= V4L2_CTRL_TYPE_BOOLEAN,
	.min	= 0,
	.max	= 1,
	.step	= 1,
	.def	= 0,
	// clang-format on
};

/* module structures */

/* TODO(vasaka) use typenames which are common to kernel, but first find out if
 * it is needed */
/* struct keeping state and settings of loopback device */

struct v4l2l_buffer {
	struct v4l2_buffer buffer;
	struct list_head list_head;
	int use_count;
};

struct v4l2_loopback_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb2_v4l2_buf;

	struct list_head list;
};
#define to_v4l2_loopback_buffer(buf)                                           \
	container_of(buf, struct v4l2_loopback_buffer, vb2_v4l2_buf)

struct v4l2_loopback_device {
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_loopback_entity {
		struct video_device vdev;
		struct vb2_queue vidq;
		struct list_head active_bufs; /* buffers in DQBUF order */
		struct mutex lock;
		int streaming : 1;
	} capture, output;
	/* pixel and stream format */
	struct v4l2_pix_format pix_format;
	struct v4l2_captureparm capture_param;
	unsigned long frame_jiffies;

	/* ctrls */
	int keep_format; /* CID_KEEP_FORMAT; stay ready_for_capture even when all
			    openers close() the device */
	int sustain_framerate; /* CID_SUSTAIN_FRAMERATE; duplicate frames to maintain
				  (close to) nominal framerate */

	/* buffers stuff */
	u8 *image; /* pointer to actual buffers data */
	unsigned long int imagesize; /* size of buffers data */
	int buffers_number; /* should not be big, 4 is a good choice */
	struct v4l2l_buffer buffers[MAX_BUFFERS]; /* inner driver buffers */
	int used_buffers; /* number of the actually used buffers */
	int max_openers; /* how many times can this device be opened */

	int write_position; /* number of last written frame + 1 */
	struct list_head outbufs_list; /* buffers in output DQBUF order */
	int bufpos2index
		[MAX_BUFFERS]; /* mapping of (read/write_position % used_buffers)
                        * to inner buffer index */
	long buffer_size;

	/* sustain_framerate stuff */
	struct timer_list sustain_timer;
	unsigned int reread_count;

	/* timeout stuff */
	unsigned long timeout_jiffies; /* CID_TIMEOUT; 0 means disabled */
	int timeout_image_io; /* CID_TIMEOUT_IMAGE_IO; next opener will
			       * read/write to timeout_image */
	u8 *timeout_image; /* copy of it will be captured when timeout passes */
	struct v4l2l_buffer timeout_image_buffer;
	struct timer_list timeout_timer;
	int timeout_happened;

	/* sync stuff */
	atomic_t open_count;

	int ready_for_capture; /* set to the number of writers that opened the
                                * device and negotiated format. */
	int ready_for_output; /* set to true when no writer is currently attached
			       * this differs slightly from !ready_for_capture,
			       * e.g. when using fallback images */

	int max_width;
	int max_height;

	char card_label[32];

	wait_queue_head_t read_event;
	spinlock_t lock;
};

#define cd_to_loopdev(ptr) video_get_drvdata(to_video_device((ptr)))
#define file_to_loopdev(ptr) video_get_drvdata(video_devdata((ptr)))
#define is_output(dev, vdev) (((vdev) == &(dev)->output.vdev) ? 1 : 0)

/* types of opener shows what opener wants to do with loopback */
enum opener_type {
	// clang-format off
	UNNEGOTIATED	= 0,
	READER		= 1,
	WRITER		= 2,
	// clang-format on
};

/* struct keeping state and type of opener */
struct v4l2_loopback_opener {
	enum opener_type type;
	int read_position; /* number of last processed frame + 1 or
			    * write_position - 1 if reader went out of sync */
	unsigned int reread_count;
	struct v4l2_buffer *buffers;
	int buffers_number; /* should not be big, 4 is a good choice */
	int timeout_image_io;

	struct v4l2_fh fh;
};

#define fh_to_opener(ptr) container_of((ptr), struct v4l2_loopback_opener, fh)

/* this is heavily inspired by the bttv driver found in the linux kernel */
struct v4l2l_format {
	char *name;
	int fourcc; /* video4linux 2 */
	int depth; /* bit/pixel */
	int flags;
};
/* set the v4l2l_format.flags to PLANAR for non-packed formats */
#define FORMAT_FLAGS_PLANAR 0x01
#define FORMAT_FLAGS_COMPRESSED 0x02

#include "v4l2loopback_formats.h"

static const unsigned int FORMATS = ARRAY_SIZE(formats);

static char *fourcc2str(unsigned int fourcc, char buf[4])
{
	buf[0] = (fourcc >> 0) & 0xFF;
	buf[1] = (fourcc >> 8) & 0xFF;
	buf[2] = (fourcc >> 16) & 0xFF;
	buf[3] = (fourcc >> 24) & 0xFF;

	return buf;
}

static const struct v4l2l_format *format_by_fourcc(int fourcc)
{
	unsigned int i;

	for (i = 0; i < FORMATS; i++) {
		if (formats[i].fourcc == fourcc)
			return formats + i;
	}

	dprintk("unsupported format '%c%c%c%c'\n", (fourcc >> 0) & 0xFF,
		(fourcc >> 8) & 0xFF, (fourcc >> 16) & 0xFF,
		(fourcc >> 24) & 0xFF);
	return NULL;
}

static void pix_format_set_size(struct v4l2_pix_format *f,
				const struct v4l2l_format *fmt,
				unsigned int width, unsigned int height)
{
	f->width = width;
	f->height = height;

	if (fmt->flags & FORMAT_FLAGS_PLANAR) {
		f->bytesperline = width; /* Y plane */
		f->sizeimage = (width * height * fmt->depth) >> 3;
	} else if (fmt->flags & FORMAT_FLAGS_COMPRESSED) {
		/* doesn't make sense for compressed formats */
		f->bytesperline = 0;
		f->sizeimage = (width * height * fmt->depth) >> 3;
	} else {
		f->bytesperline = (width * fmt->depth) >> 3;
		f->sizeimage = height * f->bytesperline;
	}
}

static int set_timeperframe(struct v4l2_loopback_device *dev,
			    struct v4l2_fract *tpf)
{
	if ((tpf->denominator < 1) || (tpf->numerator < 1)) {
		return -EINVAL;
	}
	dev->capture_param.timeperframe = *tpf;
	dev->frame_jiffies = max(1UL, msecs_to_jiffies(1000) * tpf->numerator /
					      tpf->denominator);
	return 0;
}

/* device attributes */
/* available via sysfs: /sys/devices/virtual/video4linux/video* */

static ssize_t attr_show_format(struct device *cd,
				struct device_attribute *attr, char *buf)
{
	/* gets the current format as "FOURCC:WxH@f/s", e.g. "YUYV:320x240@1000/30" */
	struct v4l2_loopback_device *dev = cd_to_loopdev(cd);
	const struct v4l2_fract *tpf;
	char buf4cc[5], buf_fps[32];

	if (!dev || !dev->ready_for_capture)
		return 0;
	tpf = &dev->capture_param.timeperframe;

	fourcc2str(dev->pix_format.pixelformat, buf4cc);
	buf4cc[4] = 0;
	if (tpf->numerator == 1)
		snprintf(buf_fps, sizeof(buf_fps), "%d", tpf->denominator);
	else
		snprintf(buf_fps, sizeof(buf_fps), "%d/%d", tpf->denominator,
			 tpf->numerator);
	return sprintf(buf, "%4s:%dx%d@%s\n", buf4cc, dev->pix_format.width,
		       dev->pix_format.height, buf_fps);
}

static ssize_t attr_store_format(struct device *cd,
				 struct device_attribute *attr, const char *buf,
				 size_t len)
{
	struct v4l2_loopback_device *dev = cd_to_loopdev(cd);
	int fps_num = 0, fps_den = 1;

	/* only fps changing is supported */
	if (sscanf(buf, "@%d/%d", &fps_num, &fps_den) > 0) {
		struct v4l2_fract f = { .numerator = fps_den,
					.denominator = fps_num };
		int err = 0;
		if ((err = set_timeperframe(dev, &f)) < 0)
			return err;
		return len;
	}
	return -EINVAL;
}

static DEVICE_ATTR(format, S_IRUGO | S_IWUSR, attr_show_format,
		   attr_store_format);

static ssize_t attr_show_buffers(struct device *cd,
				 struct device_attribute *attr, char *buf)
{
	struct v4l2_loopback_device *dev = cd_to_loopdev(cd);

	return sprintf(buf, "%d\n", dev->used_buffers);
}

static DEVICE_ATTR(buffers, S_IRUGO, attr_show_buffers, NULL);

static ssize_t attr_show_maxopeners(struct device *cd,
				    struct device_attribute *attr, char *buf)
{
	struct v4l2_loopback_device *dev = cd_to_loopdev(cd);

	return sprintf(buf, "%d\n", dev->max_openers);
}

static ssize_t attr_store_maxopeners(struct device *cd,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct v4l2_loopback_device *dev = NULL;
	unsigned long curr = 0;

	if (kstrtoul(buf, 0, &curr))
		return -EINVAL;

	dev = cd_to_loopdev(cd);

	if (dev->max_openers == curr)
		return len;

	if (dev->open_count.counter > curr) {
		/* request to limit to less openers as are currently attached to us */
		return -EINVAL;
	}

	dev->max_openers = (int)curr;

	return len;
}

static DEVICE_ATTR(max_openers, S_IRUGO | S_IWUSR, attr_show_maxopeners,
		   attr_store_maxopeners);

static void v4l2loopback_remove_sysfs(struct video_device *vdev)
{
#define V4L2_SYSFS_DESTROY(x) device_remove_file(&vdev->dev, &dev_attr_##x)

	if (vdev) {
		V4L2_SYSFS_DESTROY(format);
		V4L2_SYSFS_DESTROY(buffers);
		V4L2_SYSFS_DESTROY(max_openers);
		/* ... */
	}
}

static void v4l2loopback_create_sysfs(struct video_device *vdev)
{
	int res = 0;

#define V4L2_SYSFS_CREATE(x)                                                   \
	res = device_create_file(&vdev->dev, &dev_attr_##x);                   \
	if (res < 0)                                                           \
	break
	if (!vdev)
		return;
	do {
		V4L2_SYSFS_CREATE(format);
		V4L2_SYSFS_CREATE(buffers);
		V4L2_SYSFS_CREATE(max_openers);
		/* ... */
	} while (0);

	if (res >= 0)
		return;
	dev_err(&vdev->dev, "%s error: %d\n", __func__, res);
}

/* global module data */
/* find a device based on it's device-number (e.g. '3' for /dev/video3) */
struct v4l2loopback_lookup_cb_data {
	int device_nr;
	struct v4l2_loopback_device *device;
};
static int v4l2loopback_lookup_cb(int id, void *ptr, void *data)
{
	struct v4l2_loopback_device *device = ptr;
	struct v4l2loopback_lookup_cb_data *cbdata = data;
	if (cbdata && device) {
		if (device->output.vdev.num == cbdata->device_nr ||
		    device->capture.vdev.num == cbdata->device_nr) {
			cbdata->device = device;
			return 1;
		}
	}
	return 0;
}
static struct v4l2_loopback_device *v4l2loopback_lookup(int device_nr)
{
	struct v4l2loopback_lookup_cb_data data = {
		.device_nr = device_nr,
		.device = NULL,
	};
	int err = idr_for_each(&v4l2loopback_index_idr, &v4l2loopback_lookup_cb,
			       &data);
	return 1 == err ? data.device : NULL;
}

/* forward declarations */
static void init_buffers(struct v4l2_loopback_device *dev);
static int allocate_buffers(struct v4l2_loopback_device *dev);
static int free_buffers(struct v4l2_loopback_device *dev);
static void try_free_buffers(struct v4l2_loopback_device *dev);
static int allocate_timeout_image(struct v4l2_loopback_device *dev);
static void check_timers(struct v4l2_loopback_device *dev);
static const struct v4l2_file_operations output_fops;
static const struct v4l2_ioctl_ops output_ioctl_ops;
static const struct v4l2_file_operations capture_fops;
static const struct v4l2_ioctl_ops capture_ioctl_ops;

/* Queue helpers */
/* next functions sets buffer flags and adjusts counters accordingly */
static inline void set_done(struct v4l2l_buffer *buffer)
{
	buffer->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
	buffer->buffer.flags |= V4L2_BUF_FLAG_DONE;
}

static inline void set_queued(struct v4l2l_buffer *buffer)
{
	buffer->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
	buffer->buffer.flags |= V4L2_BUF_FLAG_QUEUED;
}

static inline void unset_flags(struct v4l2l_buffer *buffer)
{
	buffer->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
	buffer->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
}

/* V4L2 ioctl caps and params calls */
/* returns device capabilities
 * called on VIDIOC_QUERYCAP
 */
static int vidioc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	int labellen = (sizeof(cap->card) < sizeof(dev->card_label)) ?
				     sizeof(cap->card) :
				     sizeof(dev->card_label);

	strlcpy(cap->driver, "v4l2 loopback", sizeof(cap->driver));
	snprintf(cap->card, labellen, dev->card_label);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:v4l2loopback-%03d", dev->capture.vdev.num);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	cap->capabilities = dev->capture.vdev.device_caps |
			    dev->output.vdev.device_caps | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = vdev->device_caps;
#else
	cap->capabilities = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
			    V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
			    V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING |
			   (is_output(dev, vdev) ? V4L2_CAP_VIDEO_OUTPUT :
							 V4L2_CAP_VIDEO_CAPTURE);
#endif /* >=linux-4.7.0 */

	memset(cap->reserved, 0, sizeof(cap->reserved));
	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *argp)
{
	struct v4l2_loopback_device *dev;

	/* there can be only one... */
	if (argp->index)
		return -EINVAL;

	dev = file_to_loopdev(file);

	if (NULL == format_by_fourcc(argp->pixel_format))
		return -EINVAL;

	argp->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;

	argp->stepwise.min_width = V4L2LOOPBACK_SIZE_MIN_WIDTH;
	argp->stepwise.min_height = V4L2LOOPBACK_SIZE_MIN_HEIGHT;

	argp->stepwise.max_width = dev->max_width;
	argp->stepwise.max_height = dev->max_height;

	argp->stepwise.step_width = 1;
	argp->stepwise.step_height = 1;

	return 0;
}

/* returns frameinterval (fps) for the set resolution
 * called on VIDIOC_ENUM_FRAMEINTERVALS
 */
static int vidioc_enum_frameintervals(struct file *file, void *fh,
				      struct v4l2_frmivalenum *argp)
{
	struct v4l2_loopback_device *dev = file_to_loopdev(file);

	/* there can be only one... */
	if (argp->index)
		return -EINVAL;

	if (argp->width < V4L2LOOPBACK_SIZE_MIN_WIDTH ||
	    argp->width > dev->max_width ||
	    argp->height < V4L2LOOPBACK_SIZE_MIN_HEIGHT ||
	    argp->height > dev->max_height ||
	    NULL == format_by_fourcc(argp->pixel_format))
		return -EINVAL;

	argp->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	argp->stepwise.min.numerator = 1;
	argp->stepwise.min.denominator = V4L2LOOPBACK_FPS_MAX;
	argp->stepwise.max.numerator = 1;
	argp->stepwise.max.denominator = V4L2LOOPBACK_FPS_MIN;
	argp->stepwise.step.numerator = 1;
	argp->stepwise.step.denominator = 1;

	return 0;
}

/* ------------------ CAPTURE ----------------------- */

/* returns device formats
 * called on VIDIOC_ENUM_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int vidioc_enum_fmt_cap(struct file *file, void *fh,
			       struct v4l2_fmtdesc *f)
{
	struct v4l2_loopback_device *dev;
	MARK();

	dev = file_to_loopdev(file);

	if (f->index)
		return -EINVAL;
	if (dev->ready_for_capture) {
		const __u32 format = dev->pix_format.pixelformat;

		snprintf(f->description, sizeof(f->description), "[%c%c%c%c]",
			 (format >> 0) & 0xFF, (format >> 8) & 0xFF,
			 (format >> 16) & 0xFF, (format >> 24) & 0xFF);

		f->pixelformat = dev->pix_format.pixelformat;
	} else {
		return -EINVAL;
	}
	f->flags = 0;
	MARK();
	return 0;
}

/* returns current video format format fmt
 * called on VIDIOC_G_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int vidioc_g_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct v4l2_loopback_device *dev;
	MARK();

	dev = file_to_loopdev(file);

	if (!dev->ready_for_capture)
		return -EINVAL;

	fmt->fmt.pix = dev->pix_format;
	MARK();
	return 0;
}

/* checks if it is OK to change to format fmt;
 * actual check is done by inner_try_fmt_cap
 * just checking that pixelformat is OK and set other parameters, app should
 * obey this decision
 * called on VIDIOC_TRY_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int vidioc_try_fmt_cap(struct file *file, void *priv,
			      struct v4l2_format *fmt)
{
	struct v4l2_loopback_device *dev;
	char buf[5];

	dev = file_to_loopdev(file);

	if (0 == dev->ready_for_capture) {
		dprintk("setting fmt_cap not possible yet\n");
		return -EBUSY;
	}

	if (fmt->fmt.pix.pixelformat != dev->pix_format.pixelformat)
		return -EINVAL;

	fmt->fmt.pix = dev->pix_format;

	buf[4] = 0;
	dprintk("capFOURCC=%s\n", fourcc2str(dev->pix_format.pixelformat, buf));
	return 0;
}

/* sets new output format, if possible
 * actually format is set  by input and we even do not check it, just return
 * current one, but it is possible to set subregions of input TODO(vasaka)
 * called on VIDIOC_S_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int vidioc_s_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	return vidioc_try_fmt_cap(file, priv, fmt);
}

/* ------------------ OUTPUT ----------------------- */

/* returns device formats;
 * LATER: allow all formats
 * called on VIDIOC_ENUM_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int vidioc_enum_fmt_out(struct file *file, void *fh,
			       struct v4l2_fmtdesc *f)
{
	const struct v4l2l_format *fmt;

	if (f->index < 0 || f->index >= FORMATS)
		return -EINVAL;

	fmt = &formats[f->index];
	f->pixelformat = fmt->fourcc;
	snprintf(f->description, sizeof(f->description), "%s", fmt->name);
	f->flags = 0;

	return 0;
}

/* returns current video format format fmt */
/* NOTE: this is called from the producer
 * so if format has not been negotiated yet,
 * it should return ALL of available formats,
 * called on VIDIOC_G_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int vidioc_g_fmt_out(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct v4l2_loopback_device *dev;
	MARK();

	dev = file_to_loopdev(file);

	/*
	 * LATER: this should return the currently valid format
	 * gstreamer doesn't like it, if this returns -EINVAL, as it
	 * then concludes that there is _no_ valid format
	 * CHECK whether this assumption is wrong,
	 * or whether we have to always provide a valid format
	 */

	fmt->fmt.pix = dev->pix_format;
	return 0;
}

/* checks if it is OK to change to format fmt;
 * if format is negotiated do not change it
 * called on VIDIOC_TRY_FMT with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int vidioc_try_fmt_out(struct file *file, void *priv,
			      struct v4l2_format *fmt)
{
	struct v4l2_loopback_device *dev = file_to_loopdev(file);
	__u32 w = fmt->fmt.pix.width;
	__u32 h = fmt->fmt.pix.height;
	__u32 pixfmt = fmt->fmt.pix.pixelformat;
	const struct v4l2l_format *format;
	MARK();

	w = w ? clamp_val(w, V4L2LOOPBACK_SIZE_MIN_WIDTH, dev->max_width) :
		      V4L2LOOPBACK_SIZE_DEFAULT_WIDTH;
	h = h ? clamp_val(h, V4L2LOOPBACK_SIZE_MIN_HEIGHT, dev->max_height) :
		      V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT;
	dprintk("trying image %dx%d\n", w, h);

	format = format_by_fourcc(pixfmt);
	if (NULL == format)
		format = &formats[0];

	pix_format_set_size(&fmt->fmt.pix, format, w, h);

	fmt->fmt.pix.pixelformat = format->fourcc;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

	if (V4L2_FIELD_ANY == fmt->fmt.pix.field)
		fmt->fmt.pix.field = V4L2_FIELD_NONE;

	return 0;
}

/* sets new output format, if possible;
 * allocate data here because we do not know if it will be streaming or
 * read/write IO
 * called on VIDIOC_S_FMT with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int vidioc_s_fmt_out(struct file *file, void *priv,
			    struct v4l2_format *fmt)
{
	struct v4l2_loopback_device *dev = file_to_loopdev(file);
	char buf[5];
	int ret;
	MARK();

	ret = vidioc_try_fmt_out(file, priv, fmt);

	dprintk("s_fmt_out(%d) %d...%d\n", ret, dev->ready_for_capture,
		fmt->fmt.pix.sizeimage);

	buf[4] = 0;
	dprintk("outFOURCC=%s\n", fourcc2str(fmt->fmt.pix.pixelformat, buf));

	if (ret < 0)
		return ret;

	dev->pix_format = fmt->fmt.pix;
	if (!dev->ready_for_capture) {
		dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
		fmt->fmt.pix.sizeimage = dev->buffer_size;
		allocate_buffers(dev);
	}
	return ret;
}

// #define V4L2L_OVERLAY
#ifdef V4L2L_OVERLAY
/* ------------------ OVERLAY ----------------------- */
/* currently unsupported */
/* GSTreamer's v4l2sink is buggy, as it requires the overlay to work
 * while it should only require it, if overlay is requested
 * once the gstreamer element is fixed, remove the overlay dummies
 */
#warning OVERLAY dummies
static int vidioc_g_fmt_overlay(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	return 0;
}

static int vidioc_s_fmt_overlay(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	return 0;
}
#endif /* V4L2L_OVERLAY */

/* ------------------ PARAMs ----------------------- */

/* get some data flow parameters, only capability, fps and readbuffers has
 * effect on this driver
 * called on VIDIOC_G_PARM
 */
static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	/* do not care about type of opener, hope these enums would always be
	 * compatible */
	struct v4l2_loopback_device *dev;
	MARK();

	dev = file_to_loopdev(file);
	parm->parm.capture = dev->capture_param;
	return 0;
}

/* get some data flow parameters, only capability, fps and readbuffers has
 * effect on this driver
 * called on VIDIOC_S_PARM
 */
static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	struct v4l2_loopback_device *dev;
	int err = 0;
	MARK();

	dev = file_to_loopdev(file);
	dprintk("vidioc_s_parm called frate=%d/%d\n",
		parm->parm.capture.timeperframe.numerator,
		parm->parm.capture.timeperframe.denominator);

	switch (parm->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if ((err = set_timeperframe(
			     dev, &parm->parm.capture.timeperframe)) < 0)
			return err;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if ((err = set_timeperframe(
			     dev, &parm->parm.capture.timeperframe)) < 0)
			return err;
		break;
	default:
		return -1;
	}

	parm->parm.capture = dev->capture_param;
	return 0;
}

#ifdef V4L2LOOPBACK_WITH_STD
/* sets a tv standard, actually we do not need to handle this any special way
 * added to support effecttv
 * called on VIDIOC_S_STD
 */
static int vidioc_s_std(struct file *file, void *fh, v4l2_std_id *_std)
{
	v4l2_std_id req_std = 0, supported_std = 0;
	const v4l2_std_id all_std = V4L2_STD_ALL, no_std = 0;

	if (_std) {
		req_std = *_std;
		*_std = all_std;
	}

	/* we support everything in V4L2_STD_ALL, but not more... */
	supported_std = (all_std & req_std);
	if (no_std == supported_std)
		return -EINVAL;

	return 0;
}

/* gets a fake video standard
 * called on VIDIOC_G_STD
 */
static int vidioc_g_std(struct file *file, void *fh, v4l2_std_id *norm)
{
	if (norm)
		*norm = V4L2_STD_ALL;
	return 0;
}
/* gets a fake video standard
 * called on VIDIOC_QUERYSTD
 */
static int vidioc_querystd(struct file *file, void *fh, v4l2_std_id *norm)
{
	if (norm)
		*norm = V4L2_STD_ALL;
	return 0;
}
#endif /* V4L2LOOPBACK_WITH_STD */

static int v4l2loopback_set_ctrl(struct v4l2_loopback_device *dev, u32 id,
				 s64 val)
{
	switch (id) {
	case CID_KEEP_FORMAT:
		if (val < 0 || val > 1)
			return -EINVAL;
		dev->keep_format = val;
		try_free_buffers(
			dev); /* will only free buffers if !keep_format */
		break;
	case CID_SUSTAIN_FRAMERATE:
		if (val < 0 || val > 1)
			return -EINVAL;
		spin_lock_bh(&dev->lock);
		dev->sustain_framerate = val;
		check_timers(dev);
		spin_unlock_bh(&dev->lock);
		break;
	case CID_TIMEOUT:
		if (val < 0 || val > MAX_TIMEOUT)
			return -EINVAL;
		spin_lock_bh(&dev->lock);
		dev->timeout_jiffies = msecs_to_jiffies(val);
		check_timers(dev);
		spin_unlock_bh(&dev->lock);
		allocate_timeout_image(dev);
		break;
	case CID_TIMEOUT_IMAGE_IO:
		if (val < 0 || val > 1)
			return -EINVAL;
		dev->timeout_image_io = val;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int v4l2loopback_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_loopback_device *dev = container_of(
		ctrl->handler, struct v4l2_loopback_device, ctrl_handler);
	return v4l2loopback_set_ctrl(dev, ctrl->id, ctrl->val);
}

/* returns set of device outputs, in our case there is only one
 * called on VIDIOC_ENUMOUTPUT
 */
static int vidioc_enum_output(struct file *file, void *fh,
			      struct v4l2_output *outp)
{
	__u32 index = outp->index;
	MARK();

	if (0 != index)
		return -EINVAL;

	/* clear all data (including the reserved fields) */
	memset(outp, 0, sizeof(*outp));

	outp->index = index;
	strlcpy(outp->name, "loopback in", sizeof(outp->name));
	outp->type = V4L2_OUTPUT_TYPE_ANALOG;
	outp->audioset = 0;
	outp->modulator = 0;
#ifdef V4L2LOOPBACK_WITH_STD
	outp->std = V4L2_STD_ALL;
#ifdef V4L2_OUT_CAP_STD
	outp->capabilities |= V4L2_OUT_CAP_STD;
#endif /*  V4L2_OUT_CAP_STD */
#endif /* V4L2LOOPBACK_WITH_STD */

	return 0;
}

/* which output is currently active,
 * called on VIDIOC_G_OUTPUT
 */
static int vidioc_g_output(struct file *file, void *fh, unsigned int *i)
{
	if (i)
		*i = 0;
	return 0;
}

/* set output, can make sense if we have more than one video src,
 * called on VIDIOC_S_OUTPUT
 */
static int vidioc_s_output(struct file *file, void *fh, unsigned int i)
{
	if (i)
		return -EINVAL;

	return 0;
}

/* returns set of device inputs, in our case there is only one,
 * but later I may add more
 * called on VIDIOC_ENUMINPUT
 */
static int vidioc_enum_input(struct file *file, void *fh,
			     struct v4l2_input *inp)
{
	__u32 index = inp->index;
	MARK();

	if (0 != index)
		return -EINVAL;

	/* clear all data (including the reserved fields) */
	memset(inp, 0, sizeof(*inp));

	inp->index = index;
	strlcpy(inp->name, "loopback", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->audioset = 0;
	inp->tuner = 0;
	inp->status = 0;

#ifdef V4L2LOOPBACK_WITH_STD
	inp->std = V4L2_STD_ALL;
#ifdef V4L2_IN_CAP_STD
	inp->capabilities |= V4L2_IN_CAP_STD;
#endif
#endif /* V4L2LOOPBACK_WITH_STD */

	return 0;
}

/* which input is currently active,
 * called on VIDIOC_G_INPUT
 */
static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct v4l2_loopback_device *dev = file_to_loopdev(file);
	if (!dev->ready_for_capture)
		return -ENOTTY;
	if (i)
		*i = 0;
	return 0;
}

/* set input, can make sense if we have more than one video src,
 * called on VIDIOC_S_INPUT
 */
static int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	struct v4l2_loopback_device *dev = file_to_loopdev(file);
	if (!dev->ready_for_capture)
		return -ENOTTY;
	if (i == 0)
		return 0;
	return -EINVAL;
}

static int vidioc_subscribe_event(struct v4l2_fh *fh,
				  const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}

	return -EINVAL;
}

/* init functions */
/* frees buffers, if already allocated */
static int free_buffers(struct v4l2_loopback_device *dev)
{
	MARK();
	dprintk("freeing image@%p for dev:%p\n", dev ? dev->image : NULL, dev);
	if (dev->image) {
		vfree(dev->image);
		dev->image = NULL;
	}
	if (dev->timeout_image) {
		vfree(dev->timeout_image);
		dev->timeout_image = NULL;
	}
	dev->imagesize = 0;

	return 0;
}
/* frees buffers, if they are no longer needed */
static void try_free_buffers(struct v4l2_loopback_device *dev)
{
	MARK();
	if (0 == dev->open_count.counter && !dev->keep_format) {
		free_buffers(dev);
		dev->ready_for_capture = 0;
		dev->buffer_size = 0;
		dev->write_position = 0;
	}
}
/* allocates buffers, if buffer_size is set */
static int allocate_buffers(struct v4l2_loopback_device *dev)
{
	MARK();
	/* vfree on close file operation in case no open handles left */
	if (0 == dev->buffer_size)
		return -EINVAL;

	if (dev->image) {
		dprintk("allocating buffers again: %ld %ld\n",
			dev->buffer_size * dev->buffers_number, dev->imagesize);
		/* FIXME: prevent double allocation more intelligently! */
		if (dev->buffer_size * dev->buffers_number == dev->imagesize)
			return 0;

		/* if there is only one writer, no problem should occur */
		if (dev->open_count.counter == 1)
			free_buffers(dev);
		else
			return -EINVAL;
	}

	dev->imagesize = dev->buffer_size * dev->buffers_number;

	dprintk("allocating %ld = %ldx%d\n", dev->imagesize, dev->buffer_size,
		dev->buffers_number);

	dev->image = vmalloc(dev->imagesize);
	if (dev->timeout_jiffies > 0)
		allocate_timeout_image(dev);

	if (dev->image == NULL)
		return -ENOMEM;
	dprintk("vmallocated %ld bytes\n", dev->imagesize);
	MARK();
	init_buffers(dev);
	return 0;
}

/* init inner buffers, they are capture mode and flags are set as
 * for capture mod buffers */
static void init_buffers(struct v4l2_loopback_device *dev)
{
	int i;
	int buffer_size;
	int bytesused;
	MARK();

	buffer_size = dev->buffer_size;
	bytesused = dev->pix_format.sizeimage;

	for (i = 0; i < dev->buffers_number; ++i) {
		struct v4l2_buffer *b = &dev->buffers[i].buffer;
		b->index = i;
		b->bytesused = bytesused;
		b->length = buffer_size;
		b->field = V4L2_FIELD_NONE;
		b->flags = 0;
		b->m.offset = i * buffer_size;
		b->memory = V4L2_MEMORY_MMAP;
		b->sequence = 0;
		b->timestamp.tv_sec = 0;
		b->timestamp.tv_usec = 0;
		b->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		v4l2l_get_timestamp(b);
	}
	dev->timeout_image_buffer = dev->buffers[0];
	dev->timeout_image_buffer.buffer.m.offset = MAX_BUFFERS * buffer_size;
	MARK();
}

static int allocate_timeout_image(struct v4l2_loopback_device *dev)
{
	MARK();
	if (dev->buffer_size <= 0)
		return -EINVAL;

	if (dev->timeout_image == NULL) {
		dev->timeout_image = vzalloc(dev->buffer_size);
		if (dev->timeout_image == NULL)
			return -ENOMEM;
	}
	return 0;
}

static int qops_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			    unsigned int *num_planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct video_device *vdev = vb2_get_drv_priv(q);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	unsigned int size;

	size = dev->buffer_size;
	if (*num_buffers > max_buffers)
		*num_buffers = max_buffers;

	/* When called with plane sizes, validate them. v4l2loopback supports
	 * single planar formats only, and requires buffers to be large enough
	 * to store a complete frame.
	 */
	if (*num_planes)
		return *num_planes != 1 || sizes[0] < size ? -EINVAL : 0;

	*num_planes = 1;
	sizes[0] = size;
	return 0;
}

static int qops_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vb_v4l2 = to_vb2_v4l2_buffer(vb);
	struct v4l2_loopback_buffer *buf = to_v4l2_loopback_buffer(vb_v4l2);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int qops_buf_prepare(struct vb2_buffer *vb)
{
	struct video_device *vdev = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	unsigned long size;

	size = dev->buffer_size;
	if (vb2_plane_size(vb, 0) < size) {
		v4l2_err(&dev->v4l2_dev,
			 "%s data will not fit into plane (%lu < %lu)\n",
			 __func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	return 0;
}

static void qops_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vb_v4l2 = to_vb2_v4l2_buffer(vb);
	struct v4l2_loopback_buffer *buf = to_v4l2_loopback_buffer(vb_v4l2);

	list_del(&buf->list);
}

static void output_qops_buf_queue(struct vb2_buffer *vb)
{
	struct video_device *vdev = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	struct vb2_v4l2_buffer *vb_v4l2 = to_vb2_v4l2_buffer(vb);
	struct v4l2_loopback_buffer *buf = to_v4l2_loopback_buffer(vb_v4l2);

	list_add_tail(&buf->list, &dev->output.active_bufs);

	if (!dev->output.streaming)
		return;
	/* TODO: wake readers */
}

static int output_qops_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct video_device *vdev = vb2_get_drv_priv(q);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);

	dev->output.streaming = 1;
	/* TODO: wake readers */

	return 0;
}

static void output_qops_stop_streaming(struct vb2_queue *q)
{
	struct video_device *vdev = vb2_get_drv_priv(q);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	struct v4l2_loopback_buffer *buf;
	struct list_head *pos, *n;

	list_for_each_safe (pos, n, &dev->output.active_bufs) {
		buf = list_entry(pos, struct v4l2_loopback_buffer, list);
		if (buf->vb2_v4l2_buf.vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(&buf->vb2_v4l2_buf.vb2_buf,
					VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops output_qops = {
	// clang-format off
	.queue_setup            = qops_queue_setup,
	.buf_init               = qops_buf_init,
	.buf_prepare            = qops_buf_prepare,
	.buf_finish             = qops_buf_finish,
	.buf_queue              = output_qops_buf_queue,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
	.start_streaming        = output_qops_start_streaming,
	.stop_streaming         = output_qops_stop_streaming,
	// clang-format on
};

static void capture_qops_buf_queue(struct vb2_buffer *vb)
{
	struct video_device *vdev = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	struct vb2_v4l2_buffer *vb_v4l2 = to_vb2_v4l2_buffer(vb);
	struct v4l2_loopback_buffer *buf = to_v4l2_loopback_buffer(vb_v4l2);

	list_add_tail(&buf->list, &dev->capture.active_bufs);

	if (!dev->capture.streaming)
		return;
	/* TODO: wake readers */
}

static int capture_qops_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct video_device *vdev = vb2_get_drv_priv(q);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);

	dev->capture.streaming = 1;
	/* TODO: wake readers */

	return 0;
}

static void capture_qops_stop_streaming(struct vb2_queue *q)
{
	struct video_device *vdev = vb2_get_drv_priv(q);
	struct v4l2_loopback_device *dev = video_get_drvdata(vdev);
	struct v4l2_loopback_buffer *buf;
	struct list_head *pos, *n;

	list_for_each_safe (pos, n, &dev->capture.active_bufs) {
		buf = list_entry(pos, struct v4l2_loopback_buffer, list);
		if (buf->vb2_v4l2_buf.vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			vb2_buffer_done(&buf->vb2_v4l2_buf.vb2_buf,
					VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops capture_qops = {
	// clang-format off
	.queue_setup            = qops_queue_setup,
	.buf_init               = qops_buf_init,
	.buf_prepare            = qops_buf_prepare,
	.buf_finish             = qops_buf_finish,
	.buf_queue              = capture_qops_buf_queue,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
	.start_streaming        = capture_qops_start_streaming,
	.stop_streaming         = capture_qops_stop_streaming,
	// clang-format on
};

/* fills and register video device */
static int init_entity(struct v4l2_loopback_entity *entity, int nr, int type,
		       struct v4l2_loopback_device *dev)
{
	int is_output = (type == V4L2_CAP_VIDEO_OUTPUT) ? 1 : 0;
	struct video_device *vdev = &entity->vdev;
	struct vb2_queue *q = &entity->vidq;
	int err;

	snprintf(vdev->name, sizeof(vdev->name), dev->card_label);
	vdev->v4l2_dev = &dev->v4l2_dev;
	video_set_drvdata(vdev, dev);

#ifdef V4L2LOOPBACK_WITH_STD
	vdev->tvnorms = V4L2_STD_ALL;
#endif /* V4L2LOOPBACK_WITH_STD */

	vdev->vfl_type = VFL_TYPE_VIDEO;
	if (is_output) {
		vdev->vfl_dir = VFL_DIR_TX;
		vdev->fops = &output_fops;
		vdev->ioctl_ops = &output_ioctl_ops;
		q->ops = &output_qops;
	} else {
		vdev->vfl_dir = VFL_DIR_RX;
		vdev->fops = &capture_fops;
		vdev->ioctl_ops = &capture_ioctl_ops;
		q->ops = &capture_qops;
	}
	vdev->release = &video_device_release_empty;
	vdev->minor = -1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	vdev->device_caps = type | V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
#endif /* >=linux-4.7.0 */

	if (debug > 1)
		vdev->dev_debug =
			V4L2_DEV_DEBUG_IOCTL | V4L2_DEV_DEBUG_IOCTL_ARG;
	q->type = type;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF | VB2_WRITE;
	q->gfp_flags = 0;
	q->min_buffers_needed = 1;
	q->drv_priv = vdev;
	q->buf_struct_size = sizeof(struct v4l2_loopback_buffer);
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &entity->lock;

	err = vb2_queue_init(q);
	if (err < 0)
		return err;

	INIT_LIST_HEAD(&entity->active_bufs);
	vdev->lock = q->lock;
	vdev->queue = q;

	MARK();

	/* register the device -> it creates /dev/video* */
	if (video_register_device(vdev, VFL_TYPE_VIDEO, nr) < 0) {
		printk(KERN_ERR
		       "v4l2loopback: failed video_register_device()\n");
		return -EFAULT;
	}

	v4l2loopback_create_sysfs(vdev);

	return 0;
}

/* init default capture parameters, only fps may be changed in future */
static void init_capture_param(struct v4l2_captureparm *capture_param)
{
	MARK();
	capture_param->capability = 0;
	capture_param->capturemode = 0;
	capture_param->extendedmode = 0;
	capture_param->readbuffers = max_buffers;
	capture_param->timeperframe.numerator = 1;
	capture_param->timeperframe.denominator = 30;
}

static void check_timers(struct v4l2_loopback_device *dev)
{
	if (!dev->ready_for_capture)
		return;

	if (dev->timeout_jiffies > 0 && !timer_pending(&dev->timeout_timer))
		mod_timer(&dev->timeout_timer, jiffies + dev->timeout_jiffies);
	if (dev->sustain_framerate && !timer_pending(&dev->sustain_timer))
		mod_timer(&dev->sustain_timer,
			  jiffies + dev->frame_jiffies * 3 / 2);
}
#ifdef HAVE_TIMER_SETUP
static void sustain_timer_clb(struct timer_list *t)
{
	struct v4l2_loopback_device *dev = from_timer(dev, t, sustain_timer);
#else
static void sustain_timer_clb(unsigned long nr)
{
	struct v4l2_loopback_device *dev =
		idr_find(&v4l2loopback_index_idr, nr);
#endif
	spin_lock(&dev->lock);
	if (dev->sustain_framerate) {
		dev->reread_count++;
		dprintkrw("reread: %d %d\n", dev->write_position,
			  dev->reread_count);
		if (dev->reread_count == 1)
			mod_timer(&dev->sustain_timer,
				  jiffies + max(1UL, dev->frame_jiffies / 2));
		else
			mod_timer(&dev->sustain_timer,
				  jiffies + dev->frame_jiffies);
		wake_up_all(&dev->read_event);
	}
	spin_unlock(&dev->lock);
}
#ifdef HAVE_TIMER_SETUP
static void timeout_timer_clb(struct timer_list *t)
{
	struct v4l2_loopback_device *dev = from_timer(dev, t, timeout_timer);
#else
static void timeout_timer_clb(unsigned long nr)
{
	struct v4l2_loopback_device *dev =
		idr_find(&v4l2loopback_index_idr, nr);
#endif
	spin_lock(&dev->lock);
	if (dev->timeout_jiffies > 0) {
		dev->timeout_happened = 1;
		mod_timer(&dev->timeout_timer, jiffies + dev->timeout_jiffies);
		wake_up_all(&dev->read_event);
	}
	spin_unlock(&dev->lock);
}

/* init loopback main structure */
#define DEFAULT_FROM_CONF(confmember, default_condition, default_value)        \
	((conf) ?                                                              \
		       ((conf->confmember default_condition) ? (default_value) :     \
							       (conf->confmember)) : \
		       default_value)

static int v4l2_loopback_add(struct v4l2_loopback_config *conf, int *ret_nr)
{
	struct v4l2_loopback_device *dev;
	struct v4l2_ctrl_handler *hdl;

	int err = -ENOMEM;

	int _max_width = DEFAULT_FROM_CONF(
		max_width, <= V4L2LOOPBACK_SIZE_MIN_WIDTH, max_width);
	int _max_height = DEFAULT_FROM_CONF(
		max_height, <= V4L2LOOPBACK_SIZE_MIN_HEIGHT, max_height);
	int _max_buffers = DEFAULT_FROM_CONF(max_buffers, <= 0, max_buffers);
	int _max_openers = DEFAULT_FROM_CONF(max_openers, <= 0, max_openers);

	int output_nr = -1, capture_nr = -1;
	if (conf) {
		if (conf->output_nr >= 0 && conf->capture_nr >= 0 &&
		    conf->output_nr == conf->capture_nr) {
			printk(KERN_ERR
			       "split OUTPUT and CAPTURE devices not yet supported.");
			printk(KERN_INFO
			       "both devices must have the same number (%d != %d).",
			       conf->output_nr, conf->capture_nr);
			return -EINVAL;
		}

		output_nr = conf->output_nr;
		capture_nr = conf->capture_nr;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	err = idr_alloc2(&v4l2loopback_index_idr, dev, &output_nr, &capture_nr);
	if (err)
		goto out_free_dev;

	dprintk("creating v4l2loopback-device %d:%d\n", output_nr, capture_nr);

	if (conf && conf->card_label && *(conf->card_label)) {
		snprintf(dev->card_label, sizeof(dev->card_label), "%s",
			 conf->card_label);
	} else {
		snprintf(dev->card_label, sizeof(dev->card_label),
			 "Dummy video device (0x%04X)", capture_nr);
	}
	snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name),
		 "v4l2loopback-%03d", capture_nr);

	err = v4l2_device_register(NULL, &dev->v4l2_dev);
	if (err)
		goto out_free_idr;
	MARK();

	init_capture_param(&dev->capture_param);
	set_timeperframe(dev, &dev->capture_param.timeperframe);
	dev->keep_format = 0;
	dev->sustain_framerate = 0;

	dev->max_width = _max_width;
	dev->max_height = _max_height;
	dev->max_openers = _max_openers;
	dev->buffers_number = dev->used_buffers = _max_buffers;

	dev->write_position = 0;

	MARK();
	spin_lock_init(&dev->lock);
	INIT_LIST_HEAD(&dev->outbufs_list);
	if (list_empty(&dev->outbufs_list)) {
		int i;

		for (i = 0; i < dev->used_buffers; ++i)
			list_add_tail(&dev->buffers[i].list_head,
				      &dev->outbufs_list);
	}
	memset(dev->bufpos2index, 0, sizeof(dev->bufpos2index));
	atomic_set(&dev->open_count, 0);
	dev->ready_for_capture = 0;
	dev->ready_for_output = 1;

	dev->buffer_size = 0;
	dev->image = NULL;
	dev->imagesize = 0;
#ifdef HAVE_TIMER_SETUP
	timer_setup(&dev->sustain_timer, sustain_timer_clb, 0);
	timer_setup(&dev->timeout_timer, timeout_timer_clb, 0);
#else
	setup_timer(&dev->sustain_timer, sustain_timer_clb, capture_nr);
	setup_timer(&dev->timeout_timer, timeout_timer_clb, capture_nr);
#endif
	dev->reread_count = 0;
	dev->timeout_jiffies = 0;
	dev->timeout_image = NULL;
	dev->timeout_happened = 0;

	hdl = &dev->ctrl_handler;
	err = v4l2_ctrl_handler_init(hdl, 4);
	if (err)
		goto out_free_idr;
	v4l2_ctrl_new_custom(hdl, &v4l2loopback_ctrl_keepformat, NULL);
	v4l2_ctrl_new_custom(hdl, &v4l2loopback_ctrl_sustainframerate, NULL);
	v4l2_ctrl_new_custom(hdl, &v4l2loopback_ctrl_timeout, NULL);
	v4l2_ctrl_new_custom(hdl, &v4l2loopback_ctrl_timeoutimageio, NULL);
	if (hdl->error) {
		err = hdl->error;
		goto out_free_handler;
	}
	dev->v4l2_dev.ctrl_handler = hdl;

	err = v4l2_ctrl_handler_setup(hdl);

	/* FIXME set buffers to 0 */

	/* Set initial format */
	dev->pix_format.width = 0; /* V4L2LOOPBACK_SIZE_DEFAULT_WIDTH; */
	dev->pix_format.height = 0; /* V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT; */
	dev->pix_format.pixelformat = formats[0].fourcc;
	dev->pix_format.colorspace =
		V4L2_COLORSPACE_SRGB; /* do we need to set this ? */
	dev->pix_format.field = V4L2_FIELD_NONE;

	dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
	dprintk("buffer_size = %ld (=%d)\n", dev->buffer_size,
		dev->pix_format.sizeimage);
	allocate_buffers(dev);

	init_waitqueue_head(&dev->read_event);

	MARK();

	if (init_entity(&dev->output, output_nr, V4L2_CAP_VIDEO_OUTPUT, dev))
		goto out_free_handler;

	MARK();

	if (init_entity(&dev->capture, capture_nr, V4L2_CAP_VIDEO_CAPTURE, dev))
		goto out_unregister_output_vdev;

	MARK();
	if (ret_nr)
		*ret_nr = dev->capture.vdev.num;
	return 0;

out_unregister_output_vdev:
	video_unregister_device(&dev->output.vdev);
out_free_handler:
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
out_free_idr:
	idr_remove(&v4l2loopback_index_idr, output_nr);
	idr_remove(&v4l2loopback_index_idr, capture_nr);
out_free_dev:
	kfree(dev);
	return err;
}

static void v4l2_loopback_remove(struct v4l2_loopback_device *dev)
{
	struct video_device *output_vdev = &dev->output.vdev;
	struct video_device *capture_vdev = &dev->capture.vdev;

	free_buffers(dev);

	v4l2loopback_remove_sysfs(output_vdev);
	video_unregister_device(output_vdev);
	video_device_release_empty(output_vdev);

	v4l2loopback_remove_sysfs(capture_vdev);
	video_unregister_device(capture_vdev);
	video_device_release_empty(capture_vdev);

	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	kfree(dev);
}

static long v4l2loopback_control_ioctl(struct file *file, unsigned int cmd,
				       unsigned long parm)
{
	struct v4l2_loopback_device *dev, *capture_dev, *output_dev;
	struct v4l2_loopback_config conf;
	struct v4l2_loopback_config *confptr = &conf;
	int device_nr;
	int ret;

	ret = mutex_lock_killable(&v4l2loopback_ctl_mutex);
	if (ret)
		return ret;

	ret = -EINVAL;
	switch (cmd) {
	default:
		ret = -ENOSYS;
		break;
		/* add a v4l2loopback device (pair), based on the user-provided specs */
	case V4L2LOOPBACK_CTL_ADD:
		if (parm) {
			if ((ret = copy_from_user(&conf, (void *)parm,
						  sizeof(conf))) < 0)
				break;
		} else
			confptr = NULL;
		ret = v4l2_loopback_add(confptr, &device_nr);
		if (ret >= 0)
			ret = device_nr;
		break;
		/* remove a v4l2loopback device (both capture and output) */
	case V4L2LOOPBACK_CTL_REMOVE:
		dev = v4l2loopback_lookup((int)parm);
		if (dev == NULL)
			ret = -ENODEV;
		else if (dev->open_count.counter > 0)
			ret = -EBUSY;
		else {
			idr_remove(&v4l2loopback_index_idr,
				   dev->output.vdev.num);
			idr_remove(&v4l2loopback_index_idr,
				   dev->capture.vdev.num);
			v4l2_loopback_remove(dev);
			ret = 0;
		};
		break;
		/* get information for a loopback device.
                 * this is mostly about limits (which cannot be queried directly with  VIDIOC_G_FMT and friends
                 */
	case V4L2LOOPBACK_CTL_QUERY:
		if (!parm)
			break;
		if ((ret = copy_from_user(&conf, (void *)parm, sizeof(conf))) <
		    0)
			break;

		output_dev = v4l2loopback_lookup(conf.output_nr);
		capture_dev = v4l2loopback_lookup(conf.capture_nr);
		/* get the device from either capture_nr or output_nr (whatever is valid) */
		dev = output_dev ? output_dev : capture_dev;
		if (dev == NULL)
			break;
		MARK();
		/* if we got two valid device pointers, make sure they refer to
		 * the same device (or bail out)
		 */
		if (output_dev && capture_dev && output_dev != capture_dev)
			break;
		MARK();

		/* v4l2_loopback_config identified a single device, so fetch the data */
		snprintf(conf.card_label, sizeof(conf.card_label), "%s",
			 dev->card_label);
		MARK();
		conf.output_nr = dev->output.vdev.num;
		conf.capture_nr = dev->capture.vdev.num;
		conf.max_width = dev->max_width;
		conf.max_height = dev->max_height;
		conf.max_buffers = dev->buffers_number;
		conf.max_openers = dev->max_openers;
		conf.debug = debug;
		MARK();
		if (copy_to_user((void *)parm, &conf, sizeof(conf))) {
			ret = -EFAULT;
			break;
		}
		MARK();
		ret = 0;
		;
		break;
	}

	MARK();
	mutex_unlock(&v4l2loopback_ctl_mutex);
	MARK();
	return ret;
}

/* LINUX KERNEL */

static const struct file_operations v4l2loopback_ctl_fops = {
	// clang-format off
	.owner		= THIS_MODULE,
	.open		= nonseekable_open,
	.unlocked_ioctl	= v4l2loopback_control_ioctl,
	.compat_ioctl	= v4l2loopback_control_ioctl,
	.llseek		= noop_llseek,
	// clang-format on
};

static struct miscdevice v4l2loopback_misc = {
	// clang-format off
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "v4l2loopback",
	.fops		= &v4l2loopback_ctl_fops,
	// clang-format on
};

static const struct v4l2_file_operations output_fops = {
	// clang-format off
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.write		= vb2_fop_write,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
	// clang-format on
};

static const struct v4l2_ioctl_ops output_ioctl_ops = {
	// clang-format off
	.vidioc_querycap		= vidioc_querycap,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,
	.vidioc_enum_frameintervals	= vidioc_enum_frameintervals,

	.vidioc_enum_output		= vidioc_enum_output,
	.vidioc_g_output		= vidioc_g_output,
	.vidioc_s_output		= vidioc_s_output,

	.vidioc_enum_fmt_vid_out	= vidioc_enum_fmt_out,
	.vidioc_g_fmt_vid_out		= vidioc_g_fmt_out,
	.vidioc_try_fmt_vid_out		= vidioc_try_fmt_out,
	.vidioc_s_fmt_vid_out		= vidioc_s_fmt_out,

#ifdef V4L2LOOPBACK_WITH_STD
	.vidioc_g_std			= vidioc_g_std,
	.vidioc_s_std			= vidioc_s_std,
#endif /* V4L2LOOPBACK_WITH_STD */

	.vidioc_g_parm			= vidioc_g_parm,
	.vidioc_s_parm			= vidioc_s_parm,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_expbuf			= vb2_ioctl_expbuf,

	.vidioc_subscribe_event		= vidioc_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
	// clang-format on
};

static const struct v4l2_file_operations capture_fops = {
	// clang-format off
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read		= vb2_fop_read,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
	// clang-format on
};

static const struct v4l2_ioctl_ops capture_ioctl_ops = {
	// clang-format off
	.vidioc_querycap		= vidioc_querycap,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,
	.vidioc_enum_frameintervals	= vidioc_enum_frameintervals,

	.vidioc_enum_input		= vidioc_enum_input,
	.vidioc_g_input			= vidioc_g_input,
	.vidioc_s_input			= vidioc_s_input,

	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_cap,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt_cap,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt_cap,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt_cap,

#ifdef V4L2L_OVERLAY
	.vidioc_s_fmt_vid_overlay	= vidioc_s_fmt_overlay,
	.vidioc_g_fmt_vid_overlay	= vidioc_g_fmt_overlay,
#endif

#ifdef V4L2LOOPBACK_WITH_STD
	.vidioc_s_std			= vidioc_s_std,
	.vidioc_g_std			= vidioc_g_std,
	.vidioc_querystd		= vidioc_querystd,
#endif /* V4L2LOOPBACK_WITH_STD */

	.vidioc_g_parm			= vidioc_g_parm,
	.vidioc_s_parm			= vidioc_s_parm,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_expbuf			= vb2_ioctl_expbuf,

	.vidioc_subscribe_event		= vidioc_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
	// clang-format on
};

static int free_device_cb(int id, void *ptr, void *data)
{
	struct v4l2_loopback_device *dev = ptr;

	/* Half-configured device instances are freed in v4l2_loopback_add(),
	 * so here we only have to deal with fully instanciated devices. In
	 * order to avoid double free, free only when id matches its output_nr.
	 */
	if (id == dev->output.vdev.num)
		v4l2_loopback_remove(dev);

	return 0;
}
static void free_devices(void)
{
	idr_for_each(&v4l2loopback_index_idr, &free_device_cb, NULL);
	idr_destroy(&v4l2loopback_index_idr);
}

static int v4l2loopback_init_module(void)
{
	int err;
	int i;
	MARK();

	err = misc_register(&v4l2loopback_misc);
	if (err < 0)
		return err;

	if (devices < 0) {
		devices = 1;

		/* try guessing the devices from the "video_nr" and "output_nr"
		 * parameters */
		for (i = MAX_DEVICES - 1; i >= 0; i--) {
			if (video_nr[i] >= 0 || output_nr[i] >= 0) {
				devices = i + 1;
				break;
			}
		}
	}

	if (devices > MAX_DEVICES) {
		devices = MAX_DEVICES;
		printk(KERN_INFO
		       "v4l2loopback: number of initial devices is limited to: %d\n",
		       MAX_DEVICES);
	}

	if (max_buffers > MAX_BUFFERS) {
		max_buffers = MAX_BUFFERS;
		printk(KERN_INFO
		       "v4l2loopback: number of buffers is limited to: %d\n",
		       MAX_BUFFERS);
	}

	if (max_openers < 0) {
		printk(KERN_INFO
		       "v4l2loopback: allowing %d openers rather than %d\n",
		       2, max_openers);
		max_openers = 2;
	}

	if (max_width < 1) {
		max_width = V4L2LOOPBACK_SIZE_DEFAULT_MAX_WIDTH;
		printk(KERN_INFO "v4l2loopback: using max_width %d\n",
		       max_width);
	}
	if (max_height < 1) {
		max_height = V4L2LOOPBACK_SIZE_DEFAULT_MAX_HEIGHT;
		printk(KERN_INFO "v4l2loopback: using max_height %d\n",
		       max_height);
	}

	/* kfree on module release */
	for (i = 0; i < devices; i++) {
		struct v4l2_loopback_config cfg = {
			// clang-format off
			.output_nr		= output_nr[i],
			.capture_nr		= video_nr[i],
			.max_width		= max_width,
			.max_height		= max_height,
			.max_buffers		= max_buffers,
			.max_openers		= max_openers,
			.debug			= debug,
			// clang-format on
		};
		cfg.card_label[0] = 0;
		if (card_label[i])
			snprintf(cfg.card_label, sizeof(cfg.card_label), "%s",
				 card_label[i]);
		err = v4l2_loopback_add(&cfg, video_nr + i);
		if (err) {
			free_devices();
			goto error;
		}
	}

	dprintk("module installed\n");

	printk(KERN_INFO "v4l2loopback driver version %d.%d.%d loaded\n",
	       // clang-format off
	       (V4L2LOOPBACK_VERSION_CODE >> 16) & 0xff,
	       (V4L2LOOPBACK_VERSION_CODE >>  8) & 0xff,
	       (V4L2LOOPBACK_VERSION_CODE      ) & 0xff);
	// clang-format on

	return 0;
error:
	misc_deregister(&v4l2loopback_misc);
	return err;
}

static void v4l2loopback_cleanup_module(void)
{
	MARK();
	/* unregister the device -> it deletes /dev/video* */
	free_devices();
	/* and get rid of /dev/v4l2loopback */
	misc_deregister(&v4l2loopback_misc);
	dprintk("module removed\n");
}

MODULE_ALIAS_MISCDEV(MISC_DYNAMIC_MINOR);

module_init(v4l2loopback_init_module);
module_exit(v4l2loopback_cleanup_module);

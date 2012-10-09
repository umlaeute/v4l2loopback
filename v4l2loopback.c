/*
 * v4l2loopback.c  --  video4linux2 loopback driver
 *
 * Copyright (C) 2005-2009 Vasily Levin (vasaka@gmail.com)
 * Copyright (C) 2010-2012 IOhannes m zmoelnig (zmoelnig@iem.at)
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
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
# define v4l2_file_operations file_operations
#endif

#include <linux/sched.h>
#include <linux/slab.h>

#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION(0,6,1)


MODULE_DESCRIPTION("V4L2 loopback video device");
MODULE_AUTHOR("Vasily Levin, IOhannes m zmoelnig <zmoelnig@iem.at>, Stefan Diewald, Anton Novikov");
MODULE_LICENSE("GPL");


/* helpers */
#define STRINGIFY(s) #s
#define STRINGIFY2(s) STRINGIFY(s)

#define dprintk(fmt, args...)                                           \
  do { if (debug > 0) {                                                 \
      printk(KERN_INFO "v4l2-loopback[" STRINGIFY2(__LINE__) "]: " fmt, ##args); \
    } } while(0)

#define MARK()                                                          \
  do{ if (debug > 1) {                                                  \
      printk(KERN_INFO "%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);	\
    } } while (0)

#define dprintkrw(fmt, args...)                                         \
  do { if (debug > 2) {                                                 \
      printk(KERN_INFO "v4l2-loopback[" STRINGIFY2(__LINE__)"]: " fmt, ##args); \
    } } while (0)


/* module constants */
#define MAX_TIMEOUT (100 * 1000 * 1000) /* in msecs */

/* module parameters */
static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR );
MODULE_PARM_DESC(debug, "debugging level (higher values == more verbose)");

#define MAX_BUFFERS 32  /* max buffers that can be mapped, actually they
                         * are all mapped to max_buffers buffers */
static int max_buffers = 8;
module_param(max_buffers, int, S_IRUGO);
MODULE_PARM_DESC(max_buffers, "how many buffers should be allocated");

/* how many times a device can be opened
 * the per-module default value can be overridden on a per-device basis using
 * the /sys/devices interface
 *
 * note that max_openers should be at least 2 in order to get a working system:
 *   one opener for the producer and one opener for the consumer
 *   however, we leave that to the user
 */
static int max_openers = 10;
module_param(max_openers, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(max_openers, "how many users can open loopback device");


#define MAX_DEVICES 8
static int devices = -1;
module_param(devices, int, 0);
MODULE_PARM_DESC(devices, "how many devices should be created");


static int video_nr[MAX_DEVICES] = { [0 ... (MAX_DEVICES-1)] = -1 };
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "video device numbers (-1=auto, 0=/dev/video0, etc.)");



/* format specifications */
#define V4L2LOOPBACK_SIZE_MIN_WIDTH   48
#define V4L2LOOPBACK_SIZE_MIN_HEIGHT  32
#define V4L2LOOPBACK_SIZE_MAX_WIDTH   8192
#define V4L2LOOPBACK_SIZE_MAX_HEIGHT  8192

#define V4L2LOOPBACK_SIZE_DEFAULT_WIDTH   640
#define V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT  480

static int max_width = V4L2LOOPBACK_SIZE_MAX_WIDTH;
module_param(max_width, int, S_IRUGO);
MODULE_PARM_DESC(max_width, "maximum frame width");
static int max_height = V4L2LOOPBACK_SIZE_MAX_HEIGHT;
module_param(max_height, int, S_IRUGO);
MODULE_PARM_DESC(max_height, "maximum frame height");


/* control IDs */
#define CID_KEEP_FORMAT        (V4L2_CID_PRIVATE_BASE+0)
#define CID_SUSTAIN_FRAMERATE  (V4L2_CID_PRIVATE_BASE+1)
#define CID_TIMEOUT            (V4L2_CID_PRIVATE_BASE+2)
#define CID_TIMEOUT_IMAGE_IO   (V4L2_CID_PRIVATE_BASE+3)


/* module structures */
struct v4l2loopback_private {
  int devicenr;
};

typedef struct v4l2loopback_private *priv_ptr;

/* TODO(vasaka) use typenames which are common to kernel, but first find out if
 * it is needed */
/* struct keeping state and settings of loopback device */

struct v4l2l_buffer {
  struct v4l2_buffer buffer;
  struct list_head list_head;
  int use_count;
};

struct v4l2_loopback_device {
  struct video_device *vdev;
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
  u8 *image;         /* pointer to actual buffers data */
  unsigned long int imagesize;  /* size of buffers data */
  int buffers_number;  /* should not be big, 4 is a good choice */
  struct v4l2l_buffer buffers[MAX_BUFFERS];	/* inner driver buffers */
  int used_buffers; /* number of the actually used buffers */
  int max_openers;  /* how many times can this device be opened */

  int write_position; /* number of last written frame + 1 */
  struct list_head outbufs_list; /* buffers in output DQBUF order */
  int readpos2index[MAX_BUFFERS]; /* mapping of (read_position % used_buffers)
                                   * to capture DQBUF index */
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
  int ready_for_capture;/* set to true when at least one writer opened
                         * device and negotiated format */
  wait_queue_head_t read_event;
  spinlock_t lock;
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
  int vidioc_enum_frameintervals_calls;
  int read_position; /* number of last processed frame + 1 or
                      * write_position - 1 if reader went out of sync */
  unsigned int reread_count;
  struct v4l2_buffer *buffers;
  int buffers_number;  /* should not be big, 4 is a good choice */
  int timeout_image_io;
};

/* this is heavily inspired by the bttv driver found in the linux kernel */
struct v4l2l_format {
  char *name;
  int  fourcc;          /* video4linux 2      */
  int  depth;           /* bit/pixel          */
  int  flags;
};
/* set the v4l2l_format.flags to PLANAR for non-packed formats */
#define FORMAT_FLAGS_PLANAR       0x01

static const struct v4l2l_format formats[] = {
  /* here come the packed formats */
  {
    .name     = "32 bpp RGB, le",
    .fourcc   = V4L2_PIX_FMT_BGR32,
    .depth    = 32,
    .flags    = 0,
  },{
    .name     = "32 bpp RGB, be",
    .fourcc   = V4L2_PIX_FMT_RGB32,
    .depth    = 32,
    .flags    = 0,
  },{
    .name     = "24 bpp RGB, le",
    .fourcc   = V4L2_PIX_FMT_BGR24,
    .depth    = 24,
    .flags    = 0,
  },{
    .name     = "24 bpp RGB, be",
    .fourcc   = V4L2_PIX_FMT_RGB24,
    .depth    = 24,
    .flags    = 0,
  },{
    .name     = "4:2:2, packed, YUYV",
    .fourcc   = V4L2_PIX_FMT_YUYV,
    .depth    = 16,
    .flags    = 0,
  },{
    .name     = "4:2:2, packed, YUYV",
    .fourcc   = V4L2_PIX_FMT_YUYV,
    .depth    = 16,
    .flags    = 0,
  },{
    .name     = "4:2:2, packed, UYVY",
    .fourcc   = V4L2_PIX_FMT_UYVY,
    .depth    = 16,
    .flags    = 0,
  },{
#ifdef V4L2_PIX_FMT_YVYU
    .name     = "4:2:2, packed YVYU",
    .fourcc   = V4L2_PIX_FMT_YVYU,
    .depth    = 16,
    .flags=0,
  },{
#endif
#ifdef V4L2_PIX_FMT_VYUY
    .name     = "4:2:2, packed VYUY",
    .fourcc   = V4L2_PIX_FMT_VYUY,
    .depth    = 16,
    .flags=0,
  },{
#endif
    .name     = "4:2:2, packed YYUV",
    .fourcc   = V4L2_PIX_FMT_YYUV,
    .depth    = 16,
    .flags=0,
  },{
    .name     = "YUV-8-8-8-8",
    .fourcc   = V4L2_PIX_FMT_YUV32,
    .depth    = 32,
    .flags    = 0,
  },{
    .name     = "8 bpp, gray",
    .fourcc   = V4L2_PIX_FMT_GREY,
    .depth    = 8,
    .flags    = 0,
  },{
    .name     = "16 Greyscale",
    .fourcc   = V4L2_PIX_FMT_Y16,
    .depth    = 16,
    .flags    = 0,
  },

  /* here come the planar formats */
  {
    .name     = "4:1:0, planar, Y-Cr-Cb",
    .fourcc   = V4L2_PIX_FMT_YVU410,
    .depth    = 9,
    .flags    = FORMAT_FLAGS_PLANAR,
  },{
    .name     = "4:2:0, planar, Y-Cr-Cb",
    .fourcc   = V4L2_PIX_FMT_YVU420,
    .depth    = 12,
    .flags    = FORMAT_FLAGS_PLANAR,
  },{
    .name     = "4:1:0, planar, Y-Cb-Cr",
    .fourcc   = V4L2_PIX_FMT_YUV410,
    .depth    = 9,
    .flags    = FORMAT_FLAGS_PLANAR,
  },{
    .name     = "4:2:0, planar, Y-Cb-Cr",
    .fourcc   = V4L2_PIX_FMT_YUV420,
    .depth    = 12,
    .flags    = FORMAT_FLAGS_PLANAR,
  }
};
static const unsigned int FORMATS = ARRAY_SIZE(formats);


static char*
fourcc2str          (unsigned int fourcc,
                     char buf[4])
{
  buf[0]=(fourcc>> 0) & 0xFF;
  buf[1]=(fourcc>> 8) & 0xFF;
  buf[2]=(fourcc>>16) & 0xFF;
  buf[3]=(fourcc>>24) & 0xFF;

  return buf;
}

static const struct v4l2l_format*
format_by_fourcc    (int fourcc)
{
  unsigned int i;

  for (i = 0; i < FORMATS; i++) {
    if (formats[i].fourcc == fourcc)
      return formats+i;  }

  dprintk("unsupported format '%c%c%c%c'",
          (fourcc>> 0) & 0xFF,
          (fourcc>> 8) & 0xFF,
          (fourcc>>16) & 0xFF,
          (fourcc>>24) & 0xFF);
  return NULL;
}

static void
pix_format_set_size     (struct v4l2_pix_format *       f,
                         const struct v4l2l_format *    fmt,
                         unsigned int                   width,
                         unsigned int                   height)
{
  f->width = width;
  f->height = height;

  if (fmt->flags & FORMAT_FLAGS_PLANAR) {
    f->bytesperline = width; /* Y plane */
    f->sizeimage = (width * height * fmt->depth) >> 3;
  } else {
    f->bytesperline = (width * fmt->depth) >> 3;
    f->sizeimage = height * f->bytesperline;
  }
}

static void
set_timeperframe(struct v4l2_loopback_device *dev, struct v4l2_fract *tpf)
{
  dev->capture_param.timeperframe = *tpf;
  dev->frame_jiffies = max(1UL, msecs_to_jiffies(1000) * tpf->numerator / tpf->denominator);
}

static struct v4l2_loopback_device*v4l2loopback_cd2dev  (struct device*cd);

/* device attributes */
/* available via sysfs: /sys/devices/virtual/video4linux/video* */

static ssize_t attr_show_format(struct device *cd,
                                struct device_attribute *attr,
                                char *buf)
{
  /* gets the current format as "FOURCC:WxH@f/s", e.g. "YUYV:320x240@1000/30" */
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  const struct v4l2_fract *tpf;
  char buf4cc[5], buf_fps[32];

  if (!dev || !dev->ready_for_capture)
    return 0;
  tpf = &dev->capture_param.timeperframe;

  fourcc2str(dev->pix_format.pixelformat, buf4cc);
  if (tpf->numerator == 1)
    snprintf(buf_fps, sizeof(buf_fps), "%d", tpf->denominator);
  else
    snprintf(buf_fps, sizeof(buf_fps), "%d/%d", tpf->denominator, tpf->numerator);

  return sprintf(buf, "%4s:%dx%d@%s\n",
                   buf4cc, dev->pix_format.width, dev->pix_format.height, buf_fps);
}
static ssize_t attr_store_format(struct device* cd,
                                 struct device_attribute *attr,
                                 const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  int fps_num = 0, fps_den = 1;

  /* only fps changing is supported */
  if (sscanf(buf, "@%d/%d", &fps_num, &fps_den) > 0) {
    if (fps_num < 1 || fps_den < 1)
      return -EINVAL;
    set_timeperframe(dev, &(struct v4l2_fract){.numerator   = fps_den,
                                               .denominator = fps_num});
    return len;
  } else {
    return -EINVAL;
  }
}
static DEVICE_ATTR(format, S_IRUGO | S_IWUSR, attr_show_format, attr_store_format);

static ssize_t attr_show_buffers(struct device *cd,
                                 struct device_attribute *attr,
                                 char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->used_buffers);
}
static DEVICE_ATTR(buffers, S_IRUGO, attr_show_buffers, NULL);

static ssize_t attr_show_maxopeners(struct device *cd,
                                    struct device_attribute *attr,
                                    char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->max_openers);
}
static ssize_t attr_store_maxopeners(struct device* cd,
                                     struct device_attribute *attr,
                                     const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = NULL;
  unsigned long curr=0;

  if (strict_strtoul(buf, 0, &curr))
    return -EINVAL;

  dev = v4l2loopback_cd2dev(cd);

  if (dev->max_openers == curr)
    return len;

  if (dev->open_count.counter > curr) {
    /* request to limit to less openers as are currently attached to us */
    return -EINVAL;
  }

  dev->max_openers = (int)curr;

  return len;
}


static DEVICE_ATTR(max_openers, S_IRUGO | S_IWUSR, attr_show_maxopeners, attr_store_maxopeners);





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
  int res=0;
#define V4L2_SYSFS_CREATE(x)     res = device_create_file(&vdev->dev, &dev_attr_##x); if (res < 0) break
  if (!vdev) return;
  do {
    V4L2_SYSFS_CREATE(format);
    V4L2_SYSFS_CREATE(buffers);
    V4L2_SYSFS_CREATE(max_openers);
    /* ... */
  } while(0);

  if (res >= 0)return;
  dev_err(&vdev->dev, "%s error: %d\n", __func__, res);
}






/* global module data */
struct v4l2_loopback_device *devs[MAX_DEVICES];

static struct v4l2_loopback_device*
v4l2loopback_cd2dev  (struct device*cd)
{
  struct video_device *loopdev = to_video_device(cd);
  priv_ptr ptr = (priv_ptr)video_get_drvdata(loopdev);
  int nr = ptr->devicenr;
  if(nr<0 || nr>=devices){printk(KERN_ERR "v4l2-loopback: illegal device %d\n",nr);return NULL;}
  return devs[nr];
}

static struct v4l2_loopback_device*
v4l2loopback_getdevice        (struct file*f)
{
  struct video_device *loopdev = video_devdata(f);
  priv_ptr ptr = (priv_ptr)video_get_drvdata(loopdev);
  int nr = ptr->devicenr;
  if(nr<0 || nr>=devices){printk(KERN_ERR "v4l2-loopback: illegal device %d\n",nr);return NULL;}
  return devs[nr];
}

/* forward declarations */
static void init_buffers(struct v4l2_loopback_device *dev);
static int allocate_buffers(struct v4l2_loopback_device *dev);
static int free_buffers(struct v4l2_loopback_device *dev);
static void try_free_buffers(struct v4l2_loopback_device *dev);
static int allocate_timeout_image(struct v4l2_loopback_device *dev);
static void check_timers(struct v4l2_loopback_device *dev);
static const struct v4l2_file_operations v4l2_loopback_fops;
static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops;

/* Queue helpers */
/* next functions sets buffer flags and adjusts counters accordingly */
static inline void
set_done            (struct v4l2l_buffer *buffer)
{
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
  buffer->buffer.flags |= V4L2_BUF_FLAG_DONE;
}

static inline void
set_queued          (struct v4l2l_buffer *buffer)
{
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
  buffer->buffer.flags |= V4L2_BUF_FLAG_QUEUED;
}

static inline void
unset_flags         (struct v4l2l_buffer *buffer)
{
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
}
/* V4L2 ioctl caps and params calls */
/* returns device capabilities
 * called on VIDIOC_QUERYCAP
 */
static int
vidioc_querycap     (struct file *file,
                     void *priv,
                     struct v4l2_capability *cap)
{
  strlcpy(cap->driver, "v4l2 loopback", sizeof(cap->driver));
  strlcpy(cap->card, "Dummy video device", sizeof(cap->card));
  cap->bus_info[0]=0;

  cap->version = V4L2LOOPBACK_VERSION_CODE;
  cap->capabilities =
    V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
    V4L2_CAP_STREAMING |
    V4L2_CAP_READWRITE
    ;

  memset(cap->reserved, 0, sizeof(cap->reserved));

  return 0;
}

static int
vidioc_enum_framesizes        (struct file *file, void *fh,
                               struct v4l2_frmsizeenum *argp)
{
  struct v4l2_loopback_device *dev;

  /* LATER: what does the index really  mean?
   * if it's about enumerating formats, we can safely ignore it
   * (CHECK)
   */

  /* there can be only one... */
  if (argp->index)
    return -EINVAL;


  dev=v4l2loopback_getdevice(file);
  if (dev->ready_for_capture) {
    /* format has already been negotiated
     * cannot change during runtime
     */
    argp->type=V4L2_FRMSIZE_TYPE_DISCRETE;

    argp->discrete.width=dev->pix_format.width;
    argp->discrete.height=dev->pix_format.height;
  } else {
    /* if the format has not been negotiated yet, we accept anything
     */
    argp->type=V4L2_FRMSIZE_TYPE_CONTINUOUS;

    argp->stepwise.min_width=V4L2LOOPBACK_SIZE_MIN_WIDTH;
    argp->stepwise.min_height=V4L2LOOPBACK_SIZE_MIN_HEIGHT;

    argp->stepwise.max_width=max_width;
    argp->stepwise.max_height=max_height;

    argp->stepwise.step_width=1;
    argp->stepwise.step_height=1;
  }
  return 0;
}

/* returns frameinterval (fps) for the set resolution
 * called on VIDIOC_ENUM_FRAMEINTERVALS
 */
static int
vidioc_enum_frameintervals(struct file *file,
			   void *fh,
			   struct v4l2_frmivalenum *argp)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);
  struct v4l2_loopback_opener *opener = file->private_data;

  if (dev->ready_for_capture) {
    if (opener->vidioc_enum_frameintervals_calls > 0)
      return -EINVAL;
    if (argp->width == dev->pix_format.width &&
        argp->height== dev->pix_format.height)
      {
        argp->type = V4L2_FRMIVAL_TYPE_DISCRETE;
        argp->discrete = dev->capture_param.timeperframe;
        opener->vidioc_enum_frameintervals_calls++;
        return 0;
      } else {
      return -EINVAL;
    }
  }
  return 0;
}

/* ------------------ CAPTURE ----------------------- */

/* returns device formats
 * called on VIDIOC_ENUM_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_enum_fmt_cap (struct file *file,
                     void *fh,
                     struct v4l2_fmtdesc *f)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (f->index)
    return -EINVAL;
  if (dev->ready_for_capture) {
    const __u32 format = dev->pix_format.pixelformat;
    //  strlcpy(f->description, "current format", sizeof(f->description));

    snprintf(f->description, sizeof(f->description),
             "[%c%c%c%c]",
             (format>> 0) & 0xFF,
             (format>> 8) & 0xFF,
             (format>>16) & 0xFF,
             (format>>24) & 0xFF);

    f->pixelformat = dev->pix_format.pixelformat;
  } else {
    return -EINVAL;
  }
  f->flags=0;
  MARK();
  return 0;
}

/* returns current video format format fmt
 * called on VIDIOC_G_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_g_fmt_cap    (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (!dev->ready_for_capture) {
    return -EINVAL;
  }

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
static int
vidioc_try_fmt_cap  (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;

  opener = file->private_data;
  opener->type = READER;

  dev=v4l2loopback_getdevice(file);

  if (0 == dev->ready_for_capture) {
    dprintk("setting fmt_cap not possible yet\n");
    return -EBUSY;
  }

  if (fmt->fmt.pix.pixelformat != dev->pix_format.pixelformat)
    return -EINVAL;

  fmt->fmt.pix = dev->pix_format;

  do { char buf[5]; buf[4]=0; dprintk("capFOURCC=%s\n", fourcc2str(dev->pix_format.pixelformat, buf)); } while(0);
  return 0;
}

/* sets new output format, if possible
 * actually format is set  by input and we even do not check it, just return
 * current one, but it is possible to set subregions of input TODO(vasaka)
 * called on VIDIOC_S_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_s_fmt_cap    (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  return vidioc_try_fmt_cap(file, priv, fmt);
}


/* ------------------ OUTPUT ----------------------- */

/* returns device formats;
 * LATER: allow all formats
 * called on VIDIOC_ENUM_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_enum_fmt_out (struct file *file,
                     void *fh,
                     struct v4l2_fmtdesc *f)
{
  struct v4l2_loopback_device *dev;
  const struct v4l2l_format *  fmt;

  dev=v4l2loopback_getdevice(file);

  if (dev->ready_for_capture) {
    const __u32 format = dev->pix_format.pixelformat;

    /* format has been fixed by the writer, so only one single format is supported */
    if (f->index)
      return -EINVAL;

    fmt=format_by_fourcc(format);
    if(NULL == fmt)
      return -EINVAL;

    f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //    f->flags = ??;
    snprintf(f->description, sizeof(f->description),
             fmt->name);

    f->pixelformat = dev->pix_format.pixelformat;
  } else {
    __u32 format;
    /* fill in a dummy format */
    if(f->index < 0 ||
       f->index >= FORMATS)
      return -EINVAL;

    fmt=&formats[f->index];

    f->pixelformat=fmt->fourcc;
    format = f->pixelformat;

    //    strlcpy(f->description, "dummy OUT format", sizeof(f->description));
    snprintf(f->description, sizeof(f->description),
             fmt->name);

  }
  f->flags=0;

  return 0;
}

/* returns current video format format fmt */
/* NOTE: this is called from the producer
 * so if format has not been negotiated yet,
 * it should return ALL of available formats,
 * called on VIDIOC_G_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_g_fmt_out    (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;

  MARK();

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;
  opener->type = WRITER;
  /*
   * LATER: this should return the currently valid format
   * gstreamer doesn't like it, if this returns -EINVAL, as it
   * then concludes that there is _no_ valid format
   * CHECK whether this assumption is wrong,
   * or whether we have to always provide a valid format
   */

  if (0 == dev->ready_for_capture) {
    /* we are not fixated yet, so return a default format */
    const struct v4l2l_format *     defaultfmt=&formats[0];

    dev->pix_format.width=0; /* V4L2LOOPBACK_SIZE_DEFAULT_WIDTH; */
    dev->pix_format.height=0; /* V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT; */
    dev->pix_format.pixelformat=defaultfmt->fourcc;
    dev->pix_format.colorspace=V4L2_COLORSPACE_SRGB; /* do we need to set this ? */
    dev->pix_format.field=V4L2_FIELD_NONE;

    pix_format_set_size(&fmt->fmt.pix, defaultfmt,
                        dev->pix_format.width, dev->pix_format.height);

    dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
    dprintk("buffer_size = %ld (=%d)", dev->buffer_size, dev->pix_format.sizeimage);
    allocate_buffers(dev);
  }
  fmt->fmt.pix = dev->pix_format;
  return 0;
}

/* checks if it is OK to change to format fmt;
 * if format is negotiated do not change it
 * called on VIDIOC_TRY_FMT with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_try_fmt_out  (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  MARK();

  opener = file->private_data;
  opener->type = WRITER;

  dev=v4l2loopback_getdevice(file);

  /* TODO(vasaka) loopback does not care about formats writer want to set,
   * maybe it is a good idea to restrict format somehow */
  if (dev->ready_for_capture) {
    fmt->fmt.pix = dev->pix_format;
  } else {
    __u32 w=fmt->fmt.pix.width;
    __u32 h=fmt->fmt.pix.height;
    __u32 pixfmt=fmt->fmt.pix.pixelformat;
    const struct v4l2l_format*format=format_by_fourcc(pixfmt);

    if(w>max_width)
      w=max_width;
    if(h>max_height)
      h=max_height;

    dprintk("trying image %dx%d", w, h);

    if(w<1)
      w=V4L2LOOPBACK_SIZE_DEFAULT_WIDTH;

    if(h<1)
      h=V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT;

    if(NULL==format) {
      format=&formats[0];
    }

    pix_format_set_size(&fmt->fmt.pix, format, w, h);

    fmt->fmt.pix.pixelformat = format->fourcc;
    fmt->fmt.pix.colorspace=V4L2_COLORSPACE_SRGB;

    if(V4L2_FIELD_ANY == fmt->fmt.pix.field)
      fmt->fmt.pix.field=V4L2_FIELD_NONE;

    /* FIXXME: try_fmt should never modify the device-state */
    dev->pix_format = fmt->fmt.pix;
  }
  return 0;
}

/* sets new output format, if possible;
 * allocate data here because we do not know if it will be streaming or
 * read/write IO
 * called on VIDIOC_S_FMT with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_s_fmt_out    (struct file *file,
                     void *priv, struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;
  int ret;
  MARK();

  dev=v4l2loopback_getdevice(file);
  ret = vidioc_try_fmt_out(file, priv, fmt);

  dprintk("s_fmt_out(%d) %d...%d", ret, dev->ready_for_capture, dev->pix_format.sizeimage);

  do {
    char buf[5];
    buf[4]=0;
    dprintk("outFOURCC=%s\n", fourcc2str(dev->pix_format.pixelformat, buf));
  } while(0);

  if (ret < 0)
    return ret;

  if (!dev->ready_for_capture) {
    dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
    fmt->fmt.pix.sizeimage = dev->buffer_size;
    allocate_buffers(dev);
  }
  return ret;
}

//#define V4L2L_OVERLAY
#ifdef V4L2L_OVERLAY
/* ------------------ OVERLAY ----------------------- */
/* currently unsupported */
/* GSTreamer's v4l2sink is buggy, as it requires the overlay to work
 * while it should only require it, if overlay is requested
 * once the gstreamer element is fixed, remove the overlay dummies
 */
#warning OVERLAY dummies
static int
vidioc_g_fmt_overlay(struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  return 0;
}
static int
vidioc_s_fmt_overlay(struct file *file,
                     void *priv,
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
static int
vidioc_g_parm       (struct file *file,
                     void *priv,
                     struct v4l2_streamparm *parm)
{
  /* do not care about type of opener, hope this enums would always be
   * compatible */
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);
  parm->parm.capture = dev->capture_param;
  return 0;
}

/* get some data flow parameters, only capability, fps and readbuffers has
 * effect on this driver
 * called on VIDIOC_S_PARM
 */
static int
vidioc_s_parm       (struct file *file,
                     void *priv,
                     struct v4l2_streamparm *parm)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);
  dprintk("vidioc_s_parm called frate=%d/%d\n",
          parm->parm.capture.timeperframe.numerator,
          parm->parm.capture.timeperframe.denominator);

  switch (parm->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    set_timeperframe(dev, &parm->parm.capture.timeperframe);
    break;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    set_timeperframe(dev, &parm->parm.capture.timeperframe);
    break;
  default:
    return -1;
  }

  parm->parm.capture = dev->capture_param;
  return 0;
}

/* sets a tv standard, actually we do not need to handle this any special way
 * added to support effecttv
 * called on VIDIOC_S_STD
 */
static int
vidioc_s_std        (struct file *file,
                     void *private_data,
                     v4l2_std_id *_std)
{
  v4l2_std_id req_std=0, supported_std=0;
  const v4l2_std_id all_std=V4L2_STD_ALL, no_std=0;

  if(_std) {
    req_std=*_std;
    *_std=all_std;
  }

  /* we support everything in V4L2_STD_ALL, but not more... */
  supported_std=(all_std & req_std);
  if(no_std == supported_std) {
    return -EINVAL;
  }

  return 0;
}


/* gets a fake video standard
 * called on VIDIOC_G_STD
 */
static int
vidioc_g_std        (struct file *file,
                     void *private_data,
                     v4l2_std_id *norm)
{
  if(norm)
    *norm=V4L2_STD_ALL;
  return 0;
}
/* gets a fake video standard
 * called on VIDIOC_QUERYSTD
 */
static int
vidioc_querystd     (struct file *file,
                     void *private_data,
                     v4l2_std_id *norm)
{
  if(norm)
    *norm=V4L2_STD_ALL;
  return 0;
}


/* get ctrls info
 * called on VIDIOC_QUERYCTRL
 */
static int
vidioc_queryctrl(struct file *file, void *fh,
                 struct v4l2_queryctrl *q)
{
  switch (q->id) {
  case CID_KEEP_FORMAT:
  case CID_SUSTAIN_FRAMERATE:
  case CID_TIMEOUT_IMAGE_IO:
    q->type = V4L2_CTRL_TYPE_BOOLEAN;
    q->minimum = 0;
    q->maximum = 1;
    q->step = 1;
    break;
  case CID_TIMEOUT:
    q->type = V4L2_CTRL_TYPE_INTEGER;
    q->minimum = 0;
    q->maximum = MAX_TIMEOUT;
    q->step = 1;
    break;
  default:
    return -EINVAL;
  }

  switch (q->id) {
  case CID_KEEP_FORMAT:
    strcpy(q->name, "keep_format");
    q->default_value = 0;
    break;
  case CID_SUSTAIN_FRAMERATE:
    strcpy(q->name, "sustain_framerate");
    q->default_value = 0;
    break;
  case CID_TIMEOUT:
    strcpy(q->name, "timeout");
    q->default_value = 0;
    break;
  case CID_TIMEOUT_IMAGE_IO:
    strcpy(q->name, "timeout_image_io");
    q->default_value = 0;
    break;
  default:
    BUG();
  }

  memset(q->reserved, 0, sizeof(q->reserved));
  return 0;
}


static int
vidioc_g_ctrl(struct file *file, void *fh,
              struct v4l2_control *c)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);

  switch (c->id) {
  case CID_KEEP_FORMAT:
    c->value = dev->keep_format;
    break;
  case CID_SUSTAIN_FRAMERATE:
    c->value = dev->sustain_framerate;
    break;
  case CID_TIMEOUT:
    c->value = jiffies_to_msecs(dev->timeout_jiffies);
    break;
  case CID_TIMEOUT_IMAGE_IO:
    c->value = dev->timeout_image_io;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}


static int
vidioc_s_ctrl(struct file *file, void *fh,
              struct v4l2_control *c)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);

  switch (c->id) {
  case CID_KEEP_FORMAT:
    if (c->value < 0 || c->value > 1)
      return -EINVAL;
    dev->keep_format = c->value;
    try_free_buffers(dev);
    break;
  case CID_SUSTAIN_FRAMERATE:
    if (c->value < 0 || c->value > 1)
      return -EINVAL;
    spin_lock_bh(&dev->lock);
    dev->sustain_framerate = c->value;
    check_timers(dev);
    spin_unlock_bh(&dev->lock);
    break;
  case CID_TIMEOUT:
    if (c->value < 0 || c->value > MAX_TIMEOUT)
      return -EINVAL;
    spin_lock_bh(&dev->lock);
    dev->timeout_jiffies = msecs_to_jiffies(c->value);
    check_timers(dev);
    spin_unlock_bh(&dev->lock);
    allocate_timeout_image(dev);
    break;
  case CID_TIMEOUT_IMAGE_IO:
    if (c->value < 0 || c->value > 1)
      return -EINVAL;
    dev->timeout_image_io = c->value;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}


/* returns set of device outputs, in our case there is only one
 * called on VIDIOC_ENUMOUTPUT
 */
static int
vidioc_enum_output  (struct file *file,
                     void *fh,
                     struct v4l2_output *outp)
{
  __u32 index=outp->index;
  MARK();

  if (0!=index) {
    return -EINVAL;
  }

  /* clear all data (including the reserved fields) */
  memset(outp, 0, sizeof(*outp));

  outp->index = index;
  strlcpy(outp->name, "loopback in", sizeof(outp->name));
  outp->type = V4L2_OUTPUT_TYPE_ANALOG;
  outp->audioset = 0;
  outp->modulator = 0;
  outp->std = V4L2_STD_ALL;

#ifdef V4L2_OUT_CAP_STD
  outp->capabilities |= V4L2_OUT_CAP_STD;
#endif

  return 0;
}

/* which output is currently active,
 * called on VIDIOC_G_OUTPUT
 */
static int
vidioc_g_output     (struct file *file,
                    void *fh,
                    unsigned int *i)
{
  if(i)
    *i = 0;
  return 0;
}

/* set output, can make sense if we have more than one video src,
 * called on VIDIOC_S_OUTPUT
 */
static int
vidioc_s_output      (struct file *file,
                     void *fh,
                     unsigned int i)
{
  if(i)
    return -EINVAL;
  i=0;

  if (v4l2loopback_getdevice(file)->ready_for_capture) {
    return -EBUSY;
  }

  return 0;
}


/* returns set of device inputs, in our case there is only one,
 * but later I may add more
 * called on VIDIOC_ENUMINPUT
 */
static int
vidioc_enum_input   (struct file *file,
                     void *fh,
                     struct v4l2_input *inp)
{
  __u32 index=inp->index;
  MARK();

  if (0!=index) {
    return -EINVAL;
  }

  if (!v4l2loopback_getdevice(file)->ready_for_capture)
    return -EINVAL;

  /* clear all data (including the reserved fields) */
  memset(inp, 0, sizeof(*inp));

  inp->index = index;
  strlcpy(inp->name, "loopback", sizeof(inp->name));
  inp->type = V4L2_INPUT_TYPE_CAMERA;
  inp->audioset = 0;
  inp->tuner = 0;
  inp->std = V4L2_STD_ALL;
  inp->status = 0;


#ifdef V4L2_IN_CAP_STD
  inp->capabilities |= V4L2_IN_CAP_STD;
#endif
  return 0;
}

/* which input is currently active,
 * called on VIDIOC_G_INPUT
 */
static int
vidioc_g_input     (struct file *file,
                    void *fh,
                    unsigned int *i)
{
 if (!v4l2loopback_getdevice(file)->ready_for_capture)
   return -EINVAL;
  if(i)
    *i = 0;
  return 0;
}

/* set input, can make sense if we have more than one video src,
 * called on VIDIOC_S_INPUT
 */
static int
vidioc_s_input      (struct file *file,
                     void *fh,
                     unsigned int i)
{
  if ((i==0) && (v4l2loopback_getdevice(file)->ready_for_capture))
    return 0;

  return -EINVAL;
}

/* --------------- V4L2 ioctl buffer related calls ----------------- */

/* negotiate buffer type
 * only mmap streaming supported
 * called on VIDIOC_REQBUFS
 */
static int
vidioc_reqbufs      (struct file *file,
                     void *fh,
                     struct v4l2_requestbuffers *b)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  MARK();

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  dprintk("reqbufs: %d\t%d=%d", b->memory, b->count, dev->buffers_number);
  if (opener->timeout_image_io) {
    if (b->memory != V4L2_MEMORY_MMAP)
      return -EINVAL;
    b->count = 1;
    return 0;
  }

  init_buffers(dev);
  switch (b->memory) {
  case V4L2_MEMORY_MMAP:
    /* do nothing here, buffers are always allocated*/
    if (0 == b->count)
      return 0;

    if (b->count > dev->buffers_number)
      b->count = dev->buffers_number;
    opener->buffers_number = b->count;
    if (opener->buffers_number < dev->used_buffers)
      dev->used_buffers = opener->buffers_number;

    /* make sure that outbufs_list contains buffers from 0 to used_buffers-1 */
    if (list_empty(&dev->outbufs_list)) {
      int i;
      for (i = 0; i < dev->used_buffers; ++i)
        list_add_tail(&dev->buffers[i].list_head, &dev->outbufs_list);
    } else {
      struct v4l2l_buffer *pos, *n;
      list_for_each_entry_safe(pos, n, &dev->outbufs_list, list_head) {
        if (pos->buffer.index >= dev->used_buffers)
          list_del(&pos->list_head);
      }
    }
    return 0;
  default:
    return -EINVAL;
  }
}

/* returns buffer asked for;
 * give app as many buffers as it wants, if it less than MAX,
 * but map them in our inner buffers
 * called on VIDIOC_QUERYBUF
 */
static int
vidioc_querybuf     (struct file *file,
                     void *fh,
                     struct v4l2_buffer *b)
{
  enum v4l2_buf_type type ;
  int index;
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  MARK();

  type = b->type;
  index = b->index;
  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  if ((b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
      (b->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)) {
    return -EINVAL;
  }
  if (b->index > max_buffers)
    return -EINVAL;

  if (opener->timeout_image_io)
    *b = dev->timeout_image_buffer.buffer;
  else
    *b = dev->buffers[b->index % dev->used_buffers].buffer;

  b->type = type;
  b->index = index;
  dprintkrw("buffer type: %d (of %d with size=%ld)", b->memory, dev->buffers_number, dev->buffer_size);
  return 0;
}

static void
buffer_written(struct v4l2_loopback_device *dev, struct v4l2l_buffer *buf)
{
  del_timer_sync(&dev->sustain_timer);
  del_timer_sync(&dev->timeout_timer);
  spin_lock_bh(&dev->lock);

  dev->readpos2index[dev->write_position % dev->used_buffers] = buf->buffer.index;
  list_move_tail(&buf->list_head, &dev->outbufs_list);
  ++dev->write_position;
  dev->reread_count = 0;

  check_timers(dev);
  spin_unlock_bh(&dev->lock);
}

/* put buffer to queue
 * called on VIDIOC_QBUF
 */
static int
vidioc_qbuf         (struct file *file,
                     void *private_data,
                     struct v4l2_buffer *buf)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  struct v4l2l_buffer *b;
  int index;

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  if (buf->index > max_buffers)
    return -EINVAL;
  if (opener->timeout_image_io)
    return 0;

  index = buf->index % dev->used_buffers;
  b=&dev->buffers[index];

  switch (buf->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    dprintkrw("capture QBUF index: %d\n", index);
    set_queued(b);
    return 0;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    dprintkrw("output QBUF pos: %d index: %d\n", dev->write_position, index);
    do_gettimeofday(&b->buffer.timestamp);
    set_done(b);
    buffer_written(dev, b);
    wake_up_all(&dev->read_event);
    return 0;
  default:
    return -EINVAL;
  }
}

static int
can_read(struct v4l2_loopback_device *dev, struct v4l2_loopback_opener *opener)
{
  int ret;
  spin_lock_bh(&dev->lock);
  check_timers(dev);
  ret = dev->write_position > opener->read_position
        || dev->reread_count > opener->reread_count
        || dev->timeout_happened;
  spin_unlock_bh(&dev->lock);
  return ret;
}

static int
get_capture_buffer(struct file *file)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);
  struct v4l2_loopback_opener *opener = file->private_data;
  int pos, ret;
  int timeout_happened;

  if ((file->f_flags&O_NONBLOCK) && (dev->write_position <= opener->read_position &&
                                      dev->reread_count <= opener->reread_count &&
                                      !dev->timeout_happened))
    return -EAGAIN;
  wait_event_interruptible(dev->read_event, can_read(dev, opener));

  spin_lock_bh(&dev->lock);
  if (dev->write_position == opener->read_position) {
    if (dev->reread_count > opener->reread_count+2)
      opener->reread_count = dev->reread_count - 1;
    ++opener->reread_count;
    pos = (opener->read_position + dev->used_buffers - 1) % dev->used_buffers;
  } else {
    opener->reread_count = 0;
    if (dev->write_position > opener->read_position+2)
      opener->read_position = dev->write_position - 1;
    pos = opener->read_position % dev->used_buffers;
    ++opener->read_position;
  }
  timeout_happened = dev->timeout_happened;
  dev->timeout_happened = 0;
  spin_unlock_bh(&dev->lock);

  ret = dev->readpos2index[pos];
  if (timeout_happened) {
    /* although allocated on-demand, timeout_image is freed only in free_buffers(),
     * so we don't need to worry about it being deallocated suddenly */
    memcpy(dev->image + dev->buffers[ret].buffer.m.offset, dev->timeout_image, dev->buffer_size);
  }
  return ret;
}

/* put buffer to dequeue
 * called on VIDIOC_DQBUF
 */
static int
vidioc_dqbuf        (struct file *file,
                     void *private_data,
                     struct v4l2_buffer *buf)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  int index;
  struct v4l2l_buffer *b;

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;
  if (opener->timeout_image_io) {
    *buf = dev->timeout_image_buffer.buffer;
    return 0;
  }

  switch (buf->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    index = get_capture_buffer(file);
    if (index < 0)
      return index;
    dprintkrw("capture DQBUF pos: %d index: %d\n", opener->read_position - 1, index);
    if (!(dev->buffers[index].buffer.flags&V4L2_BUF_FLAG_MAPPED)) {
      dprintk("trying to return not mapped buf\n");
      return -EINVAL;
    }
    unset_flags(&dev->buffers[index]);
    *buf = dev->buffers[index].buffer;
    return 0;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    b = list_entry(dev->outbufs_list.next, struct v4l2l_buffer, list_head);
    list_move_tail(&b->list_head, &dev->outbufs_list);
    dprintkrw("output DQBUF index: %d\n", b->buffer.index);
    unset_flags(b);
    *buf = b->buffer;
    return 0;
  default:
    return -EINVAL;
  }
}

/* ------------- STREAMING ------------------- */

/* start streaming
 * called on VIDIOC_STREAMON
 */
static int
vidioc_streamon     (struct file *file,
                     void *private_data,
                     enum v4l2_buf_type type)
{
  struct v4l2_loopback_device *dev;
  int ret;
  MARK();

  dev=v4l2loopback_getdevice(file);

  switch (type) {
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    if (!dev->ready_for_capture) {
      ret = allocate_buffers(dev);
      if (ret < 0)
        return ret;
      dev->ready_for_capture = 1;
    }
    return 0;
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    if (!dev->ready_for_capture)
      return -EIO;
    return 0;
  default:
    return -EINVAL;
  }
}

/* stop streaming
 * called on VIDIOC_STREAMOFF
 */
static int
vidioc_streamoff    (struct file *file,
                     void *private_data,
                     enum v4l2_buf_type type)
{
  MARK();
  dprintk("%d", type);
  return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int
vidiocgmbuf         (struct file *file,
                     void *fh,
                     struct video_mbuf *p)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);
  p->frames = dev->buffers_number;
  p->offsets[0] = 0;
  p->offsets[1] = 0;
  p->size = dev->buffer_size;
  return 0;
}
#endif

static int
vidioc_g_audout     (struct file *file,
                     void *fh,
                     struct v4l2_audioout *argp) {
  return -EINVAL;
}
static int
vidioc_s_audout     (struct file *file,
                     void *fh,
                     struct v4l2_audioout *argp) {
  return -EINVAL;
}
static int
vidioc_g_audio     (struct file *file,
                     void *fh,
                     struct v4l2_audio *argp) {
  return -EINVAL;
}
static int
vidioc_s_audio     (struct file *file,
                     void *fh,
                     struct v4l2_audio *argp) {
  return -EINVAL;
}


/* file operations */
static void
vm_open             (struct vm_area_struct *vma)
{
  struct v4l2l_buffer *buf;
  MARK();

  buf=vma->vm_private_data;
  buf->use_count++;
}

static void
vm_close            (struct vm_area_struct *vma)
{
  struct v4l2l_buffer *buf;
  MARK();

  buf=vma->vm_private_data;
  buf->use_count--;
}

static struct vm_operations_struct vm_ops = {
  .open = vm_open,
  .close = vm_close,
};

static int
v4l2_loopback_mmap  (struct file *file,
                     struct vm_area_struct *vma)
{
  int i;
  unsigned long addr;
  unsigned long start;
  unsigned long size;
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  struct v4l2l_buffer *buffer = NULL;
  MARK();

  start = (unsigned long) vma->vm_start;
  size = (unsigned long) (vma->vm_end - vma->vm_start);

  dev=v4l2loopback_getdevice(file);
  opener=file->private_data;

  if (size > dev->buffer_size) {
    dprintk("userspace tries to mmap too much, fail\n");
    return -EINVAL;
  }
  if (opener->timeout_image_io) {
    /* we are going to map the timeout_image_buffer */
    if ((vma->vm_pgoff << PAGE_SHIFT) != dev->buffer_size * MAX_BUFFERS) {
      dprintk("invalid mmap offset for timeout_image_io mode\n");
      return -EINVAL;
    }
  } else if ((vma->vm_pgoff << PAGE_SHIFT) >
      dev->buffer_size * (dev->buffers_number - 1)) {
    dprintk("userspace tries to mmap too far, fail\n");
    return -EINVAL;
  }

  /* FIXXXXXME: allocation should not happen here! */
  if(NULL==dev->image) {
    if(allocate_buffers(dev)<0) {
      return -EINVAL;
    }
  }

  if (opener->timeout_image_io) {
    buffer = &dev->timeout_image_buffer;
    addr = (unsigned long) dev->timeout_image;
  } else {
    for (i = 0; i < dev->buffers_number; ++i) {
      buffer = &dev->buffers[i];
      if ((buffer->buffer.m.offset >> PAGE_SHIFT) == vma->vm_pgoff)
        break;
    }

    if(NULL == buffer) {
      return -EINVAL;
    }

    addr = (unsigned long) dev->image + (vma->vm_pgoff << PAGE_SHIFT);
  }

  while (size > 0) {
    struct page *page;

    page = (void *) vmalloc_to_page((void *) addr);

    if (vm_insert_page(vma, start, page) < 0)
      return -EAGAIN;

    start += PAGE_SIZE;
    addr += PAGE_SIZE;
    size -= PAGE_SIZE;
  }

  vma->vm_ops = &vm_ops;
  vma->vm_private_data = buffer;
  buffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED;

  vm_open(vma);

  MARK();
  return 0;
}

static unsigned int
v4l2_loopback_poll  (struct file *file,
                     struct poll_table_struct *pts)
{
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  int ret_mask = 0;
  MARK();

  opener = file->private_data;
  dev    = v4l2loopback_getdevice(file);

  switch (opener->type) {
  case WRITER:
    ret_mask = POLLOUT | POLLWRNORM;
    break;
  case READER:
    poll_wait(file, &dev->read_event, pts);
    if (can_read(dev, opener))
      ret_mask =  POLLIN | POLLRDNORM;
    break;
  default:
    ret_mask = -POLLERR;
  }
  MARK();

  return ret_mask;
}

/* do not want to limit device opens, it can be as many readers as user want,
 * writers are limited by means of setting writer field */
static int
v4l2_loopback_open   (struct file *file)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (dev->open_count.counter >= dev->max_openers)
    return -EBUSY;
  /* kfree on close */
  opener = kzalloc(sizeof(*opener), GFP_KERNEL);
  if (opener == NULL)
    return -ENOMEM;
  file->private_data = opener;
  atomic_inc(&dev->open_count);

  opener->timeout_image_io = dev->timeout_image_io;
  dev->timeout_image_io = 0;

  if (opener->timeout_image_io) {
    int r = allocate_timeout_image(dev);
    if (r < 0) {
      dprintk("timeout image allocation failed\n");
      return r;
    }
  }
  dprintk("opened dev:%p with image:%p", dev, dev?dev->image:NULL);
  MARK();
  return 0;
}

static int
v4l2_loopback_close  (struct file *file)
{
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  MARK();

  opener = file->private_data;
  dev    = v4l2loopback_getdevice(file);

  atomic_dec(&dev->open_count);
  if (dev->open_count.counter == 0) {
    del_timer_sync(&dev->sustain_timer);
    del_timer_sync(&dev->timeout_timer);
  }
  try_free_buffers(dev);
  kfree(opener);
  MARK();
  return 0;
}

static ssize_t
v4l2_loopback_read   (struct file *file,
                      char __user *buf,
                      size_t count,
                      loff_t *ppos)
{
  int read_index;
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  MARK();

  opener = file->private_data;
  dev    = v4l2loopback_getdevice(file);

  read_index = get_capture_buffer(file);
  if (count > dev->buffer_size)
    count = dev->buffer_size;
  if (copy_to_user((void *) buf, (void *) (dev->image +
                                           dev->buffers[read_index].buffer.m.offset), count)) {
    printk(KERN_ERR "v4l2-loopback: "
           "failed copy_from_user() in write buf\n");
    return -EFAULT;
  }
  dprintkrw("leave v4l2_loopback_read()\n");
  return count;
}

static ssize_t
v4l2_loopback_write  (struct file *file,
                      const char __user *buf,
                      size_t count,
                      loff_t *ppos)
{
  struct v4l2_loopback_device *dev;
  int write_index;
  struct v4l2_buffer*b;
  int ret;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (!dev->ready_for_capture) {
    ret = allocate_buffers(dev);
    if (ret < 0)
      return ret;
    dev->ready_for_capture = 1;
  }
  dprintkrw("v4l2_loopback_write() trying to write %zu bytes\n", count);
  if (count > dev->buffer_size)
    count = dev->buffer_size;

  write_index = dev->write_position % dev->used_buffers;
  b=&dev->buffers[write_index].buffer;

  if (copy_from_user(
                     (void *) (dev->image + b->m.offset),
                     (void *) buf, count)) {
    printk(KERN_ERR "v4l2-loopback: "
           "failed copy_from_user() in write buf, could not write %zu\n",
           count);
    return -EFAULT;
  }
  do_gettimeofday(&b->timestamp);
  b->sequence = dev->write_position;
  buffer_written(dev, &dev->buffers[write_index]);
  wake_up_all(&dev->read_event);
  dprintkrw("leave v4l2_loopback_write()\n");
  return count;
}

/* init functions */
/* frees buffers, if already allocated */
static int free_buffers(struct v4l2_loopback_device *dev)
{
  MARK();
  dprintk("freeing image@%p for dev:%p", dev?(dev->image):NULL, dev);
  if(dev->image) {
    vfree(dev->image);
    dev->image=NULL;
  }
  if(dev->timeout_image) {
    vfree(dev->timeout_image);
    dev->timeout_image=NULL;
  }
  dev->imagesize=0;

  return 0;
}
/* frees buffers, if they are no longer needed */
static void
try_free_buffers(struct v4l2_loopback_device *dev)
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
static int
allocate_buffers    (struct v4l2_loopback_device *dev)
{
  MARK();
  /* vfree on close file operation in case no open handles left */
  if (0 == dev->buffer_size)
    return -EINVAL;

  if (dev->image) {
    dprintk("allocating buffers again: %ld %ld", dev->buffer_size * dev->buffers_number, dev->imagesize);
    /* FIXME: prevent double allocation more intelligently! */
    if(dev->buffer_size * dev->buffers_number == dev->imagesize)
      return 0;

    /* if there is only one writer, no problem should occur */
    if (dev->open_count.counter==1)
      free_buffers(dev);
    else
      return -EINVAL;
  }

  dev->imagesize=dev->buffer_size * dev->buffers_number;

  dprintk("allocating %ld = %ldx%d", dev->imagesize, dev->buffer_size, dev->buffers_number);

  dev->image = vmalloc(dev->imagesize);
  if (dev->timeout_jiffies > 0)
    allocate_timeout_image(dev);

  if (dev->image == NULL)
    return -ENOMEM;
  dprintk("vmallocated %ld bytes\n",
          dev->imagesize);
  MARK();
  init_buffers(dev);
  return 0;
}
/* init inner buffers, they are capture mode and flags are set as
 * for capture mod buffers */
static void
init_buffers        (struct v4l2_loopback_device *dev)
{
  int i;
  int buffer_size;
  int bytesused;
  MARK();

  buffer_size=dev->buffer_size;
  bytesused = dev->pix_format.sizeimage;

  for (i = 0; i < dev->buffers_number; ++i) {
    struct v4l2_buffer*b=&dev->buffers[i].buffer;
    b->index             = i;
    b->bytesused         = bytesused;
    b->length            = buffer_size;
    b->field             = V4L2_FIELD_NONE;
    b->flags             = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,1)
    b->input             = 0;
#endif
    b->m.offset          = i * buffer_size;
    b->memory            = V4L2_MEMORY_MMAP;
    b->sequence          = 0;
    b->timestamp.tv_sec  = 0;
    b->timestamp.tv_usec = 0;
    b->type              = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    do_gettimeofday(&b->timestamp);
  }
  dev->timeout_image_buffer = dev->buffers[0];
  dev->timeout_image_buffer.buffer.m.offset = MAX_BUFFERS * buffer_size;
  MARK();
}

static int
allocate_timeout_image(struct v4l2_loopback_device *dev)
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

/* fills and register video device */
static void
init_vdev           (struct video_device *vdev)
{
  MARK();
  strlcpy(vdev->name, "Loopback video device", sizeof(vdev->name));
  vdev->tvnorms      = V4L2_STD_ALL;
  vdev->current_norm = V4L2_STD_ALL;
  vdev->vfl_type     = VFL_TYPE_GRABBER;
  vdev->fops         = &v4l2_loopback_fops;
  vdev->ioctl_ops    = &v4l2_loopback_ioctl_ops;
  vdev->release      = &video_device_release;
  vdev->minor        = -1;
  if (debug > 1)
    vdev->debug = V4L2_DEBUG_IOCTL | V4L2_DEBUG_IOCTL_ARG;
  MARK();
}

/* init default capture parameters, only fps may be changed in future */
static void
init_capture_param  (struct v4l2_captureparm *capture_param)
{
  MARK();
  capture_param->capability               = 0;
  capture_param->capturemode              = 0;
  capture_param->extendedmode             = 0;
  capture_param->readbuffers              = max_buffers;
  capture_param->timeperframe.numerator   = 1;
  capture_param->timeperframe.denominator = 30;
}

static void
check_timers(struct v4l2_loopback_device *dev)
{
  if (!dev->ready_for_capture)
    return;

  if (dev->timeout_jiffies > 0 && !timer_pending(&dev->timeout_timer))
    mod_timer(&dev->timeout_timer, jiffies + dev->timeout_jiffies);
  if (dev->sustain_framerate && !timer_pending(&dev->sustain_timer))
    mod_timer(&dev->sustain_timer, jiffies + dev->frame_jiffies * 3 / 2);
}

static void
sustain_timer_clb(unsigned long nr)
{
  struct v4l2_loopback_device *dev = devs[nr];
  spin_lock(&dev->lock);
  if (dev->sustain_framerate) {
    dev->reread_count++;
    dprintkrw("reread: %d %d", dev->write_position, dev->reread_count);
    if (dev->reread_count == 1)
      mod_timer(&dev->sustain_timer, jiffies + max(1UL, dev->frame_jiffies / 2));
    else
      mod_timer(&dev->sustain_timer, jiffies + dev->frame_jiffies);
    wake_up_all(&dev->read_event);
  }
  spin_unlock(&dev->lock);
}

static void
timeout_timer_clb(unsigned long nr)
{
  struct v4l2_loopback_device *dev = devs[nr];
  spin_lock(&dev->lock);
  if (dev->timeout_jiffies > 0) {
    dev->timeout_happened = 1;
    mod_timer(&dev->timeout_timer, jiffies + dev->timeout_jiffies);
    wake_up_all(&dev->read_event);
  }
  spin_unlock(&dev->lock);
}

/* init loopback main structure */
static int
v4l2_loopback_init  (struct v4l2_loopback_device *dev,
                     int nr)
{
  MARK();
  dev->vdev = video_device_alloc();
  if (dev->vdev == NULL)
    return -ENOMEM;

  video_set_drvdata(dev->vdev, kzalloc(sizeof(struct v4l2loopback_private), GFP_KERNEL));
  if (video_get_drvdata(dev->vdev) == NULL) {
    kfree(dev->vdev);
    return -ENOMEM;
  }
  ((priv_ptr)video_get_drvdata(dev->vdev))->devicenr = nr;

  init_vdev(dev->vdev);
  init_capture_param(&dev->capture_param);
  set_timeperframe(dev, &dev->capture_param.timeperframe);
  dev->keep_format = 0;
  dev->sustain_framerate = 0;
  dev->buffers_number = max_buffers;
  dev->used_buffers = max_buffers;
  dev->max_openers = max_openers;
  dev->write_position = 0;
  INIT_LIST_HEAD(&dev->outbufs_list);
  if (list_empty(&dev->outbufs_list)) {
    int i;
    for (i = 0; i < dev->used_buffers; ++i) {
      list_add_tail(&dev->buffers[i].list_head, &dev->outbufs_list);
    }
  }
  memset(dev->readpos2index, 0, sizeof(dev->readpos2index));
  atomic_set(&dev->open_count, 0);
  dev->ready_for_capture = 0;
  dev->buffer_size = 0;
  dev->image = NULL;
  dev->imagesize = 0;
  setup_timer(&dev->sustain_timer, sustain_timer_clb, nr);
  dev->reread_count = 0;
  setup_timer(&dev->timeout_timer, timeout_timer_clb, nr);
  dev->timeout_jiffies = 0;
  dev->timeout_image = NULL;
  dev->timeout_happened = 0;

  /* FIXME set buffers to 0 */

  init_waitqueue_head(&dev->read_event);

  MARK();
  return 0;
};

/* LINUX KERNEL */
static const struct v4l2_file_operations v4l2_loopback_fops = {
  .owner   = THIS_MODULE,
  .open    = v4l2_loopback_open,
  .release = v4l2_loopback_close,
  .read    = v4l2_loopback_read,
  .write   = v4l2_loopback_write,
  .poll    = v4l2_loopback_poll,
  .mmap    = v4l2_loopback_mmap,
  .unlocked_ioctl   = video_ioctl2,
};

static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops = {
  .vidioc_querycap         = &vidioc_querycap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
  .vidioc_enum_framesizes  = &vidioc_enum_framesizes,
  .vidioc_enum_frameintervals = &vidioc_enum_frameintervals,
#endif

  .vidioc_queryctrl         = &vidioc_queryctrl,
  .vidioc_g_ctrl            = &vidioc_g_ctrl,
  .vidioc_s_ctrl            = &vidioc_s_ctrl,

  .vidioc_enum_output       = &vidioc_enum_output,
  .vidioc_g_output          = &vidioc_g_output,
  .vidioc_s_output          = &vidioc_s_output,

  .vidioc_enum_input       = &vidioc_enum_input,
  .vidioc_g_input          = &vidioc_g_input,
  .vidioc_s_input          = &vidioc_s_input,

  .vidioc_enum_fmt_vid_cap = &vidioc_enum_fmt_cap,
  .vidioc_g_fmt_vid_cap    = &vidioc_g_fmt_cap,
  .vidioc_s_fmt_vid_cap    = &vidioc_s_fmt_cap,
  .vidioc_try_fmt_vid_cap  = &vidioc_try_fmt_cap,

  .vidioc_enum_fmt_vid_out = &vidioc_enum_fmt_out,
  .vidioc_s_fmt_vid_out    = &vidioc_s_fmt_out,
  .vidioc_g_fmt_vid_out    = &vidioc_g_fmt_out,
  .vidioc_try_fmt_vid_out  = &vidioc_try_fmt_out,

#ifdef V4L2L_OVERLAY
  .vidioc_s_fmt_vid_overlay= &vidioc_s_fmt_overlay,
  .vidioc_g_fmt_vid_overlay= &vidioc_g_fmt_overlay,
#endif

  .vidioc_s_std            = &vidioc_s_std,
  .vidioc_g_std            = &vidioc_g_std,
  .vidioc_querystd         = &vidioc_querystd,

  .vidioc_g_parm           = &vidioc_g_parm,
  .vidioc_s_parm           = &vidioc_s_parm,

  .vidioc_reqbufs          = &vidioc_reqbufs,
  .vidioc_querybuf         = &vidioc_querybuf,
  .vidioc_qbuf             = &vidioc_qbuf,
  .vidioc_dqbuf            = &vidioc_dqbuf,

  .vidioc_streamon         = &vidioc_streamon,
  .vidioc_streamoff        = &vidioc_streamoff,

#ifdef CONFIG_VIDEO_V4L1_COMPAT
  .vidiocgmbuf             = &vidiocgmbuf,
#endif

  .vidioc_g_audio          = &vidioc_g_audio,
  .vidioc_s_audio          = &vidioc_s_audio,
  .vidioc_g_audout         = &vidioc_g_audout,
  .vidioc_s_audout         = &vidioc_s_audout,
};

static void
zero_devices        (void)
{
  int i;
  MARK();
  for(i=0; i<MAX_DEVICES; i++) {
    devs[i]=NULL;
  }
}

static void
free_devices        (void)
{
  int i;
  MARK();
  for(i=0; i<devices; i++) {
    if(NULL!=devs[i]) {
      free_buffers(devs[i]);
      v4l2loopback_remove_sysfs(devs[i]->vdev);
      kfree(video_get_drvdata(devs[i]->vdev));
      video_unregister_device(devs[i]->vdev);
      kfree(devs[i]);
      devs[i]=NULL;
    }
  }
}

int __init
init_module         (void)
{
  int ret;
  int i;
  MARK();

  zero_devices();
  if (devices < 0) {
    devices = 1;

    // try guessing the devices from the "video_nr" parameter
    for(i=MAX_DEVICES-1; i>=0; i--) {
      if(video_nr[i]>=0) {
        devices=i+1;
        break;
      }
    }
  }

  if (devices > MAX_DEVICES) {
    devices = MAX_DEVICES;
    printk(KERN_INFO "v4l2loopback: number of devices is limited to: %d\n", MAX_DEVICES);
  }

  if (max_buffers > MAX_BUFFERS) {
    max_buffers=MAX_BUFFERS;
    printk(KERN_INFO "v4l2loopback: number of buffers is limited to: %d\n", MAX_BUFFERS);
  }

  if (max_openers < 0) {
    printk(KERN_INFO "v4l2loopback: allowing %d openers rather than %d\n", 2, max_openers);
    max_openers=2;
  }

  if (max_width < 1) {
    max_width = V4L2LOOPBACK_SIZE_MAX_WIDTH;
    printk(KERN_INFO "v4l2loopback: using max_width %d\n", max_width);
  }
  if (max_height < 1) {
    max_height = V4L2LOOPBACK_SIZE_MAX_HEIGHT;
    printk(KERN_INFO "v4l2loopback: using max_height %d\n", max_height);
  }

  /* kfree on module release */
  for(i=0; i<devices; i++) {
    dprintk("creating v4l2loopback-device #%d\n", i);
    devs[i] = kzalloc(sizeof(*devs[i]), GFP_KERNEL);
    if (devs[i] == NULL) {
      free_devices();
      return -ENOMEM;
    }
    ret = v4l2_loopback_init(devs[i], i);
    if (ret < 0) {
      free_devices();
      return ret;
    }
    /* register the device -> it creates /dev/video* */
    if (video_register_device(devs[i]->vdev, VFL_TYPE_GRABBER, video_nr[i]) < 0) {
      video_device_release(devs[i]->vdev);
      printk(KERN_ERR "v4l2loopback: failed video_register_device()\n");
      free_devices();
      return -EFAULT;
    }
    v4l2loopback_create_sysfs(devs[i]->vdev);
  }


  dprintk("module installed\n");

  printk(KERN_INFO "v4l2loopback driver version %d.%d.%d loaded\n",
         (V4L2LOOPBACK_VERSION_CODE >> 16) & 0xff,
         (V4L2LOOPBACK_VERSION_CODE >>  8) & 0xff,
         (V4L2LOOPBACK_VERSION_CODE      ) & 0xff);

  return 0;
}

void __exit
cleanup_module      (void)
{
  MARK();
  /* unregister the device -> it deletes /dev/video* */
  free_devices();
  dprintk("module removed\n");
}


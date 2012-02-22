/*
 * v4l2loopback.c  --  video4linux2 loopback driver
 *
 * Copyright (C) 2005-2009 Vasily Levin (vasaka@gmail.com)
 * Copyright (C) 2010-2011 IOhannes m zmoelnig (zmoelnig@iem.at)
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
#include <linux/mutex.h>
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

#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION(0,5,0)


MODULE_DESCRIPTION("V4L2 loopback video device");
MODULE_AUTHOR("Vasily Levin, IOhannes m zmoelnig <zmoelnig@iem.at>, Stefan Diewald <stefan.diewald@mytum.de>");
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

#define MS_TO_NS(ms) (ms * 1000000LL)

/* module constants */
#define PLACEHOLDER_FRAME 1

/* module parameters */
static int debug = 0;
module_param(debug, int, S_IRUGO|S_IWUSR );
MODULE_PARM_DESC(debug, "if debug output is enabled, values are 0, 1 or 2");

#define MAX_BUFFERS 32  /* max buffers that can be mapped, actually they
                         * are all mapped to max_buffers buffers */
#define DEFAULT_MAX_BUFFERS 8
static uint max_buffers = DEFAULT_MAX_BUFFERS;
module_param(max_buffers, uint, S_IRUGO);
MODULE_PARM_DESC(max_buffers, "how many buffers should be allocated");

/* how many times a device can be opened
 * the per-module default value can be overridden on a per-device basis using
 * the /sys/devices interface
 *
 * note that max_openers should be at least 2 in order to get a working system:
 *   one opener for the producer and one opener for the consumer
 *   however, we leave that to the user
 */
#define DEFAULT_MAX_OPENERS 10
static int max_openers = DEFAULT_MAX_OPENERS;
module_param(max_openers, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(max_openers, "how many users can open loopback device");

#define DEFAULT_FPS 30
static uint fps = DEFAULT_FPS;
module_param(fps, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fps, "frames per second");

/* negative timeout is infinite */
#define DEFAULT_TIMEOUT -1
static int timeout = DEFAULT_TIMEOUT;
module_param(timeout, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(timeout, "the delay before starting producing placeholder frames, in ms");


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
  int use_count;
};

struct v4l2_loopback_device {
  struct video_device *vdev;
  /* pixel and stream format */
  struct v4l2_pix_format pix_format;
  struct v4l2_captureparm capture_param;
  int keep_format; /* if device should sustain negotiated format even when all openers close() the device */

  /* buffers stuff */
  u8 *image;         /* pointer to actual buffers data */
  unsigned long int imagesize;  /* size of buffers data */
  int buffers_number;  /* should not be big, 4 is a good choice */
  struct v4l2l_buffer buffers[MAX_BUFFERS];	/* inner driver buffers */
  int used_buffers; /* number of the actually used buffers */
  uint max_openers;  /* how many times can this device be opened */

  struct mutex write_mutex;
  struct timeval last_write_timestamp;
  int write_position; /* number of last written frame + 1 */
  long buffer_size;
  int timeout;
  u8 *placeholder_frame;
  int idle_frame_needed;
  struct timer_list idle_frame_timer;
  struct v4l2_fract idle_timeperframe;

  /* sync stuff */
  atomic_t open_count;
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
  int vidioc_enum_frameintervals_calls;
  int read_position; /* number of last processed frame + 1 or
                      * write_position - 1 if reader went out of sync */
  struct v4l2_buffer *buffers;
  int buffers_number;  /* should not be big, 4 is a good choice */
};

/* this is heavily inspired by the bttv driver found in the linux kernel */
struct v4l2l_format {
  char *name;
  int  fourcc;          /* video4linux 2      */
  int  depth;           /* bit/pixel          */
  int  flags;
};


/* forward declarations */
static int get_capture_buffer(struct v4l2_loopback_device *dev,
                              struct v4l2_loopback_opener *opener,
                              struct file *file);
static void init_buffers(struct v4l2_loopback_device *dev);
static int allocate_buffers(struct v4l2_loopback_device *dev);
static int free_buffers(struct v4l2_loopback_device *dev);
static int free_buffers_if_needed(struct v4l2_loopback_device *dev);
static void schedule_idle_frame(struct v4l2_loopback_device *dev);
static const struct v4l2_file_operations v4l2_loopback_fops;
static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops;


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

static int str2fps(const char*buffer, size_t len, int*numerator, int*denominator) {
  int res=0;
  int num=1;
  int denom=1;
  res=sscanf(buffer, "%10d/%10d", &num, &denom);

    /* 100/4 -> 25fps
     * 55    -> 55fps
     */
  if(1!=res && 2!=res)
    return 0;

  /* invalid fps */
  if(num<1 || denom<0)
    return 0;

  /* all is good, return values */
  if(numerator)
    *numerator=num;
  if(denominator)
    *denominator=denom;

  return len;
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

static struct v4l2_loopback_device*v4l2loopback_cd2dev  (struct device*cd);

/* device attributes */
/* available via sysfs: /sys/devices/virtual/video4linux/video* */

static ssize_t attr_show_fourcc(struct device *cd,
                         struct device_attribute *attr,
                         char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);

  char buf4cc[5];
  if(!dev)return 0;

  fourcc2str(dev->pix_format.pixelformat, buf4cc);

  return sprintf(buf, "%.*s\n", 4, buf4cc);
}
static DEVICE_ATTR(fourcc, S_IRUGO, attr_show_fourcc, NULL);

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

static ssize_t attr_show_fps(struct device *cd,
                                 struct device_attribute *attr,
                                 char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d/%d\n",
                 dev->capture_param.timeperframe.denominator,
                 dev->capture_param.timeperframe.numerator);
}
static ssize_t attr_store_fps(struct device* cd,
                                  struct device_attribute *attr,
                                  const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = NULL;
  int fps_denominator=1, fps_numerator=1;
  int count=0;
  double fps=0.;

  count=str2fps(buf, len, &fps_numerator, &fps_denominator);

  if(!count)
    return -EINVAL;

  /* check the resulting fps */
  fps=((double)fps_numerator)/((double)fps_denominator);
  if(fps>1000. || fps<0.) {
    /* something insane */
    return -EINVAL;
  }
  dev = v4l2loopback_cd2dev(cd);

  if(dev->capture_param.timeperframe.denominator==fps_numerator &&
     dev->capture_param.timeperframe.numerator==fps_denominator)
    return len;

  dev->capture_param.timeperframe.denominator=fps_numerator;
  dev->capture_param.timeperframe.numerator=fps_denominator;

  return len;
}
static DEVICE_ATTR(fps, S_IRUGO | S_IWUSR, attr_show_fps, attr_store_fps);

static ssize_t attr_show_idlefps(struct device *cd,
                                 struct device_attribute *attr,
                                 char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  if(!dev->idle_timeperframe.denominator)return 0;

  return sprintf(buf, "%d/%d\n",
                 dev->idle_timeperframe.denominator,
                 dev->idle_timeperframe.numerator);
}
static ssize_t attr_store_idlefps(struct device* cd,
                                  struct device_attribute *attr,
                                  const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);

  int idlefps_denominator=1, idlefps_numerator=1;
  int count=0;
  double idlefps=0.;

  /* reset to defaults */
  dev->idle_timeperframe.denominator=0;
  dev->idle_timeperframe.numerator=0;

  count=str2fps(buf, len, &idlefps_numerator, &idlefps_denominator);

  if(!count)
    return len;

  /* check the resulting idlefps */
  idlefps=((double)idlefps_numerator)/((double)idlefps_denominator);
  if(idlefps>1000. || idlefps<0.) {
    /* something insane */
    return len;
  }

  /* user passed a valid fps; use it rather than the default */

  if(dev->idle_timeperframe.denominator==idlefps_numerator &&
     dev->idle_timeperframe.numerator==idlefps_denominator)
    return len;

  dev->idle_timeperframe.denominator=idlefps_numerator;
  dev->idle_timeperframe.numerator=idlefps_denominator;

  return len;
}
static DEVICE_ATTR(idle_fps, S_IRUGO | S_IWUSR, attr_show_idlefps, attr_store_idlefps);

static ssize_t attr_show_keepformat(struct device *cd,
                                   struct device_attribute *attr,
                                   char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->keep_format);
}
static ssize_t attr_store_keepformat(struct device* cd,
                                    struct device_attribute *attr,
                                    const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = NULL;
  unsigned long curr=0;

  if (strict_strtol(buf, 0, &curr))
    return -EINVAL;

  curr=(curr==1);

  dev = v4l2loopback_cd2dev(cd);

  if (dev->keep_format == curr)
    return len;

  dev->keep_format = (int)curr;
  free_buffers_if_needed(dev);
  return len;
}
static DEVICE_ATTR(keep_format, S_IRUGO | S_IWUSR, attr_show_keepformat, attr_store_keepformat);

static ssize_t attr_show_timeout(struct device *cd,
                                 struct device_attribute *attr,
                                 char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->timeout);
}
static ssize_t attr_store_timeout(struct device* cd,
                                           struct device_attribute *attr,
                                           const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = NULL;
  long curr=0;

  if (strict_strtol(buf, 0, &curr))
    return -EINVAL;

  if(curr<0)
    curr=-1;

  dev = v4l2loopback_cd2dev(cd);

  if (dev->timeout == curr)
    return len;

  dev->timeout = (int)curr;
  return len;
}
static DEVICE_ATTR(timeout, S_IRUGO | S_IWUSR,
                   attr_show_timeout, attr_store_timeout);

static ssize_t attr_show_maxbuffers(struct device *cd,
                                    struct device_attribute *attr,
                                    char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->buffers_number);
}
static DEVICE_ATTR(max_buffers, S_IRUGO, attr_show_maxbuffers, NULL);

static ssize_t attr_show_buffersize(struct device *cd,
                                    struct device_attribute *attr,
                                    char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%ld\n", dev->buffer_size);
}
static DEVICE_ATTR(buffer_size, S_IRUGO, attr_show_buffersize, NULL);


static void v4l2loopback_remove_sysfs(struct video_device *vdev)
{
#define V4L2_SYSFS_DESTROY(x) device_remove_file(&vdev->dev, &dev_attr_##x)

  if (vdev) {
    V4L2_SYSFS_DESTROY(fourcc);
    V4L2_SYSFS_DESTROY(keep_format);
    V4L2_SYSFS_DESTROY(buffers);
    V4L2_SYSFS_DESTROY(max_openers);
    V4L2_SYSFS_DESTROY(fps);
    V4L2_SYSFS_DESTROY(timeout);
    V4L2_SYSFS_DESTROY(idle_fps);
    V4L2_SYSFS_DESTROY(max_buffers);
    V4L2_SYSFS_DESTROY(buffer_size);
    /* ... */
  }
}
static void v4l2loopback_create_sysfs(struct video_device *vdev)
{
  int res=0;
#define V4L2_SYSFS_CREATE(x)     res = device_create_file(&vdev->dev, &dev_attr_##x); if (res < 0) break
  if (!vdev) return;
  do {
    V4L2_SYSFS_CREATE(fourcc);
    V4L2_SYSFS_CREATE(keep_format);
    V4L2_SYSFS_CREATE(buffers);
    V4L2_SYSFS_CREATE(max_openers);
    V4L2_SYSFS_CREATE(fps);
    V4L2_SYSFS_CREATE(timeout);
    V4L2_SYSFS_CREATE(idle_fps);
    V4L2_SYSFS_CREATE(max_buffers);
    V4L2_SYSFS_CREATE(buffer_size);
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

    argp->stepwise.max_width=V4L2LOOPBACK_SIZE_MAX_WIDTH;
    argp->stepwise.max_height=V4L2LOOPBACK_SIZE_MAX_HEIGHT;

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
    dev->pix_format.sizeimage = fmt->fmt.pix.sizeimage;

    dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
    dprintkrw("buffer_size: %u -> %ld", dev->pix_format.sizeimage, dev->buffer_size);
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
    dprintkrw("buffer_size: %u -> %ld", dev->pix_format.sizeimage, dev->buffer_size);
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
    dev->capture_param.timeperframe=parm->parm.capture.timeperframe;
    break;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    dev->capture_param.timeperframe=parm->parm.capture.timeperframe;
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


/* dummy queyr control
 * called on VIDIOC_QUERYCTRL
 */
static int
vidioc_queryctrl(struct file *file, void *fh,
                 struct v4l2_queryctrl *a)
{
  return -EINVAL;
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
  allocate_buffers(dev);
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
  MARK();

  type = b->type;
  index = b->index;
  dev=v4l2loopback_getdevice(file);

  if ((b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
      (b->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)) {
    return -EINVAL;
  }
  if (b->index > max_buffers)
    return -EINVAL;

  *b = dev->buffers[b->index % dev->used_buffers].buffer;

  b->type = type;
  b->index = index;
  dprintkrw("buffer type: %d (of %d with size=%ld)", b->memory, dev->buffers_number, dev->buffer_size);
  return 0;
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
  struct v4l2l_buffer *b;
  int index;

  dev=v4l2loopback_getdevice(file);

  if (buf->index > max_buffers)
    return -EINVAL;

  index = buf->index % dev->used_buffers;
  b=&dev->buffers[index];

  switch (buf->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    dprintkrw("capture QBUF index: %d\n", index);
    set_queued(b);
    return 0;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    dprintkrw("output QBUF index: %d\n", index);
    do_gettimeofday(&b->buffer.timestamp);
    dev->last_write_timestamp = b->buffer.timestamp;
    set_done(b);
    wake_up_all(&dev->read_event);
    schedule_idle_frame(dev);
    return 0;
  default:
    return -EINVAL;
  }
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

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  switch (buf->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    index = get_capture_buffer(dev, opener, file);
    if (index < 0)
      return -EAGAIN;
    dprintkrw("capture DQBUF index: %d\n", index);
    if (!(dev->buffers[index].buffer.flags&V4L2_BUF_FLAG_MAPPED)) {
      dprintk("trying to return not mapped buf\n");
      return -EINVAL;
    }
    ++opener->read_position;
    unset_flags(&dev->buffers[index]);
    *buf = dev->buffers[index].buffer;
    dprintkrw("buffer out: %d %d %d %ld %ld",
              index,
              opener->read_position - 1,
              dev->write_position,
              (long)buf->timestamp.tv_sec,
              (long)buf->timestamp.tv_usec);
    return 0;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    if (mutex_lock_interruptible(&dev->write_mutex) < 0)
      return -EAGAIN;
    index = dev->write_position % dev->used_buffers;
    dprintkrw("output DQBUF index: %d\n", index);
    unset_flags(&dev->buffers[index]);
    *buf = dev->buffers[index].buffer;
    buf->sequence = dev->write_position++;
    dev->idle_frame_needed = 0;
    mutex_unlock(&dev->write_mutex);
    return 0;
  default:
    dprintk("trying to dequeue buffer of unknown type: %x\n", buf->type);
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

  buf = vma->vm_private_data;
  if (buf != NULL)
    buf->use_count++;
}

static void
vm_close            (struct vm_area_struct *vma)
{
  struct v4l2l_buffer *buf;
  MARK();

  buf = vma->vm_private_data;
  if (buf != NULL)
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
  u8 *addr;
  unsigned long start;
  unsigned long size;
  unsigned long offset;
  unsigned long buf;
  struct v4l2_loopback_device *dev;
  struct v4l2l_buffer *buffer;
  MARK();

  dev = v4l2loopback_getdevice(file);

  start = (unsigned long) vma->vm_start;
  size = (unsigned long) (vma->vm_end - vma->vm_start);
  offset = vma->vm_pgoff << PAGE_SHIFT;
  addr = dev->image + offset;
  buf = offset / dev->buffer_size;

  if (offset != buf * dev->buffer_size) {
    dprintk("userspace tries to mmap the middle of frame, fail\n");
    return -EINVAL;
  }
  if (size > dev->buffer_size) {
    dprintk("userspace tries to mmap too much, fail\n");
    return -EINVAL;
  }

  /* FIXXXXXME: allocation should not happen here! */
  if(NULL==dev->image) {
    if(allocate_buffers(dev)<0) {
      return -EINVAL;
    }
  }

  if (buf < dev->buffers_number) {
    buffer = &dev->buffers[buf];
    if (offset != buffer->buffer.m.offset) {
      dprintk("assertion failed; something's wrong\n");
      return -EFAULT;
    }
  } else if (buf == dev->buffers_number) {
    /* placeholder frame */
    buffer = NULL;
    if (offset != dev->placeholder_frame - dev->image) {
      dprintk("assertion failed; something's wrong\n");
      return -EFAULT;
    }
  } else {
    dprintk("userspace tries to mmap too far, fail\n");
    return -EINVAL;
  }

  while (size > 0) {
    struct page *page;

    page = vmalloc_to_page(addr);
    if (vm_insert_page(vma, start, page) < 0)
      return -EAGAIN;

    start += PAGE_SIZE;
    addr += PAGE_SIZE;
    size -= PAGE_SIZE;
  }

  vma->vm_ops = &vm_ops;
  vma->vm_private_data = buffer;
  if (buffer != NULL)
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
    if (dev->write_position > opener->read_position || dev->idle_frame_needed)
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
  free_buffers_if_needed(dev);
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

  read_index = get_capture_buffer(dev, opener, file);
  if (read_index < 0)
    return -EAGAIN;
  if (count > dev->buffer_size)
    count = dev->buffer_size;
  if (copy_to_user((void *) buf, (void *) (dev->image +
                                           dev->buffers[read_index].buffer.m.offset), count)) {
    printk(KERN_ERR "v4l2-loopback: "
           "failed copy_from_user() in write buf\n");
    return -EFAULT;
  }
  ++opener->read_position;
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

  if (mutex_lock_interruptible(&dev->write_mutex) < 0)
    return -EAGAIN;
  write_index = dev->write_position % dev->used_buffers;
  b=&dev->buffers[write_index].buffer;

  if (copy_from_user(
                     (void *) (dev->image + b->m.offset),
                     (void *) buf, count)) {
    printk(KERN_ERR "v4l2-loopback: "
           "failed copy_from_user() in write buf, could not write %zu\n",
           count);
    mutex_unlock(&dev->write_mutex);
    return -EFAULT;
  }
  do_gettimeofday(&b->timestamp);
  dev->last_write_timestamp = b->timestamp;
  b->sequence = dev->write_position++;
  dev->idle_frame_needed = 0;
  mutex_unlock(&dev->write_mutex);
  wake_up_all(&dev->read_event);
  dprintkrw("leave v4l2_loopback_write()\n");
  return count;
}

static void
generate_idle_frame(struct v4l2_loopback_device *dev)
{
  struct v4l2l_buffer *src, *dst;
  s64 idle_ns;
  struct timeval idle_time;
  u8 *frame;

  MARK();
  if (!dev->idle_frame_needed)
    dprintk("!dev->idle_frame_needed; shoudn't happen");
  src = &dev->buffers[dev->write_position % dev->used_buffers];
  dst = &dev->buffers[(dev->write_position + 1) % dev->used_buffers];
  if (!(dst->buffer.flags & V4L2_BUF_FLAG_QUEUED))
    dprintk("destination buffer not queued; will cross fingers and use it anyway");
  do_gettimeofday(&dst->buffer.timestamp);
  idle_ns = timeval_to_ns(&dst->buffer.timestamp) - timeval_to_ns(&dev->last_write_timestamp);
  idle_time = ns_to_timeval(idle_ns);
  dprintkrw("generate_idle_frame %ld %ld",
            (long)idle_time.tv_sec,
            (long)idle_time.tv_usec);

  frame = (dev->timeout >= 0 && idle_ns > MS_TO_NS(dev->timeout)) ?
          dev->placeholder_frame :
          (dev->image + src->buffer.m.offset);
  memcpy(dev->image + dst->buffer.m.offset, frame, dev->buffer_size);
  set_done(dst);
  dev->idle_frame_needed = 0;
  dev->write_position++;
}

static int
get_capture_buffer(struct v4l2_loopback_device *dev,
                   struct v4l2_loopback_opener *opener,
                   struct file *file)
{
  if ((dev->write_position <= opener->read_position) &&
      (file->f_flags&O_NONBLOCK))
    return -EAGAIN;
  if (wait_event_interruptible(
          dev->read_event,
          (dev->write_position > opener->read_position) || dev->idle_frame_needed) < 0)
    return -EAGAIN;
  if (dev->write_position == opener->read_position) {
    if (mutex_lock_interruptible(&dev->write_mutex) < 0)
      return -EAGAIN;
    /* check again */
    if (dev->write_position == opener->read_position) {
      generate_idle_frame(dev);
    }
    mutex_unlock(&dev->write_mutex);
  }
  if (dev->write_position > opener->read_position+2)
    opener->read_position = dev->write_position - 1;
  return opener->read_position % dev->used_buffers;
}

static void schedule_idle_frame(struct v4l2_loopback_device *dev)
{
  long frame_jiffies = 0;

  /* if the user has set a valid idle_fps (denominator!=0),
   * we use that for scheduling the next idle frame;
   * if they haven't done so, we use fps/2 by default
   */
  __u32 num=2*dev->capture_param.timeperframe.numerator;
  __u32 denom=dev->capture_param.timeperframe.denominator;

  if(dev->idle_timeperframe.denominator) {
    num=dev->idle_timeperframe.numerator;
    denom=dev->idle_timeperframe.denominator;
  }

  frame_jiffies = (msecs_to_jiffies(1000) * num) / denom;

  if (frame_jiffies > 0) {
    mod_timer(&dev->idle_frame_timer, jiffies + frame_jiffies);
  } else {
    del_timer(&dev->idle_frame_timer);
  }
}

static void idle_frame_callback(unsigned long nr)
{
  struct v4l2_loopback_device *dev;

  dev = devs[nr];
  dprintkrw("idle frame: %d", dev->write_position);
  dev->idle_frame_needed = 1;
  wake_up_all(&dev->read_event);
  schedule_idle_frame(dev);
}

/* init functions */
/* frees buffers, if already allocated */
static int free_buffers(struct v4l2_loopback_device *dev)
{
  dprintk("freeing %p -> %p", dev, dev->image);
  if(dev->image) {
    vfree(dev->image);
    dev->image=NULL;
    dev->placeholder_frame = NULL;
  }
  dev->imagesize=0;

  return 0;
}

static int free_buffers_if_needed(struct v4l2_loopback_device *dev)
{
  if (0 == dev->open_count.counter) {
    del_timer(&dev->idle_frame_timer);

    if(!dev->keep_format) {
      dev->ready_for_capture = 0;
      dev->buffer_size = 0;
      return free_buffers(dev);
    }
  }
  return 0;
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
    if(dev->buffer_size * (dev->buffers_number + PLACEHOLDER_FRAME) == dev->imagesize)
      return 0;

    /* if there is only one writer, no problem should occur */
    if (dev->open_count.counter==1)
      free_buffers(dev);
    else
      return -EINVAL;
  }

  dev->imagesize=dev->buffer_size * (dev->buffers_number + PLACEHOLDER_FRAME);
  dprintkrw("vmallocating %ld*%d bytes\n", dev->buffer_size, (dev->buffers_number + PLACEHOLDER_FRAME));
  dev->image = vmalloc(dev->imagesize);

  if (dev->image == NULL)
    return -ENOMEM;
  dprintk("vmallocated %ld bytes\n",
          dev->imagesize);
  memset(dev->image, 0, dev->imagesize);
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
    b->input             = 0;
    b->m.offset          = i * buffer_size;
    b->memory            = V4L2_MEMORY_MMAP;
    b->sequence          = 0;
    b->timestamp.tv_sec  = 0;
    b->timestamp.tv_usec = 0;
    b->type              = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    do_gettimeofday(&b->timestamp);
  }
  dev->placeholder_frame = dev->image + dev->buffers_number * buffer_size;
  dev->write_position = 0;
  MARK();
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
  capture_param->capability               = 0;
  capture_param->capturemode              = 0;
  capture_param->extendedmode             = 0;
  capture_param->readbuffers              = max_buffers;
  if(fps>0) {
    capture_param->timeperframe.numerator   = 1;
    capture_param->timeperframe.denominator = fps;
  } else {
    capture_param->timeperframe.numerator   = 1;
    capture_param->timeperframe.denominator = 30;
  }
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
  dev->buffers_number = max_buffers;
  dev->used_buffers = max_buffers;
  dev->max_openers = max_openers;
  atomic_set(&dev->open_count, 0);
  dev->ready_for_capture = 0;
  dev->buffer_size = 0;
  dev->image = NULL;
  dev->imagesize = 0;
  dev->timeout = timeout;
  mutex_init(&dev->write_mutex);
  setup_timer(&dev->idle_frame_timer, idle_frame_callback, nr);

  dev->idle_timeperframe.numerator=0;
  dev->idle_timeperframe.denominator=0;

  /* FIXME set buffers to 0 */

  init_waitqueue_head(&dev->read_event);
  return 0;
};

static void
v4l2_loopback_free(struct v4l2_loopback_device *dev)
{
  del_timer_sync(&dev->idle_frame_timer);
  v4l2loopback_remove_sysfs(dev->vdev);
  kfree(video_get_drvdata(dev->vdev));
  video_unregister_device(dev->vdev);
  kfree(dev);
}

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
  for(i=0; i<MAX_DEVICES; i++) {
    devs[i]=NULL;
  }
}

static void
free_devices        (void)
{
  int i;
  for(i=0; i<devices; i++) {
    if(NULL!=devs[i]) {
      v4l2_loopback_free(devs[i]);
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
    printk(KERN_INFO "v4l2loopback: allowing %d openers rather than %d\n", DEFAULT_MAX_OPENERS, max_openers);
    max_openers=DEFAULT_MAX_OPENERS;
  }

  if (fps < 0) {
    printk(KERN_INFO "v4l2loopback: setting fps to %d/1 rather than %d/1\n", DEFAULT_FPS, fps);
    fps=DEFAULT_FPS;
  }

  if (timeout < 0) {
    timeout=-1;
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

/* vim: set ts=2 sw=2: */

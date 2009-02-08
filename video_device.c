/*
AVLD - Another Video Loopback Device

Version : 0.1.4

AVLD is a V4L dummy video device built to simulate an input video device like a webcam or a video
capture card. You just have to send the video stream on it (using, for instance, mplayer or ffmpeg),
that's all. Then, you can use this device by watching the video on it with your favorite video player. But,
one of the most useful interest, is obviously to use it with a VideoConferencing software to show a video
over internet. According to the software you are using, you could also be able to capture your screen in
realtime. A third interest, and maybe not the last one, could be to use it with an image processing (or
other) software which has been designed to use a video device as input.

(c)2008 Pierre PARENT - allonlinux@free.fr

Distributed according to the GPL.
*/
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <media/v4l2-ioctl.h>
#endif

#define DEBUG
//#define DEBUG_OUT
//#define DEBUG_RW

#define KERNEL_PREFIX "AVLD device: " /* Prefix of each kernel message */
#define VID_DUMMY_DEVICE 0 /* The type of device we create */

#define BUFFER_SIZE_MACRO	(width*height*depth) /* compute buffer size to allocate */


/* The device can't be used by more than 2 applications ( typically : a read and a write application )
Decrease this number if you want it to be accessible for more applications.*/
static int usage = -1;
/* read increases frame_counter, wrie decreases it */
static unsigned int frame_counter = 0;
DECLARE_WAIT_QUEUE_HEAD(read_event);


/* modifiable video options */
static int fps = 15;
static struct v4l2_pix_format video_format;

/* Frame specifications and options*/
static __u8 *image = NULL;
enum v4l2_memory  buffer_alloc_type;

static long BUFFER_SIZE = 0;

/****************************************************************
**************** V4L2 ioctl caps and params calls ***************
****************************************************************/
/******************************************************************************/
/* returns device capabilities called on VIDIOC_QUERYCAP ioctl*/
static int vidioc_querycap(struct file *file, 
                           void  *priv,
                           struct v4l2_capability *cap) {
  strcpy(cap->driver, "v4l2 loopback");
  strcpy(cap->card, "Dummy video device");
  cap->version = 1;
  cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
                      V4L2_CAP_READWRITE;/* | V4L2_CAP_STREAMING; no streaming yet*/
  return 0;
}
/******************************************************************************/
/* returns device formats called on VIDIOC_ENUM_FMT ioctl*/
int vidioc_enum_fmt_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f) {
  if (f->index)
    return -EINVAL;
  strcpy(f->description, "my only format");
  f->pixelformat = video_format.pixelformat;
  return 0;
};
/******************************************************************************/
/* returns current video format format fmt, called on VIDIOC_G_FMT ioctl */
 static int vidioc_g_fmt_cap(struct file *file, 
                               void *priv,
                               struct v4l2_format *fmt) {
  fmt->fmt.pix = video_format;
  return 0; 
}
/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl 
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
/* actual check is done by inner_try_fmt_cap */
 static int vidioc_try_fmt_cap(struct file *file, 
                               void *priv,
                               struct v4l2_format *fmt) {
   /* TODO(vasaka) maybe should add sanity checks here */
  fmt->fmt.pix = video_format;
  return 0; 
}
/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */ 
static int vidioc_try_fmt_video_output(struct file *file, 
                                       void *priv, 
                                       struct v4l2_format *fmt) {
  /* TODO(vasaka) maybe should add sanity checks here */
  fmt->fmt.pix = video_format;
  return 0;
};
/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
static int vidioc_s_fmt_cap(struct file *file, 
                            void *priv,
                            struct v4l2_format *fmt) {
 /* TODO(vasaka) add check if it is OK to change format now */
	if (0)
	    return -EBUSY;
/* check if requsted format is OK */
/*actually format is set  by input and we only check that format is not changed, 
 but it is possible to set subregion of input to return to client TODO(vasaka)*/
	return vidioc_try_fmt_cap(file, priv, fmt);
}
/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */ 
static int vidioc_s_fmt_video_output(struct file *file, 
                                     void *priv, 
                                     struct v4l2_format *fmt) {
 /* TODO(vasaka) add check if it is OK to change format now */
	if (0)
	    return -EBUSY;  
	return vidioc_try_fmt_video_output(file, priv, fmt);
}
/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver called on VIDIOC_G_PARM*/
int vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *parm) {
  struct v4l2_captureparm *cparm = &parm->parm.capture;
  struct v4l2_outputparm  *oparm = &parm->parm.output;
  switch (parm->type)
  {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    {
      /* TODO(VASAKA) restore from saved capture mode, remove hardcoded fps */
      cparm->timeperframe.denominator = fps;
      cparm->timeperframe.numerator = 1;
      return 0;
    }
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    {
      oparm->timeperframe.denominator = fps;
      oparm->timeperframe.numerator = 1;
      return 0;
    }
    default:
      return -1;
  }
  return 0;
}
/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver. called on VIDIOC_G_PARM */
int vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *parm) {
  struct v4l2_captureparm *cparm = &parm->parm.capture;
  struct v4l2_outputparm  *oparm = &parm->parm.output;
	#ifdef DEBUG
		printk(KERNEL_PREFIX "vidioc_s_parm called frate=%d/%d\n", 
           oparm->timeperframe.numerator, 
           oparm->timeperframe.denominator);
	#endif    
  switch (parm->type)
  {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    {
      /* TODO(VASAKA) saved capture mode */
      return 0;
    }
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    {
      return 0;
    }
    default:
      return -1;
  }
  return 0;
}
/* returns set of device inputs, in our case there is only one, but later I may
 * add more, called on VIDIOC_ENUMINPUT */
int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *inp) {
  if (inp->index==0) 
  {
    strcpy(inp->name,"loopback");
    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->audioset = 0;
    inp->tuner = 0;
    inp->std = V4L2_STD_PAL_B;
    inp->status = 0;    
    return 0;
  }
  return -EINVAL;
}
/* VIDIOC_G_INPUT */
int vidioc_g_input(struct file *file, void *fh, unsigned int *i) {
  *i = 0;
  return 0;
}
/* VIDIOC_S_INPUT */
int vidioc_s_input(struct file *file, void *fh, unsigned int i) {
  if (i == 0)
    return 0;
  return -EINVAL;
}
/***************************************************************
**************** V4L2 ioctl buffer related calls ***************
***************************************************************/
/* called on VIDIOC_REQBUFS */
int vidioc_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *b) {
  if (b->memory == V4L2_MEMORY_MMAP)
    return -EINVAL;
  if (b->memory == V4L2_MEMORY_OVERLAY)
    return -EINVAL;
  if (b->memory == V4L2_MEMORY_USERPTR)
  {
    buffer_alloc_type = V4L2_MEMORY_USERPTR;
    return 0;
  }
  return -EINVAL; /* should never come here */
};
/************************************************
**************** file operations  ***************
************************************************/
unsigned int poll(struct file * file, struct poll_table_struct * pts) {
  /* we can read when something is in buffer */
  wait_event_interruptible(read_event, (frame_counter>0) ); 
  return POLLIN|POLLRDNORM;
}
static int v4l_open(struct inode *inode, struct file *file) {

	#ifdef DEBUG
		printk(KERNEL_PREFIX "entering v4l_open()\n");
	#endif

	if(usage > 0)
		return -EBUSY;
	usage++;

	return 0;
}
static int v4l_close(struct inode *inode, struct file *file) {

	#ifdef DEBUG
		printk(KERNEL_PREFIX "entering v4l_close()\n");
	#endif

	usage--;

	return 0;
}

static ssize_t v4l_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
  wait_event_interruptible(read_event, (frame_counter>0) );
  #ifdef DEBUG_OUT
  static int frame_count = 0;
  int i;
  ++frame_count;
  printk(KERNEL_PREFIX "read frame number %d\n", frame_count);
  for(i=0;i<BUFFER_SIZE;++i)
  {
    image[i] = frame_count+i;
  }
  #endif
  
	// if input size superior to the buffered image size
	if (count > BUFFER_SIZE) {
		printk(KERNEL_PREFIX "ERROR : you are attempting to read too much data : %d/%lu\n",count,BUFFER_SIZE);
		return -EINVAL;
	}
	if (copy_to_user((void*)buf, (void*)image, count)) {
		printk (KERN_INFO "failed copy_from_user() in write buf: %p, image: %p\n", buf, image);
		return -EFAULT;
	}    
	//memcpy(buf,image,count);
  --frame_counter;
	#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "v4l_read(), read frame: %d\n",frame_counter);
	#endif
	return count;
}

static ssize_t v4l_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	// if input size superior to the buffered image size
	if (count > BUFFER_SIZE) {
		printk(KERNEL_PREFIX "ERROR : you are attempting to write too much data\n");
		return -EINVAL;
	}

	// Copy of the input image
  int fail_count = copy_from_user((void*)image, (void*)buf, count);
	if (fail_count) {
		printk ("failed copy_from_user() in write buf, could not write %d of %d\n",
            fail_count,
            count);
		return -EFAULT;
	}
  //memcpy(image, buf, count);
  ++frame_counter;
  wake_up_all(&read_event);
	#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "v4l_write(), written frame: %d\n",frame_counter);
	#endif  
	return count;
}

/***********************************************
**************** LINUX KERNEL ****************
************************************************/
void release(struct video_device *vdev) {
	#ifdef DEBUG
		printk(KERNEL_PREFIX "releasing the video device\n");
	#endif
	//kfree(vdev);
}

static struct file_operations v4l_fops = {
	owner:		THIS_MODULE,
	open:		v4l_open,
	release:	v4l_close,
	read:		v4l_read,
	write:		v4l_write,
  poll: poll,
	ioctl:		video_ioctl2,
	compat_ioctl: v4l_compat_ioctl32,
	llseek:     no_llseek,
};
static struct video_device my_device = {
	name:		"Dummy video device",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	type:		VFL_TYPE_GRABBER,
  type2:  VID_TYPE_CAPTURE,
#else
	vfl_type:	VID_TYPE_CAPTURE,
#endif
	fops:       &v4l_fops,
	release:  &release, /*&video_device_release, segfaults on unload double kfree somewhere*/
  vidioc_querycap: &vidioc_querycap,
  vidioc_enum_fmt_cap: &vidioc_enum_fmt_cap,
  vidioc_enum_input: &vidioc_enum_input,
  vidioc_g_input: &vidioc_g_input,
  vidioc_s_input: &vidioc_s_input,
  vidioc_g_fmt_cap: &vidioc_g_fmt_cap,
  vidioc_s_fmt_cap: &vidioc_s_fmt_cap, 
  vidioc_s_fmt_video_output: &vidioc_s_fmt_video_output,
  vidioc_try_fmt_cap: &vidioc_try_fmt_cap,
  vidioc_try_fmt_video_output: &vidioc_try_fmt_video_output,
  vidioc_g_parm: &vidioc_g_parm,
  vidioc_s_parm: &vidioc_s_parm,
	minor:		-1,
  debug:    V4L2_DEBUG_IOCTL|V4L2_DEBUG_IOCTL_ARG,
};

int init_module() {

	#ifdef DEBUG
		printk(KERNEL_PREFIX "entering init_module()\n");
	#endif
	// we register the device -> it creates /dev/video*
	if(video_register_device(&my_device, VFL_TYPE_GRABBER, -1) < 0) {
//    video_device_release(&my_device); TODO(vasaka) uncomment it
		printk (KERN_INFO "failed video_register_device()\n");
		return -EINVAL;
	}

	// image buffer size is computed
	video_format.height = 480;
  video_format.width = 640;
  video_format.pixelformat = V4L2_PIX_FMT_YUV420;
  video_format.field = V4L2_FIELD_NONE;
  video_format.bytesperline = video_format.width*3;
  video_format.sizeimage = video_format.height*video_format.width*3;
  video_format.colorspace = V4L2_COLORSPACE_SRGB;
  BUFFER_SIZE = video_format.sizeimage;

	// allocation of the memory used to save the image
	image = kmalloc (BUFFER_SIZE*3,GFP_KERNEL);
	if (!image) {
    video_unregister_device(&my_device);
		printk (KERN_INFO "failed vmalloc\n");
		return -EINVAL;
	}
	memset(image,0,BUFFER_SIZE*3);

	printk(KERNEL_PREFIX "module installed\n");

	return 0;
}
void cleanup_module() {

	#ifdef DEBUG
		printk(KERNEL_PREFIX "entering cleanup_module()\n");
	#endif

	// we unallocate the image
	if ( image != NULL ) {
		kfree(image);
	}

	// we unregister the device -> it deletes /dev/video*
	video_unregister_device(&my_device);

	printk(KERNEL_PREFIX "module removed\n");
}


MODULE_DESCRIPTION("YAVLD - V4L2 loopback video device");
MODULE_VERSION("0.0.1");
MODULE_AUTHOR("Vasily Levin");
MODULE_LICENSE("GPL");

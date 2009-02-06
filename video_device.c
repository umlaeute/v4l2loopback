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
/*#define DEBUG_RW*/

#define KERNEL_PREFIX "AVLD device: " /* Prefix of each kernel message */
#define VID_DUMMY_DEVICE 0 /* The type of device we create */

#define BUFFER_SIZE_MACRO	(width*height*depth) /* compute buffer size to allocate */


/* The device can't be used by more than 2 applications ( typically : a read and a write application )
Decrease this number if you want it to be accessible for more applications.*/
static int usage = -1;

/* Variable used to let reader control framerate */
struct mutex lock;

/* modifiable video options */
static int fps = 15;
static struct v4l2_pix_format video_format;

static wait_queue_head_t wait;/* variable used to simulate the FPS  */
struct timeval timer;/* used to have a better framerate accuracy */


/* Frame specifications and options*/
static __u8 *image = NULL;
static __u32 pixelformat = V4L2_PIX_FMT_UYVY;

static long BUFFER_SIZE = 0;

/************************************************
**************** V4L2 ioctl calls ***************
************************************************/
/******************************************************************************/
/* writes device capabilities to cap parameter,called on VIDIOC_QUERYCAP ioctl*/
static int vidioc_querycap(struct file *file, 
                           void  *priv,
                           struct v4l2_capability *cap) {
  strcpy (cap->driver, "v4l2 loopback");
  strcpy (cap->card, "Dummy video device");
  cap->version = 1;
  cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
  return 0;
}
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
  if (fmt->fmt.pix.height != video_format.height) return -1;
  if (fmt->fmt.pix.width != video_format.width) return -1;
  if (fmt->fmt.pix.pixelformat != video_format.pixelformat) return -1;
  return 0; 
}
/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */ 
static int vidioc_try_fmt_video_output(struct file *file, 
                                       void *priv, 
                                       struct v4l2_format *fmt) {
  /* TODO(vasaka) check sanity of new values */
  return 0;
};
/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
static int vidioc_s_fmt_cap(struct file *file, 
                            void *priv,
                            struct v4l2_format *fmt) {
 /* TODO(vasaka) check is it OK to supress warnings about mixing declarations 
  * and code in linux kernel modules */
 int ret;
 /* TODO(vasaka) add check if it is OK to change format now */
	if (0)
	    return -EBUSY;
/* check if requsted format is OK */
/*actually format is set  by input and we only check that format is not changed, 
 but it is possible to set subregion of input to return to client TODO(vasaka)*/
	ret = vidioc_try_fmt_cap(file, priv, fmt);
	if (ret != 0)
	    return ret;
  fmt->fmt.pix = video_format;
	#ifdef DEBUG
		printk(KERNEL_PREFIX "video mode set to %dx%d\n", fmt->fmt.pix.width, 
                                                      fmt->fmt.pix.height);
	#endif  
	return 0;
}
/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */ 
static int vidioc_s_fmt_video_output(struct file *file, 
                                     void *priv, 
                                     struct v4l2_format *fmt) {
  int ret;
 /* TODO(vasaka) add check if it is OK to change format now */
	if (0)
	    return -EBUSY;  
	ret = vidioc_try_fmt_video_output(file, priv, fmt);
	if (ret != 0)
	    return ret;
  /* TODO(vasaka) change format and realloc buffer here */
  fmt->fmt.pix = video_format;
	return 0;  
}
/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver called on VIDIOC_G_PARM*/
int vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *parm)
{
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
int vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *parm)
{
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
/************************************************
**************** file operations  ***************
************************************************/
unsigned int poll(struct file * file, struct poll_table_struct * pts) {
  interruptible_sleep_on_timeout (&wait,1000/fps);
  return 1;
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




static int v4l_mmap(struct file *file, struct vm_area_struct *vma) {

	struct page *page = NULL;

	unsigned long pos;
	unsigned long start = (unsigned long)vma->vm_start;
	unsigned long size = (unsigned long)(vma->vm_end-vma->vm_start);

	#ifdef DEBUG
		printk(KERNEL_PREFIX "entering v4l_mmap()\n");
	#endif

	// if userspace tries to mmap beyond end of our buffer, fail
	if (size>BUFFER_SIZE) {
		printk(KERNEL_PREFIX "userspace tries to mmap beyond end of our buffer, fail : %lu. Buffer size is : %lu\n",size,BUFFER_SIZE);
		return -EINVAL;
	}

	// start off at the start of the buffer
	pos=(unsigned long) image;

	// loop through all the physical pages in the buffer
	while (size > 0) {
		page=(void *)vmalloc_to_pfn((void *)pos);

		if (remap_pfn_range(vma, start, (int)page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start+=PAGE_SIZE;
		pos+=PAGE_SIZE;
		size-=PAGE_SIZE;
	}

	#ifdef DEBUG
		printk(KERNEL_PREFIX "leaving v4l_mmap()\n");
	#endif

	return 0;
}

static ssize_t v4l_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {

	#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "entering v4l_read()\n");
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

	#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "leaving v4l_read() : %d - %ld\n",count,BUFFER_SIZE);
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
	if (copy_from_user((void*)image, (void*)buf, count)) {
		printk (KERN_INFO "failed copy_from_user() in write buf: %p, image: %p\n", buf, image);
		return -EFAULT;
	}
  //memcpy(image, buf, count);
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
	mmap: 		v4l_mmap,
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
	release:	&release,
  vidioc_querycap: &vidioc_querycap,
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

	// we initialize the timer and wait structure
	do_gettimeofday(&timer);
	init_waitqueue_head(&wait);

	// we register the device -> it creates /dev/video*
	if(video_register_device(&my_device, VFL_TYPE_GRABBER, -1) < 0) {
		printk (KERN_INFO "failed video_register_device()\n");
		return -EINVAL;
	}

	// image buffer size is computed
	video_format.height = 480;
  video_format.width = 640;
  video_format.pixelformat = pixelformat;
  video_format.field = V4L2_FIELD_NONE;
  video_format.bytesperline = video_format.width*3;
  video_format.sizeimage = video_format.height*video_format.width*3;
  video_format.colorspace = V4L2_COLORSPACE_SRGB;
  BUFFER_SIZE = video_format.sizeimage;

	// allocation of the memory used to save the image
	image = vmalloc (BUFFER_SIZE*3);
	if (!image) {
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
		vfree(image);
	}

	// we unregister the device -> it deletes /dev/video*
	video_unregister_device(&my_device);

	printk(KERNEL_PREFIX "module removed\n");
}


MODULE_DESCRIPTION("YAVLD - V4L2 loopback video device");
MODULE_VERSION("0.0.1");
MODULE_AUTHOR("Vasily Levin");
MODULE_LICENSE("GPL");

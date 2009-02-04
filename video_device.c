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
static int fps = 25; /* framerate */
static int width = 640;
static int height = 480;

/* Frame specifications and options*/
static char *image = NULL;
static int brightness = 32768;
static int hue = 32768;
static int colour = 32768;
static int contrast = 32768;
static int whiteness =32768;
static int depth = 24;
static int palette = 0;
static __u32 pixelformat = V4L2_PIX_FMT_RGB24;
static struct video_window capture_win;

static long BUFFER_SIZE = 0;

static wait_queue_head_t wait;/* variable used to simulate the FPS  */
struct timeval timer;/* used to have a better framerate accuracy */



/************************************************
**************** V4L2 FUNCTIONS  ***************
************************************************/
/******************************************************************************/
/* writes device capabilities to cap parameter,called on VIDIOC_QUERYCAP ioctl*/
static int vidioc_querycap(struct file *file, void  *priv,
        struct v4l2_capability *cap) {
  strcpy (cap->driver, "v4l2 loopback");
  strcpy (cap->card, "Dummy video device");
  cap->version = 1;
  cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
  return 0;
}
/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl 
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
/* actual check is done by inner_try_fmt_cap */
 static int vidioc_try_fmt_cap(struct file *file, void *priv,
         struct v4l2_format *fmt) {
  if (fmt->fmt.pix.height != height) return -1;
  if (fmt->fmt.pix.width != width) return -1;
  if (fmt->fmt.pix.pixelformat != pixelformat) return -1;
  return 0; 
}
/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */ 
static int vidioc_try_fmt_video_output(struct file *file, void *priv, 
                     struct v4l2_format *fmt) {
  /* TODO(vasaka) check sanity of new values */
  return 0;
};
/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
 static int vidioc_s_fmt_cap(struct file *file, void *priv, 
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
	return 0;
}
/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */ 
static int vidioc_s_fmt_video_output(struct file *file, void *priv, 
                     struct v4l2_format *fmt) {
  int ret;
 /* TODO(vasaka) add check if it is OK to change format now */
	if (0)
	    return -EBUSY;  
	ret = vidioc_try_fmt_video_output(file, priv, fmt);
	if (ret != 0)
	    return ret;
  width = fmt->fmt.pix.width;
  height = fmt->fmt.pix.height;
  pixelformat = fmt->fmt.pix.pixelformat;
  /* TODO(vasaka) realloc buffer here */
	return 0;  
};

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
	memcpy(buf,image,count);

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
  vidioc_s_fmt_cap: &vidioc_s_fmt_cap, 
  vidioc_s_fmt_video_output: &vidioc_s_fmt_video_output,
  vidioc_try_fmt_cap: &vidioc_try_fmt_cap,
  vidioc_try_fmt_video_output: &vidioc_try_fmt_video_output,
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
	BUFFER_SIZE=BUFFER_SIZE_MACRO;


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
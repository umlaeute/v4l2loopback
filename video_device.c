/*
YAVLD - Another Video Loopback Device

Version : 0.1.4

YAVLD is a V4L dummy video device built to simulate an input video device like a webcam or a video
capture card. You just have to send the video stream on it (using, for instance, mplayer or ffmpeg),
that's all. Then, you can use this device by watching the video on it with your favorite video player. But,
one of the most useful interest, is obviously to use it with a VideoConferencing software to show a video
over internet. According to the software you are using, you could also be able to capture your screen in
realtime. A third interest, and maybe not the last one, could be to use it with an image processing (or
other) software which has been designed to use a video device as input.

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

#define KERNEL_PREFIX "YAVLD device: " /* Prefix of each kernel message */

/* The device can't be used by more than 2 applications ( typically : a read and a write application )
Decrease this number if you want it to be accessible for more applications.*/
static int usage = -1;
/* read increases frame_counter, write decreases it */
static unsigned int frame_counter = 0;
DECLARE_WAIT_QUEUE_HEAD(read_event);


/* modifiable video options */
static int fps = 30;
static struct v4l2_pix_format video_format;

/* buffers stuff */
static __u8 *image = NULL;
enum v4l2_memory  buffer_alloc_type;
struct v4l2_captureparm	capture_param;
#define MAX_BUFFERS 5
static int buffers_asked = 1;
struct v4l2_buffer my_buffers[MAX_BUFFERS];

static long BUFFER_SIZE = 0;

/****************************************************************
**************** V4L2 ioctl caps and params calls ***************
****************************************************************/
/******************************************************************************/
/* returns device capabilities, called on VIDIOC_QUERYCAP ioctl*/
static int vidioc_querycap(struct file *file, 
                           void  *priv,
                           struct v4l2_capability *cap) {
  strcpy(cap->driver, "v4l2 loopback");
  strcpy(cap->card, "Dummy video device");
  cap->version = 1;
  cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
                      V4L2_CAP_READWRITE | V4L2_CAP_STREAMING; 
  return 0;
}
/******************************************************************************/
/* returns device formats, called on VIDIOC_ENUM_FMT ioctl*/
static int vidioc_enum_fmt_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f) {
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
  if (fmt->fmt.pix.pixelformat != video_format.pixelformat)
    return -EINVAL;
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
 *on this driver, called on VIDIOC_G_PARM*/
static int vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *parm) {
  switch (parm->type)
  {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    {
      parm->parm.capture = capture_param;
      return 0;
    }
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    {
      /* TODO(vasaka) do the same thing as for capture */
      parm->parm.output.timeperframe.denominator = fps;
      parm->parm.output.timeperframe.numerator = 1;
      return 0;
    }
    default:
      return -1;
  }
}
/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver, called on VIDIOC_S_PARM */
static int vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *parm) {
	#ifdef DEBUG
		printk(KERNEL_PREFIX "vidioc_s_parm called frate=%d/%d\n", 
           parm->parm.capture.timeperframe.numerator, 
           parm->parm.capture.timeperframe.denominator);
	#endif    
  switch (parm->type)
  {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    {
      /* TODO(VASAKA) add some checks */
      parm->parm.capture = capture_param;
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
static int vidioc_enum_input(struct file *file, void *fh, struct v4l2_input *inp) {
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
/* which input is currently active, called on VIDIOC_G_INPUT */
int vidioc_g_input(struct file *file, void *fh, unsigned int *i) {
  *i = 0;
  return 0;
}
/* set input, can make sense if we have more than one video src,
 * called on VIDIOC_S_INPUT */
int vidioc_s_input(struct file *file, void *fh, unsigned int i) {
  if (i == 0)
    return 0;
  return -EINVAL;
}
/***************************************************************
**************** V4L2 ioctl buffer related calls ***************
***************************************************************/
/* negotiate buffer type, called on VIDIOC_REQBUFS */
static int vidioc_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *b) {
  switch (b->memory)
  {
    case V4L2_MEMORY_MMAP:
    case V4L2_MEMORY_USERPTR:
    {      
      buffer_alloc_type = b->memory;
      buffers_asked = b->count > MAX_BUFFERS;
      if (buffers_asked > MAX_BUFFERS)
        return -EINVAL;
      if (buffers_asked < 1) 
        buffers_asked = 1;
      my_buffers[0].memory = buffer_alloc_type;      
      return 0;
    }
    default:
      return -EINVAL;
  }
}
/* returns buffer asked for, called on VIDIOC_QUERYBUF */
static int vidioc_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
  int b_index = b->index; /* skype hack */
  /* TODO(vasaka) add validity check */
  if (b->index>MAX_BUFFERS)
    return -EINVAL;
  *b = my_buffers[0];
  b->index = b_index;
  return 0;
}
/* put buffer to queue, called on VIDIOC_QBUF */
static int vidioc_qbuf (struct file *file, void *private_data, 
                 struct v4l2_buffer *buf) {
  /* TODO(vasaka) add validity check */
  buf->flags |= V4L2_BUF_FLAG_QUEUED;
  return 0;
}
static int vidioc_dqbuf (struct file *file, void *private_data, 
        struct v4l2_buffer *b) {
  int b_index = b->index;/* skype hack */
  /* TODO(vasaka) add nonblocking */
  wait_event_interruptible(read_event, (frame_counter>0) );
  --frame_counter;
  my_buffers[0].flags = V4L2_BUF_FLAG_DONE;
  *b = my_buffers[0];
  b->index = my_buffers[0].sequence%buffers_asked; 
  return 0;
}
static int vidioc_streamon(struct file *file, void *private_data, 
                    enum v4l2_buf_type type) {
  frame_counter = 0; /* TODO(vasaka) consider a better place do drop counter */
  return 0;
}
static int vidioc_streamoff(struct file *file, void *private_data, 
                    enum v4l2_buf_type type) {
  return 0;
}
/************************************************
**************** file operations  ***************
************************************************/
static void vm_open(struct vm_area_struct *vma)
{
  /* TODO(vasaka) do open counter here */
}
static void vm_close(struct vm_area_struct *vma)
{
  /* TODO(vasaka) do open counter here */
}
static struct vm_operations_struct vm_ops = {
  .open = vm_open,
  .close = vm_close,
};
static int v4l2_mmap(struct file *file, struct vm_area_struct *vma) {

        struct page *page = NULL;

        unsigned long addr;
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
        addr=(unsigned long) image;

        // loop through all the physical pages in the buffer
        while (size > 0) {
                page=(void *)vmalloc_to_page((void *)addr);

                if (vm_insert_page(vma, start, page) < 0)
                        return -EAGAIN;

                start+=PAGE_SIZE;
                addr+=PAGE_SIZE;
                size-=PAGE_SIZE;
        }
        
        vma->vm_ops = &vm_ops;
        vma->vm_private_data = 0; /* TODO(vasaka) put open counter there and set buffer mmaped flag */
        vm_open(vma);

        #ifdef DEBUG
                printk(KERNEL_PREFIX "leaving v4l_mmap()\n");
        #endif

        return 0;
}
static unsigned int v4l2_poll(struct file * file, struct poll_table_struct * pts) {
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
  int fail_count;
	// if input size superior to the buffered image size
	if (count > BUFFER_SIZE) {
		printk(KERNEL_PREFIX "ERROR : you are attempting to write too much data\n");
		return -EINVAL;
	}

	// Copy of the input image
  fail_count = copy_from_user((void*)image, (void*)buf, count);
	if (fail_count) {
		printk (KERNEL_PREFIX
            "failed copy_from_user() in write buf, could not write %d of %d\n",
            fail_count,
            count);
		return -EFAULT;
	}
  //memcpy(image, buf, count);
  ++frame_counter;
  my_buffers[0].sequence++;
  do_gettimeofday(&my_buffers[0].timestamp);
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
  poll: v4l2_poll,
  mmap: v4l2_mmap,
	ioctl:		video_ioctl2,
	compat_ioctl: v4l_compat_ioctl32,
	llseek:     no_llseek,
};
static struct video_device my_device = {
	name:		"Dummy video device",
  tvnorms: V4L2_STD_PAL_B, /* set something */
  current_norm: V4L2_STD_PAL_B,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	type:		VFL_TYPE_GRABBER,
  type2:  VID_TYPE_CAPTURE,
#else
	vfl_type:	VID_TYPE_CAPTURE,
#endif
	fops:       &v4l_fops,
	release:  &release, /*TODO(vasaka) decide if we need to alloc my_device with video_dev_alloc() */
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
  vidioc_reqbufs: &vidioc_reqbufs,
  vidioc_querybuf: &vidioc_querybuf,
  vidioc_qbuf: &vidioc_qbuf,
  vidioc_dqbuf: &vidioc_dqbuf,
  vidioc_streamon: &vidioc_streamon,
  vidioc_streamoff: &vidioc_streamoff,
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

	/* default picture parameters */
	video_format.height = 480;
  video_format.width = 640;
  video_format.pixelformat = V4L2_PIX_FMT_YUYV;
  video_format.field = V4L2_FIELD_NONE;
  video_format.bytesperline = video_format.width*3;
  video_format.sizeimage = video_format.height*video_format.width*3;
  video_format.colorspace = V4L2_COLORSPACE_SRGB;
  BUFFER_SIZE = video_format.sizeimage;
  
  /* default straming parameters */
  capture_param.capability = 0; 
  capture_param.capturemode = 0;
  capture_param.extendedmode = 0;
  capture_param.readbuffers = 1;
  capture_param.timeperframe.numerator = 1;
  capture_param.timeperframe.denominator = fps;  
  

	// allocation of the memory used to save the image
	image = vmalloc (BUFFER_SIZE*3);
	if (!image) {
    video_unregister_device(&my_device);
		printk (KERN_INFO "failed vmalloc\n");
		return -EINVAL;
	}
	memset(image,0,BUFFER_SIZE*3);
  /* buffer setup */
  my_buffers[0].bytesused = BUFFER_SIZE; /* actual data size */
  my_buffers[0].length = BUFFER_SIZE; /* buffer size */
  my_buffers[0].field = V4L2_FIELD_NONE; /* field of interlaced image */
  my_buffers[0].flags = 0; /* state of buffer */
  my_buffers[0].index = 0; /* index of buffer */
  my_buffers[0].input = 0; /* this is needed for multiply inputs device */
  my_buffers[0].m.offset = 0; /* magic cookie of the buffer */
  my_buffers[0].memory = V4L2_MEMORY_MMAP; /* type of buffer TODO(vasaka) make adjustable */
  my_buffers[0].sequence = 0; /* number of frame transferred */
  my_buffers[0].timestamp.tv_sec = 0; /* 0 means as soon as possible */
  my_buffers[0].timestamp.tv_usec = 0;
  my_buffers[0].type = V4L2_BUF_TYPE_VIDEO_CAPTURE; /* TODO(vasaka) make adjustable */
  

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

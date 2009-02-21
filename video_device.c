/*
YAVLD - Yet Another Video Loopback Device
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
#define DEBUG_RW
#define YAVLD_STREAMING

#define KERNEL_PREFIX "YAVLD device: " /* Prefix of each kernel message */

/* TODO(vasaka) move all global data to private field I think 
 * this would be better then having global variables*/

/* The device can't be used by more than 2 applications ( typically : a read and a write application )
Decrease this number if you want it to be accessible for more applications.*/
static int usage = -1;
static int readers = 0;
DECLARE_WAIT_QUEUE_HEAD(read_event);

/* modifiable video options */
static int fps = 30;
static struct v4l2_pix_format video_format;

/* buffers stuff */
static __u8 *image = NULL;
struct v4l2_captureparm	capture_param;
#define MAX_BUFFERS 5
/* TODO(vasaka) make set of buffers for each opener, in order to give them only
 *buffers they mmaped */
static struct v4l2_buffer my_buffers[MAX_BUFFERS];
static int queued_number = 0;
static int done_number = 0;
static long BUFFER_SIZE = 0;
/****************************************************************
**************** my queue helpers *******************************
****************************************************************/
/******************************************************************************/
/*finds oldest index in outgoing queue, returns index on success -1 on failure*/
static int find_oldest_done(void)
{
  int i;
  int oldest_index = -1;
  __u32 oldest_frame = -1;
  if (done_number == 0)
    return -1;
  for(i=0;i<MAX_BUFFERS;++i)
  {
    if ((my_buffers[i].flags&V4L2_BUF_FLAG_DONE)&&
            (my_buffers[i].sequence<oldest_index))
    {
      oldest_frame = my_buffers[i].sequence;
      oldest_index = i;
    }
  }
  return oldest_index;
}
/* finds next index in incoming queue, returns index on success -1 on failure */
static int find_next_queued(int index)
{
  int i;
  int queued_index = -1;
  if (queued_number == 0)
    return -1;
  for(i=0;i<MAX_BUFFERS;++i)
  {
    if (my_buffers[(index+i)%MAX_BUFFERS].flags&V4L2_BUF_FLAG_QUEUED)
    {
      queued_index = (index+i)%MAX_BUFFERS;
      return queued_index;
    }
  }
  return queued_index;
}
/* next functions sets buffer flags and adjusts counters accordingly */
void set_done(struct v4l2_buffer *buffer)
{
  if (!(buffer->flags&V4L2_BUF_FLAG_DONE))
  {
    buffer->flags |= V4L2_BUF_FLAG_DONE;
    ++done_number;
  }
  if (buffer->flags&V4L2_BUF_FLAG_QUEUED)
  {
    buffer->flags &= ~V4L2_BUF_FLAG_QUEUED;
    --queued_number;
  }  
}
void set_queued(struct v4l2_buffer *buffer)
{
  if (!(buffer->flags&V4L2_BUF_FLAG_QUEUED))
  {
    buffer->flags |= V4L2_BUF_FLAG_QUEUED;
    ++queued_number;
  }
  if (buffer->flags&V4L2_BUF_FLAG_DONE)
  {
    buffer->flags &= ~V4L2_BUF_FLAG_DONE;
    --done_number;
  }    
}
void unset_all(struct v4l2_buffer *buffer)
{
  if (buffer->flags&V4L2_BUF_FLAG_QUEUED)
  {
    buffer->flags &= ~V4L2_BUF_FLAG_QUEUED;
    --queued_number;
  }
  if (buffer->flags&V4L2_BUF_FLAG_DONE)
  {
    buffer->flags &= ~V4L2_BUF_FLAG_DONE;
    --done_number;
  }   
}
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
                      V4L2_CAP_READWRITE 
#ifdef YAVLD_STREAMING
                      | V4L2_CAP_STREAMING
#endif
  ;
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
  if (fmt->fmt.pix.pixelformat != video_format.pixelformat)
    return -EINVAL;   
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
/* sets a tv standart, actually we do not need to handle this any spetial way
 *added to support effecttv */
static int vidioc_s_std(struct file *file, void *private_data, 
                        v4l2_std_id *norm) {
  return 0;
}
/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver, called on VIDIOC_S_PARM */
static int vidioc_s_parm(struct file *file, void *priv, 
                         struct v4l2_streamparm *parm) {
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
static int vidioc_enum_input(struct file *file, void *fh, 
                             struct v4l2_input *inp) {
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
    /* only mmap streaming supported */
    case V4L2_MEMORY_MMAP:
    {      
      if (b->count == 0) 
      {
        /* do nothing here, buffers are always allocated */
        return 0;
      }
      b->count = MAX_BUFFERS;
      return 0;
    }
    default:
      return -EINVAL;
  }
}
/* returns buffer asked for, called on VIDIOC_QUERYBUF */
static int vidioc_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
  /* store type to give the app what it wants */
  enum v4l2_buf_type type = b->type;
  if ((b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)&&
     (b->type != V4L2_BUF_TYPE_VIDEO_OUTPUT))
    return -EINVAL;
  if (b->index>MAX_BUFFERS)
    return -EINVAL;
  *b = my_buffers[b->index];
  b->type = type;
  return 0;
}
/* put buffer to queue, called on VIDIOC_QBUF */
static int vidioc_qbuf (struct file *file, void *private_data, 
                 struct v4l2_buffer *buf) {  
  if (buf->index>MAX_BUFFERS)
    return -EINVAL;
  switch (buf->type) 
  {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    {
      set_queued(&my_buffers[buf->index]);
      return 0;
    }
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    {      
      do_gettimeofday(&my_buffers[buf->index].timestamp);
      set_done(&my_buffers[buf->index]);
      wake_up_all(&read_event);      
      return 0;
    }
    default:
      return -EINVAL;
  }
}
/* put buffer to dequeue, called on VIDIOC_DQBUF */
static int vidioc_dqbuf (struct file *file, void *private_data, 
        struct v4l2_buffer *buf) {
  int index;
  static int queued_index = 0;
  switch (buf->type) 
  {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    {
      /* TODO(vasaka) add nonblocking */
      wait_event_interruptible(read_event, (done_number>0) );
      index = find_oldest_done();
      if (index<0)
      {
        printk (KERN_INFO "find_oldest_done failed on dqbuf\n");
        return -EFAULT;    
      }      
      *buf = my_buffers[index];
      unset_all(&my_buffers[buf->index]);
      return 0;
    }
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    {      
      /* TODO(vasaka) need to add check for empty queue and polling for buffer*/
      queued_index = find_next_queued(queued_index);
      if (queued_index<0)
      {
        printk (KERN_INFO "find_next_queued failed on dqbuf\n");
        return -EFAULT;    
      }      
      *buf = my_buffers[queued_index];
      unset_all(&my_buffers[queued_index]);
      return 0;
    }
    default:
      return -EINVAL;
  }  
  return 0;
}
static int vidioc_streamon(struct file *file, void *private_data, 
                    enum v4l2_buf_type type) {
  return 0;
}
static int vidioc_streamoff(struct file *file, void *private_data, 
                    enum v4l2_buf_type type) {
  return 0;
}
#ifdef CONFIG_VIDEO_V4L1_COMPAT
int vidiocgmbuf(struct file *file, void *fh, struct video_mbuf *p) {
  p->frames = MAX_BUFFERS;
  p->offsets[0] = 0;
  p->offsets[1] = 0;
  p->size = BUFFER_SIZE;
  return 0;
}
#endif
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
        addr=(unsigned long) image+vma->vm_pgoff;

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
  /* TODO(vasaka) make a distinction between reader and writer */
  wait_event_interruptible(read_event, (done_number>0) ); 
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
  int done_index;
  /* TODO(vasaka) fill incoming_queue before starting basic IO */
  wait_event_interruptible(read_event, (done_number>0) );
	// we can write only what we have already
	if (count > BUFFER_SIZE) 
    count = BUFFER_SIZE;
  done_index = find_oldest_done();
  if (done_index<0)
  {
		printk (KERN_INFO "find_oldest_done failed on read\n");
		return -EFAULT;    
  }
  unset_all(&my_buffers[done_index]);
	if (copy_to_user((void*)buf, 
                   (void*)(image+my_buffers[done_index].m.offset), count)) {
		printk (KERN_INFO "failed copy_from_user() in write buf: %p, image: %p\n", 
            buf, image);
		return -EFAULT;
	}
  set_queued(&my_buffers[done_index]);
#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "leave v4l_read()\n");
#endif
	return count;
}

static ssize_t v4l_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
  static int  queued_index = 0;
  static int frame_number = 0;
#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "v4l_write(), enter write frame: %d\n",frame_number);
#endif    
  /* we do not need to write anithyng if there is no incoming buffers */
  if (queued_number == 0)
    return count;
	// we simply throw away what is more than we want
	if (count > BUFFER_SIZE) 
    count = BUFFER_SIZE;
  queued_index = find_next_queued(queued_index);
  if (queued_index<0)
  {
		printk (KERN_INFO "find_next_queued failed on write\n");
		return -EFAULT;    
  }
  unset_all(&my_buffers[queued_index]);
	// Copy of the input image
	if (copy_from_user((void*)(image+my_buffers[queued_index].m.offset), 
                     (void*)buf, count)) {
		printk (KERNEL_PREFIX
            "failed copy_from_user() in write buf, could not write %d\n",
            count);
		return -EFAULT;
	}
  my_buffers[queued_index].sequence = frame_number++;
  do_gettimeofday(&my_buffers[queued_index].timestamp);
  set_done(&my_buffers[queued_index]);
  wake_up_all(&read_event);
#ifdef DEBUG_RW
		printk(KERNEL_PREFIX "v4l_write(), written frame: %d\n",frame_number);
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
  tvnorms: V4L2_STD_NTSC|V4L2_STD_SECAM|V4L2_STD_PAL, /* set something */
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
  vidioc_s_std: &vidioc_s_std,
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
#ifdef CONFIG_VIDEO_V4L1_COMPAT
  vidiocgmbuf: &vidiocgmbuf,
#endif  
};

void init_buffers (int buffer_size)
{
  int i;
  for (i=0;i<MAX_BUFFERS;++i)
  {
    my_buffers[i].bytesused = buffer_size; /* actual data size */
    my_buffers[i].length = buffer_size; /* buffer size */
    my_buffers[i].field = V4L2_FIELD_NONE; /* field of interlaced image */
    my_buffers[i].flags = 0; /* state of buffer, flags are valid for capture mode */
    my_buffers[i].index = i; /* index of buffer */
    my_buffers[i].input = 0; /* this is needed for multiply inputs device */
    my_buffers[i].m.offset = i*buffer_size; /* magic cookie of the buffer */
    my_buffers[i].memory = V4L2_MEMORY_MMAP; /* type of buffer TODO(vasaka) make adjustable */
    my_buffers[i].sequence = 0; /* number of frame transferred */
    my_buffers[i].timestamp.tv_sec = 0; /* 0 means as soon as possible */
    my_buffers[i].timestamp.tv_usec = 0;
    my_buffers[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE; /* TODO(vasaka) make adjustable */    
  }
}

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
  video_format.bytesperline = video_format.width*2;
  video_format.sizeimage = video_format.height*video_format.width*2;
  video_format.colorspace = V4L2_COLORSPACE_SRGB;
  BUFFER_SIZE = PAGE_ALIGN(video_format.sizeimage);
  
  /* default straming parameters */
  capture_param.capability = 0; 
  capture_param.capturemode = 0;
  capture_param.extendedmode = 0;
  capture_param.readbuffers = 1;
  capture_param.timeperframe.numerator = 1;
  capture_param.timeperframe.denominator = fps;  
  
  init_buffers(BUFFER_SIZE);
	// allocation of the memory used to save the image
  /* TODO(vasaka) move this to reqbufs in order to make it consume only memory
   * needed */
	image = vmalloc (BUFFER_SIZE*MAX_BUFFERS);
	if (!image) {
    video_unregister_device(&my_device);
		printk (KERN_INFO "failed vmalloc\n");
		return -EINVAL;
	}
	memset(image,0,BUFFER_SIZE*MAX_BUFFERS);
  /* buffer setup */
  
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

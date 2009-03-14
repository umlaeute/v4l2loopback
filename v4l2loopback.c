/*
YAVLD - Yet Another Video Loopback Device
Distributed according to the GPL.
*/
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/module.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <media/v4l2-ioctl.h>
#endif
#include "v4l2loopback.h"

//#define DEBUG
//#define DEBUG_RW
#define YAVLD_STREAMING
#define KERNEL_PREFIX "YAVLD device: "	/* Prefix of each kernel message */
/* global module data */
struct v4l2_loopback_device *dev;
/* forward declarations */
static void init_buffers(int buffer_size);
static const struct file_operations v4l2_loopback_fops;
static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops;
/****************************************************************
**************** my queue helpers *******************************
****************************************************************/
/******************************************************************************/
/*finds oldest index in outgoing queue, returns index on success -1 on failure*/
static int find_oldest_done(void)
{
	int i;
	int oldest_index = -1;
	__u32 oldest_frame = -1;	/* this should be max __u32 */
	if (dev->done_number == 0) {
		return -1;
	}
	for (i = 0; i < dev->buffers_number; ++i) {
		if ((dev->buffers[i].flags & V4L2_BUF_FLAG_DONE) &&
		    (dev->buffers[i].sequence < oldest_index)) {
			oldest_frame = dev->buffers[i].sequence;
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
	if (dev->queued_number == 0) {
		return -1;
	}
	if (index < -1) {
		return -1;
	}
	for (i = 1; i <= dev->buffers_number; ++i) {
		if (dev->buffers[(index + i) % dev->buffers_number].
		    flags & V4L2_BUF_FLAG_QUEUED) {
			queued_index = (index + i) % dev->buffers_number;
			return queued_index;
		}
	}
	return queued_index;
}

/* next functions sets buffer flags and adjusts counters accordingly */
void set_done(struct v4l2_buffer *buffer)
{
	if (!(buffer->flags & V4L2_BUF_FLAG_DONE)) {
		buffer->flags |= V4L2_BUF_FLAG_DONE;
		++dev->done_number;
	}
	if (buffer->flags & V4L2_BUF_FLAG_QUEUED) {
		buffer->flags &= ~V4L2_BUF_FLAG_QUEUED;
		--dev->queued_number;
	}
}

void set_queued(struct v4l2_buffer *buffer)
{
	if (!(buffer->flags & V4L2_BUF_FLAG_QUEUED)) {
		buffer->flags |= V4L2_BUF_FLAG_QUEUED;
		++dev->queued_number;
	}
	if (buffer->flags & V4L2_BUF_FLAG_DONE) {
		buffer->flags &= ~V4L2_BUF_FLAG_DONE;
		--dev->done_number;
	}
}

void unset_all(struct v4l2_buffer *buffer)
{
	if (buffer->flags & V4L2_BUF_FLAG_QUEUED) {
		buffer->flags &= ~V4L2_BUF_FLAG_QUEUED;
		--dev->queued_number;
	}
	if (buffer->flags & V4L2_BUF_FLAG_DONE) {
		buffer->flags &= ~V4L2_BUF_FLAG_DONE;
		--dev->done_number;
	}
}

/****************************************************************
**************** V4L2 ioctl caps and params calls ***************
****************************************************************/
/******************************************************************************/
/* returns device capabilities, called on VIDIOC_QUERYCAP ioctl*/
static int vidioc_querycap(struct file *file,
			   void *priv, struct v4l2_capability *cap)
{
	strcpy(cap->driver, "v4l2 loopback");
	strcpy(cap->card, "Dummy video device");
	cap->version = 1;
	cap->capabilities =
	    V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
	    V4L2_CAP_READWRITE
#ifdef YAVLD_STREAMING
	    | V4L2_CAP_STREAMING
#endif
	    ;
	return 0;
}

/******************************************************************************/
/* returns device formats, called on VIDIOC_ENUM_FMT ioctl*/
static int vidioc_enum_fmt_cap(struct file *file, void *fh,
			       struct v4l2_fmtdesc *f)
{
	if (dev->redy_for_capture == 0) {
		return -EINVAL;
	}
	if (f->index) {
		return -EINVAL;
	}
	strcpy(f->description, "current format");
	f->pixelformat = dev->pix_format.pixelformat;
	return 0;
};

/******************************************************************************/
/* returns current video format format fmt, called on VIDIOC_G_FMT ioctl */
static int vidioc_g_fmt_cap(struct file *file,
			    void *priv, struct v4l2_format *fmt)
{
	if (dev->redy_for_capture == 0) {
		return -EINVAL;
	}
	fmt->fmt.pix = dev->pix_format;
	return 0;
}

/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
/* actual check is done by inner_try_fmt_cap */
/* just checking that pixelformat is OK and set other parameters, app should
 * obey this decidion */
static int vidioc_try_fmt_cap(struct file *file,
			      void *priv, struct v4l2_format *fmt)
{
	if (dev->redy_for_capture == 0) {
		return -EINVAL;
	}
	if (fmt->fmt.pix.pixelformat != dev->pix_format.pixelformat) {
		return -EINVAL;
	}
	fmt->fmt.pix = dev->pix_format;
	return 0;
}

/******************************************************************************/
/* checks if it is OK to change to format fmt, called on VIDIOC_TRY_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */
/* if format is negotiated do not change it */
static int vidioc_try_fmt_video_output(struct file *file,
				       void *priv, struct v4l2_format *fmt)
{
	/* TODO(vasaka) loopback does not care about formats writer want to set,
	 * maybe it is a good idea to restrict format somehow */
	if (dev->redy_for_capture) {
		fmt->fmt.pix = dev->pix_format;
	} else {
		if (fmt->fmt.pix.sizeimage == 0) {
			return -1;
		}
		dev->pix_format = fmt->fmt.pix;
	}
	return 0;
};

/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE */
/* actually format is set  by input and we even do not check it, just return
 * current one, but it is possible to set subregions of input TODO(vasaka) */
static int vidioc_s_fmt_cap(struct file *file,
			    void *priv, struct v4l2_format *fmt)
{
	return vidioc_try_fmt_cap(file, priv, fmt);
}

/******************************************************************************/
/* sets new output format, if possible, called on VIDIOC_S_FMT ioctl
 * with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT */
/* allocate data here because we do not know if it will be streaming or
 * read/write IO */
static int vidioc_s_fmt_video_output(struct file *file,
				     void *priv, struct v4l2_format *fmt)
{
	vidioc_try_fmt_video_output(file, priv, fmt);
	if (dev->redy_for_capture == 0) {
		dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
		fmt->fmt.pix.sizeimage = dev->buffer_size;
		/* vfree on close file operation in case no open handles left */
		dev->image =
		    vmalloc(dev->buffer_size * dev->buffers_number);
		if (dev->image == NULL) {
			return -EINVAL;
		}
#ifdef DEBUG
		printk(KERNEL_PREFIX "vmallocated %ld bytes\n",
		       dev->buffer_size * dev->buffers_number);
#endif
		init_buffers(dev->buffer_size);
		dev->redy_for_capture = 1;
	}
	return 0;
}

/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver, called on VIDIOC_G_PARM*/
static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
	/* do not care about type of opener, hope this enums would always be 
	 * compatible */
	parm->parm.capture = dev->capture_param;
	return 0;
}

/******************************************************************************/
/*get some data flaw parameters, only capability, fps and readbuffers has effect
 *on this driver, called on VIDIOC_S_PARM */
static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *parm)
{
#ifdef DEBUG
	printk(KERNEL_PREFIX "vidioc_s_parm called frate=%d/%d\n",
	       parm->parm.capture.timeperframe.numerator,
	       parm->parm.capture.timeperframe.denominator);
#endif
	switch (parm->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:{
			parm->parm.capture = dev->capture_param;
			return 0;
		}
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:{
			/* TODO(vasaka) do nothing now, but should set fps if 
			 * needed */
			parm->parm.capture = dev->capture_param;
			return 0;
		}
	default:{
			return -1;
		}
	}
}

/* sets a tv standart, actually we do not need to handle this any spetial way
 *added to support effecttv */
static int vidioc_s_std(struct file *file, void *private_data,
			v4l2_std_id * norm)
{
	return 0;
}

/* returns set of device inputs, in our case there is only one, but later I may
 * add more, called on VIDIOC_ENUMINPUT */
static int vidioc_enum_input(struct file *file, void *fh,
			     struct v4l2_input *inp)
{
	if (dev->redy_for_capture == 0) {
		return -EINVAL;
	}
	if (inp->index == 0) {
		strcpy(inp->name, "loopback");
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
int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	if (dev->redy_for_capture == 0) {
		return -EINVAL;
	}
	*i = 0;
	return 0;
}

/* set input, can make sense if we have more than one video src,
 * called on VIDIOC_S_INPUT */
int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	if (dev->redy_for_capture == 0) {
		return -EINVAL;
	}
	if (i == 0) {
		return 0;
	}
	return -EINVAL;
}

/***************************************************************
**************** V4L2 ioctl buffer related calls ***************
***************************************************************/
/* negotiate buffer type, called on VIDIOC_REQBUFS */
/* only mmap streaming supported */
static int vidioc_reqbufs(struct file *file, void *fh,
			  struct v4l2_requestbuffers *b)
{
	switch (b->memory) {
	case V4L2_MEMORY_MMAP:{
			if (b->count == 0) {
				/* do nothing here, buffers are always allocated
				 * TODO(vasaka) ? */
				return 0;
			}
			b->count = dev->buffers_number;
			return 0;
		}
	default:{
			return -EINVAL;
		}
	}
}

/* returns buffer asked for, called on VIDIOC_QUERYBUF */
/* give app as many buffers as it wants, if it less than 100 :-), 
 * but map them in our inner buffers */
static int vidioc_querybuf(struct file *file, void *fh,
			   struct v4l2_buffer *b)
{
	enum v4l2_buf_type type = b->type;
	int index = b->index;
	if ((b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
	    (b->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)) {
		return -EINVAL;
	}
	if (b->index > 100) {
		return -EINVAL;
	}
	*b = dev->buffers[b->index % dev->buffers_number];
	b->type = type;
	b->index = index;
	return 0;
}

/* put buffer to queue, called on VIDIOC_QBUF */
static int vidioc_qbuf(struct file *file, void *private_data,
		       struct v4l2_buffer *buf)
{
	int index = buf->index % dev->buffers_number;
	if (buf->index > 100) {
		return -EINVAL;
	}
	switch (buf->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:{
			if (dev->buffers[index].flags & V4L2_BUF_FLAG_DONE)
				return 0;
			set_queued(&dev->buffers[index]);
			return 0;
		}
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:{
			do_gettimeofday(&dev->buffers[index].timestamp);
			set_done(&dev->buffers[index]);
			wake_up_all(&dev->read_event);
			return 0;
		}
	default:{
			return -EINVAL;
		}
	}
}

/* put buffer to dequeue, called on VIDIOC_DQBUF */
static int vidioc_dqbuf(struct file *file, void *private_data,
			struct v4l2_buffer *buf)
{
	int index;
	static int queued_index = V4L2_LOOPBACK_BUFFERS_NUMBER;
	switch (buf->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:{
			/* TODO(vasaka) add nonblocking */
			wait_event_interruptible(dev->read_event,
						 (dev->done_number >=
						  dev->num_collect));
			/* TODO(vasaka) uncomment when blocking io will be OK */
			/*if (done_number<num_collect)
			   return -EAGAIN;
			 */
			index = find_oldest_done();
			if (index < 0) {
				printk(KERN_INFO
				       "find_oldest_done failed on dqbuf\n");
				return -EFAULT;
			}
			unset_all(&dev->buffers[index]);
			*buf = dev->buffers[index];
			return 0;
		}
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:{
			/* TODO(vasaka) need to add check for empty incoming 
			 * queue and polling for buffer */
			queued_index = find_next_queued(queued_index);
			if (queued_index < 0) {
				printk(KERN_INFO
				       "find_next_queued failed on dqbuf\n");
				return -EFAULT;
			}
			unset_all(&dev->buffers[queued_index]);
			*buf = dev->buffers[queued_index];
			return 0;
		}
	default:
		return -EINVAL;
	}
}

static int vidioc_streamon(struct file *file, void *private_data,
			   enum v4l2_buf_type type)
{
	return 0;
}

static int vidioc_streamoff(struct file *file, void *private_data,
			    enum v4l2_buf_type type)
{
	return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
int vidiocgmbuf(struct file *file, void *fh, struct video_mbuf *p)
{
	p->frames = dev->buffers_number;
	p->offsets[0] = 0;
	p->offsets[1] = 0;
	p->size = dev->buffer_size;
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

static int v4l2_loopback_mmap(struct file *file,
			      struct vm_area_struct *vma)
{

	struct page *page = NULL;

	unsigned long addr;
	unsigned long start = (unsigned long) vma->vm_start;
	unsigned long size = (unsigned long) (vma->vm_end - vma->vm_start);

#ifdef DEBUG
	printk(KERNEL_PREFIX "entering v4l_mmap(), offset: %lu\n",
	       vma->vm_pgoff << PAGE_SHIFT);
#endif
	if (size > dev->buffer_size) {
		printk(KERNEL_PREFIX
		       "userspace tries to mmap to much, fail\n");
		return -EINVAL;
	}
	if ((vma->vm_pgoff << PAGE_SHIFT) >
	    dev->buffer_size * (dev->buffers_number - 1)) {
		printk(KERNEL_PREFIX
		       "userspace tries to mmap to far, fail\n");
		return -EINVAL;
	}
	// start off at the start of the buffer
	addr = (unsigned long) dev->image + (vma->vm_pgoff << PAGE_SHIFT);

	// loop through all the physical pages in the buffer
	while (size > 0) {
		page = (void *) vmalloc_to_page((void *) addr);

		if (vm_insert_page(vma, start, page) < 0)
			return -EAGAIN;

		start += PAGE_SIZE;
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vma->vm_ops = &vm_ops;
	vma->vm_private_data = 0;/* TODO(vasaka) put open counter there and set
				  * buffer mmaped flag */
	vm_open(vma);

#ifdef DEBUG
	printk(KERNEL_PREFIX "leaving v4l_mmap()\n");
#endif

	return 0;
}

static unsigned int v4l2_loopback_poll(struct file *file,
				       struct poll_table_struct *pts)
{
	/* TODO(vasaka) make a distinction between reader and writer */
	wait_event_interruptible(dev->read_event,
				 (dev->done_number >= dev->num_collect));
	return POLLIN | POLLRDNORM;
}

/* do not want to limit device opens, it can be as many readers as user want,
 * writers are limited by means of setting writer field */
static int v4l_loopback_open(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	printk(KERNEL_PREFIX "entering v4l_open()\n");
#endif
	/* TODO(vasaka) remove this when multy reader is ready */
	if (dev->open_count == 2) {
		return -EBUSY;
	}
	++dev->open_count;
	return 0;
}

static int v4l_loopback_close(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	printk(KERNEL_PREFIX "entering v4l_close()\n");
#endif
	--dev->open_count;
	/* TODO(vasaka) does the closed file means that mmaped buffers are 
	 * no more valid and one can free data? */
	if (dev->open_count == 0) {
		vfree(dev->image);
		dev->redy_for_capture = 0;
	}
	return 0;
}

static ssize_t v4l_loopback_read(struct file *file, char __user * buf,
				 size_t count, loff_t * ppos)
{
	int done_index;
	/* TODO(vasaka) fill incoming_queue before starting basic IO */
	wait_event_interruptible(dev->read_event,
				 (dev->done_number >= dev->num_collect));
	if (count > dev->buffer_size) {
		count = dev->buffer_size;
	}
	done_index = find_oldest_done();
	if (done_index < 0) {
		printk(KERN_INFO "find_oldest_done failed on read\n");
		return -EFAULT;
	}
	unset_all(&dev->buffers[done_index]);
	if (copy_to_user((void *) buf,
			 (void *) (dev->image +
				   dev->buffers[done_index].m.offset),
			 count)) {
		printk(KERN_INFO "failed copy_from_user() in write buf\n");
		return -EFAULT;
	}
	set_queued(&dev->buffers[done_index]);
#ifdef DEBUG_RW
	printk(KERNEL_PREFIX "leave v4l2_loopback_read()\n");
#endif
	return count;
}

static ssize_t v4l_loopback_write(struct file *file,
				  const char __user * buf, size_t count,
				  loff_t * ppos)
{
	static int queued_index = -1;/* on first pass we want to search from 0*/
	static int frame_number = 0;
#ifdef DEBUG_RW
	printk(KERNEL_PREFIX
	       "v4l2_loopback_write() trying to write %d bytes\n", count);
#endif
	if (dev->queued_number < 2) {
		return count;
	}
	if (count > dev->buffer_size) {
		count = dev->buffer_size;
	}
	queued_index = find_next_queued(queued_index);
	if (queued_index < 0) {
		printk(KERN_INFO "find_next_queued failed on write\n");
		return -EFAULT;
	}
	unset_all(&dev->buffers[queued_index]);

	if (copy_from_user
	    ((void *) (dev->image + dev->buffers[queued_index].m.offset),
	     (void *) buf, count)) {
		printk(KERNEL_PREFIX
		  "failed copy_from_user() in write buf, could not write %d\n",
		   count);
		return -EFAULT;
	}
	dev->buffers[queued_index].sequence = frame_number++;
	do_gettimeofday(&dev->buffers[queued_index].timestamp);
	set_done(&dev->buffers[queued_index]);
	wake_up_all(&dev->read_event);
#ifdef DEBUG_RW
	printk(KERNEL_PREFIX "leave v4l2_loopback_write()\n");
#endif
	return count;
}

/************************************************
**************** init functions *****************
************************************************/
/* init inner buffers, they are capture mode and flags are set as 
 * for capture mod buffers */
static void init_buffers(int buffer_size)
{
	int i;
	for (i = 0; i < dev->buffers_number; ++i) {
		dev->buffers[i].bytesused = buffer_size;
		dev->buffers[i].length = buffer_size;
		dev->buffers[i].field = V4L2_FIELD_NONE;
		dev->buffers[i].flags = 0;	
		dev->buffers[i].index = i;
		dev->buffers[i].input = 0;
		dev->buffers[i].m.offset = i * buffer_size;
		dev->buffers[i].memory = V4L2_MEMORY_MMAP;
		dev->buffers[i].sequence = 0;
		dev->buffers[i].timestamp.tv_sec = 0;
		dev->buffers[i].timestamp.tv_usec = 0;
		dev->buffers[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	}
	dev->done_number = 0;
	dev->queued_number = 0;
}

/* fills and register video device */
static void init_vdev(struct video_device *vdev)
{
	strcpy(vdev->name, "Dummy video device");
	vdev->tvnorms = V4L2_STD_NTSC | V4L2_STD_SECAM | V4L2_STD_PAL;/* TODO */
	vdev->current_norm = V4L2_STD_PAL_B, /* do not know what is best here */
	vdev->vfl_type = VID_TYPE_CAPTURE;
	vdev->fops = &v4l2_loopback_fops;
	vdev->ioctl_ops = &v4l2_loopback_ioctl_ops;
	vdev->release = &video_device_release;
	vdev->minor = -1;
#ifdef DEBUG
	vdev->debug = V4L2_DEBUG_IOCTL | V4L2_DEBUG_IOCTL_ARG;
#endif
}

/* init default capture paramete, only fps may be changed in future */
static void init_capture_param(struct v4l2_captureparm *capture_param)
{
	capture_param->capability = 0;
	capture_param->capturemode = 0;
	capture_param->extendedmode = 0;
	capture_param->readbuffers = V4L2_LOOPBACK_BUFFERS_NUMBER;
	capture_param->timeperframe.numerator = 1;
	capture_param->timeperframe.denominator = 30;
}

/* init loopback main structure */
static int v4l2_loopback_init(struct v4l2_loopback_device *dev)
{
	dev->vdev = video_device_alloc();
	if (dev->vdev == NULL) {
		return -1;
	}
	init_vdev(dev->vdev);
	init_capture_param(&dev->capture_param);
	dev->buffers_number = V4L2_LOOPBACK_BUFFERS_NUMBER;
	dev->open_count = 0;
	dev->redy_for_capture = 0;
	dev->num_collect = 1;
	dev->buffer_size = 0;
	/* kfree on module release */
	dev->buffers =
	    kzalloc(sizeof(*dev->buffers) * dev->buffers_number,
		    GFP_KERNEL);
	if (dev->buffers == NULL) {
		return -ENOMEM;
	}
	init_waitqueue_head(&dev->read_event);
	return 0;
};

/***********************************************
**************** LINUX KERNEL ****************
************************************************/
static const struct file_operations v4l2_loopback_fops = {
      .owner = THIS_MODULE,
      .open = v4l_loopback_open,
      .release = v4l_loopback_close,
      .read = v4l_loopback_read,
      .write = v4l_loopback_write,
      .poll = v4l2_loopback_poll,
      .mmap = v4l2_loopback_mmap,
      .ioctl = video_ioctl2,
      .compat_ioctl = v4l_compat_ioctl32,
      .llseek = no_llseek,
};

static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops = {
	.vidioc_querycap = &vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = &vidioc_enum_fmt_cap,
	.vidioc_enum_input = &vidioc_enum_input,
	.vidioc_g_input = &vidioc_g_input,
	.vidioc_s_input = &vidioc_s_input,
	.vidioc_g_fmt_vid_cap = &vidioc_g_fmt_cap,
	.vidioc_s_fmt_vid_cap = &vidioc_s_fmt_cap,
	.vidioc_s_fmt_vid_out = &vidioc_s_fmt_video_output,
	.vidioc_try_fmt_vid_cap = &vidioc_try_fmt_cap,
	.vidioc_try_fmt_vid_out = &vidioc_try_fmt_video_output,
	.vidioc_s_std = &vidioc_s_std,
	.vidioc_g_parm = &vidioc_g_parm,
	.vidioc_s_parm = &vidioc_s_parm,
	.vidioc_reqbufs = &vidioc_reqbufs,
	.vidioc_querybuf = &vidioc_querybuf,
	.vidioc_qbuf = &vidioc_qbuf,
	.vidioc_dqbuf = &vidioc_dqbuf,
	.vidioc_streamon = &vidioc_streamon,
	.vidioc_streamoff = &vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf = &vidiocgmbuf,
#endif
};

int __init init_module()
{
#ifdef DEBUG
	printk(KERNEL_PREFIX "entering init_module()\n");
#endif
	/* kfree on module release */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		return -ENOMEM;
	}
	if (v4l2_loopback_init(dev) < 0) {
		return -EINVAL;
	}
	/* register the device -> it creates /dev/video* */
	if (video_register_device(dev->vdev, VFL_TYPE_GRABBER, -1) < 0) {
		video_device_release(dev->vdev);
		printk(KERN_INFO "failed video_register_device()\n");
		return -EINVAL;
	}
	printk(KERNEL_PREFIX "module installed\n");
	return 0;
}

void __exit cleanup_module()
{
#ifdef DEBUG
	printk(KERNEL_PREFIX "entering cleanup_module()\n");
#endif
	/* unregister the device -> it deletes /dev/video* */
	video_unregister_device(dev->vdev);
	kfree(dev->buffers);
	kfree(dev);
	printk(KERNEL_PREFIX "module removed\n");
}


MODULE_DESCRIPTION("YAVLD - V4L2 loopback video device");
MODULE_VERSION("0.0.1");
MODULE_AUTHOR("Vasily Levin");
MODULE_LICENSE("GPL");

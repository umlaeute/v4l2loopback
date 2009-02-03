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
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#include <media/v4l2-ioctl.h>
#endif

/*#define DEBUG*/

#define KERNEL_PREFIX "AVLD device: " /* Prefix of each kernel message */
#define VID_DUMMY_DEVICE 0 /* The type of device we create */

#define BUFFER_SIZE_MACRO	((width*height*(depth>>3))/PAGE_SIZE+1)*PAGE_SIZE /* compute buffer size to allocate */


/* v4l palettes */
static int _palette[]={
	VIDEO_PALETTE_RGB24,	/*0*/
	VIDEO_PALETTE_GREY,		/*1*/
	VIDEO_PALETTE_HI240,	/*2*/
	VIDEO_PALETTE_RGB565,	/*3*/
	VIDEO_PALETTE_RGB24,	/*4*/
	VIDEO_PALETTE_RGB32,	/*5*/
	VIDEO_PALETTE_RGB555,	/*6*/
	VIDEO_PALETTE_YUV422,	/*7*/
	VIDEO_PALETTE_YUYV,		/*8*/
	VIDEO_PALETTE_UYVY,		/*9*/
	VIDEO_PALETTE_YUV420,	/*10*/
	VIDEO_PALETTE_YUV411,	/*11*/
	VIDEO_PALETTE_RAW,		/*12*/
	VIDEO_PALETTE_YUV422P,	/*13*/
	VIDEO_PALETTE_YUV411P,	/*14*/
	VIDEO_PALETTE_YUV420P,	/*15*/
	VIDEO_PALETTE_YUV410P	/*16*/
};

/* v4l palette names */
static char *palette_name[]={
	"RGB24",
	"GREY",
	"HI240",
	"RGB565",
	"RGB24",
	"RGB32",
	"RGB555",
	"YUV422",
	"YUYV",
	"UYVY",
	"YUV420",
	"YUV411",
	"RAW",
	"YUV422P",
	"YUV411P",
	"YUV420P",
	"YUV410P",
	0
};


/* The device can't be used by more than 2 applications ( typically : a read and a write application )
Decrease this number if you want it to be accessible for more applications.*/
static int usage = -1;

/* Variable used to let reader control framerate */
struct mutex lock;

/* modifiable video options */ 
static int fps = 25; /* framerate */
static int width = 320; 
static int height = 240;

/* Frame specifications and options*/
static char *image = NULL;
static int brightness = 32768;
static int hue = 32768;
static int colour = 32768;
static int contrast = 32768;
static int whiteness =32768;
static int depth = 32;
static int palette = 0;
static struct video_window capture_win;
	
static long BUFFER_SIZE = 0;

static wait_queue_head_t wait;/* variable used to simulate the FPS  */
struct timeval timer;/* used to have a better framerate accuracy */



/************************************************
**************** V4L FUNCTIONS  ***************
************************************************/
static int v4l_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg) {
	
	switch(cmd) {
		/* Get capabilities */
		case VIDIOCGCAP:
		{
			struct video_capability *v=(struct video_capability *)arg;
 			
 			#ifdef DEBUG
 				printk (KERNEL_PREFIX "VIDIOCGCAP\n");
 			#endif 			

			v->type = VID_TYPE_CAPTURE;
			v->channels = 1;
			v->audios = 0;
			v->maxwidth = width;
			v->maxheight = height;
			v->minwidth = width;
			v->minheight = height;
			strcpy(v->name, "Dummy video device");
 			
			return 0;

		} 
		
		/* Get channel info (sources) */
		case VIDIOCGCHAN:
		{
			struct video_channel *v=(struct video_channel *)arg;

 			#ifdef DEBUG
 				printk (KERNEL_PREFIX "VIDIOCGCHAN\n");
 			#endif
		
			if (v->channel!=0)
				return -EINVAL;

			v->flags = 0;
			v->tuners = 0;
			v->type = VIDEO_TYPE_CAMERA;
			v->norm = VIDEO_MODE_AUTO;
			v->channel = 0;
			strcpy(v->name, "Dummy video device");
 			
			return 0;
		}
		
		/* Set channel  */
		case VIDIOCSCHAN:
		{
			struct video_channel *v=(struct video_channel *)arg;
			
			#ifdef DEBUG
				printk (KERNEL_PREFIX "VIDIOCSCHAN\n");
			#endif
			
			#ifdef DEBUG
				printk("Channel=%d Norm=%d\n", v->channel, v->norm);
			#endif
			
			if(v->channel != 0)
				return -EINVAL;
			
			return 0;
		}
		
		/* Get picture properties */
		case VIDIOCGPICT:
		{
			struct video_picture *v=(struct video_picture *)arg;

			#ifdef DEBUG
				printk (KERNEL_PREFIX "VIDIOCGPICT\n");
			#endif
			
			v->brightness = brightness; 
			v->hue = hue; 
			v->colour = colour; 
			v->contrast = contrast; 
			v->whiteness = whiteness;
			v->depth = depth;
			v->palette = _palette[palette];

			return 0;
		}
		
		/* Set picture properties */
		case VIDIOCSPICT:
		{
			struct video_picture *v=(struct video_picture *)arg;

 			#ifdef DEBUG
 				printk (KERNEL_PREFIX "VIDIOCSPICT\n");
 			#endif 			

			brightness = v->brightness;
			hue = v->hue;
			colour = v->colour;
			contrast = v->contrast;
 			
			/*
			// It "seems" not to be useful.. I don't remember why I did this test ?! But it blocks using UYVY 
			// palette with mencoder/mplayer, so I disable it for the moment, until I remember its use..
			if(v->depth != depth || v->palette != _palette[palette]) {
 				#ifdef DEBUG
					printk (KERNEL_PREFIX "Attempt to change picture properties.. palette wanted=%s, depth wanted=%d\n",palette_name[v->palette],v->depth);
 				#endif
 				return -EINVAL;
 			}*/

			return 0;
		}
		
		/* Sync with mmap grabbing */
		case VIDIOCSYNC:
		{
			#ifdef DEBUG
				printk (KERNEL_PREFIX "VIDIOCSYNC\n");
			#endif
			return 0;
		}
		
		/* Memory map buffer info */
		case VIDIOCGMBUF:
		{
			struct video_mbuf *buf = (struct video_mbuf*)arg;
			
			#ifdef DEBUG
				printk (KERNEL_PREFIX "VIDIOCGMBUF\n");
			#endif
			buf->frames = 1;
			buf->offsets[0] = 0;
			buf->size = BUFFER_SIZE;
			
			return 0;
		}
		
		/* Get the video overlay window */
		case VIDIOCGWIN:
		{
			struct video_window *v=(struct video_window *)arg;

			#ifdef DEBUG
				printk (KERNEL_PREFIX "VIDIOCGWIN\n");
			#endif

			v->width=width;	
			v->height=height;

			return 0;
		}
		
		/* Set the video overlay window - passes clip list for hardware smarts , chromakey etc */
		case VIDIOCSWIN:
		{
			struct video_window *v=(struct video_window *)arg;
			
			#ifdef DEBUG
				printk (KERNEL_PREFIX "VIDIOCSWIN\n");
			#endif

			if ((v->width != width) || (v->height != height))
				return -EINVAL;

			if ((v->clipcount) || (v->clips)) 
				return -EINVAL;
 			
			if (v->flags) 
				return -EINVAL;

			memcpy(&capture_win, v, sizeof(struct video_window));

 			return 0;
		}
		
		/* Start, end capture */
		case VIDIOCCAPTURE:
			return -EINVAL;
		
		/* Grab frames */
		case VIDIOCMCAPTURE:
		{
			
			#ifdef DEBUG
				struct video_mmap *vm = (struct video_mmap *)arg;
				printk (KERNEL_PREFIX "VIDIOCMCAPTURE: frame= %d w= %d h= %d format= %d\n", vm->frame, vm->width, vm->height, vm->format);
			#endif			
	
			/* if fps<0, reader controls the framerate */
			if (fps < 0) {
				mutex_unlock(&lock);
			}
			
			return 0;
		}
		
		/* Set frame buffer - root only */
		case  VIDIOCSFBUF: 
		{
			
			#ifdef DEBUG		
				struct video_buffer *v=(struct video_buffer *)arg;
				printk(KERNEL_PREFIX "Display at %p is %d by %d, bpl %d at %d depth\n", v->base, v->width, v->height, v->bytesperline,v->depth); 			
			#endif
			
			return 0;
			
		}
		
		/* Get frame buffer */
		case  VIDIOCGFBUF:			
		{
			struct video_buffer *v=(struct video_buffer *)arg;

			
			v->base = 0;
			v->height = height;
			v->width = width;
			v->depth = depth;
			v->bytesperline=width*(depth>>3);

			
			#ifdef DEBUG
				printk(KERNEL_PREFIX "VIDIOCGFBUF : %d\n",v->bytesperline);
			#endif
			
			return 0;
		}
		
		case  VIDIOCGCAPTURE:
		case  VIDIOCSCAPTURE:
		case  VIDIOCGUNIT:
		case  VIDIOCKEY:
		case  VIDIOCGTUNER:
		case  VIDIOCSTUNER:
		case  VIDIOCGFREQ:
		case  VIDIOCSFREQ:
		case  VIDIOCGAUDIO:
		case  VIDIOCSAUDIO:
		
		case  VIDIOCSPLAYMODE:
		case  VIDIOCSWRITEMODE:
		case  VIDIOCGPLAYINFO:
		case  VIDIOCSMICROCODE:
		case  VIDIOCGVBIFMT:
		case  VIDIOCSVBIFMT:
			#ifdef DEBUG
				printk (KERNEL_PREFIX "command not handled : %d\n", cmd);
			#endif
		
		
		default:
			#ifdef DEBUG
				printk (KERNEL_PREFIX "command unknown : %d\n",cmd);
			#endif
			break;
	}
	
	
	return -ENOIOCTLCMD;

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

static int v4l_read(struct file *file, char *buf, size_t count, loff_t *ppos) {
	
	#ifdef DEBUG
		printk(KERNEL_PREFIX "entering v4l_read()\n");
	#endif
	
	
	// if input size superior to the buffered image size
	if (count > BUFFER_SIZE) {
		printk(KERNEL_PREFIX "ERROR : you are attempting to read too much data : %d/%lu\n",count,BUFFER_SIZE);
		return -EINVAL;
	}
	memcpy(buf,image,count*sizeof(char));
	
	#ifdef DEBUG
		printk(KERNEL_PREFIX "leaving v4l_read() : %d - %ld\n",count,BUFFER_SIZE);
	#endif
	
	return count;
}

static int v4l_write(struct file *file, const char *buf, size_t count, loff_t *ppos) {
	
	#ifdef DEBUG
		printk(KERNEL_PREFIX "v4l_write() : %d/%lu\n",count,BUFFER_SIZE);
	#endif
	
	int waiting_time;
	struct timeval current_time;
	
	// in case user wants to change the video parameters, we check if "buf" contains the necessary string
	int l__width=0,l__height=0,l__fps=0,l__depth=0,l__palette=0;	


	if (buf[0]=='w' && buf[1]=='i' && buf[2]=='d') {
		// we use image buffer to read palette name
		char *s__palette=image;
	
		int l__ret = sscanf(buf,"width=%d height=%d fps=%d palette=%s depth=%d",&l__width,&l__height,&l__fps,s__palette,&l__depth);

		if ( l__ret == 5 ) {

			int l__newSize;

			// Check arguments
			int p=0;
			while (palette_name[p]) {
				if (!strcmp(palette_name[p],s__palette)) {
					break;
				}
				p++;
			}
			if (palette_name[p]) {
				l__palette=p;
			} else { 
				printk (KERN_INFO "ERROR : Incorrect palette number\n");
				return -EINVAL;
			}
 
			if (l__depth<1 || l__depth>32 ) {
				printk (KERN_INFO "ERROR : Incorrect depth\n");
				return -EINVAL;
			}

			if ( l__width <= 0 || l__height <= 0 ) {
				printk (KERN_INFO "ERROR : Incorrect width and/or height values\n");
				return -EINVAL;
			}

			if ( l__fps < 0 ) {
				printk (KERN_INFO "WARNING : Framerate is synchronized on reader !!\n");;
				l__fps=-1;
				mutex_init(&lock);
			}			
				
			width=l__width;
			height=l__height;
			fps=l__fps;
			palette=l__palette;
			depth=l__depth;

 
			// we recalculate the new size
			l__newSize=BUFFER_SIZE_MACRO;

			// if the current buffer is too small
			if (BUFFER_SIZE<l__newSize) {

				// we unallocate the old image
				if (image) {
					vfree(image);
					image=NULL;
				}
				
				BUFFER_SIZE=l__newSize;

				// and finally, we allocate memory for this new image
				image = vmalloc (BUFFER_SIZE);
				if (!image) {
					BUFFER_SIZE=0;
					printk (KERN_INFO "Failed vmalloc. Try to set or change parameters again.\n");
					return -EINVAL;
				}
			}

			printk(KERNEL_PREFIX "New video parameters : width=%d - height=%d - fps=%d - palette=%s - depth=%d\n",width,height,fps,palette_name[palette],depth);		

		}
 	}

	
	// if input size superior to the buffered image size
	if (count > BUFFER_SIZE) {
		printk(KERNEL_PREFIX "ERROR : you are attempting to write too much data\n");
		return -EINVAL;
	}
	
	
	
	// Copy of the input image
	if (copy_from_user((void*)image, (void*)buf, count)) {
		printk (KERN_INFO "failed copy_from_user()\n");
		return -EFAULT;
	}

	// Wait to simulate FPS	if necessary
	if ( fps > 0 ) {		
		do_gettimeofday(&current_time);
		//printk(KERNEL_PREFIX "t2=%d.%d - t1=%d.%d\n",current_time.tv_sec,current_time.tv_usec,timer.tv_sec,timer.tv_usec);

		// Compute waiting_time
		waiting_time = HZ/fps - HZ*( (current_time.tv_sec-timer.tv_sec)*USEC_PER_SEC  +  (current_time.tv_usec-timer.tv_usec)  )/USEC_PER_SEC;
		//waiting_time = MSEC_PER_SEC/fps - ( (current_time.tv_sec-timer.tv_sec)*MSEC_PER_SEC  +  (current_time.tv_usec-timer.tv_usec)/USEC_PER_MSEC  );

		// waits
		if (waiting_time > 0) {
			// solution 1
			interruptible_sleep_on_timeout (&wait,waiting_time);

			// solution 2
			//set_current_state(TASK_INTERRUPTIBLE);
			//schedule_timeout(waiting_time);
			
			// solution 3
			//msleep_interruptible(waiting_time);
		}

		do_gettimeofday(&timer);
	}
	// Wait reader before continue
	else if ( fps < 0 ) {
		#ifdef DEBUG
			printk (KERN_INFO "fps value : %d\n",fps);
		#endif
		mutex_lock_interruptible(&lock);
	}
	
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
	ioctl:		v4l_ioctl,
	compat_ioctl: v4l_compat_ioctl32,
	llseek:     no_llseek,
};
static struct video_device my_device = {
	name:		"Dummy video device",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	type:		VID_TYPE_CAPTURE,
#else
	vfl_type:	VID_TYPE_CAPTURE,
#endif
	fops:       &v4l_fops,
	release:	&release,
	minor:		-1,
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

	// we check that video size and fps values are correct
	if ( width <= 0 || height <= 0 ) {
		printk (KERN_INFO "ERROR : Incorrect width and/or height values\n");
		return -EINVAL;
	} else if ( fps < 0 ) {
		printk (KERN_INFO "WARNING : Framerate is synchronized on reader !!\n");
		mutex_init(&lock);
	}

	// image buffer size is computed
	BUFFER_SIZE=BUFFER_SIZE_MACRO;

	
	// allocation of the memory used to save the image
	image = vmalloc (BUFFER_SIZE);
	if (!image) {
		printk (KERN_INFO "failed vmalloc\n");
		return -EINVAL;
	}
	memset(image,0,BUFFER_SIZE);
	
	printk(KERNEL_PREFIX "module installed: fps=%d palette=%s (memory allocated to fit up to: width=%d - height=%d - depth=%d)\n",fps,palette_name[palette],width,height,depth);
 		
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


MODULE_DESCRIPTION("AVLD - V4L dummy video device");
MODULE_VERSION("0.1.4");
MODULE_AUTHOR("Pierre PARENT");
MODULE_LICENSE("GPL");

/* Parameters to be able to change the defaults width, height and framerate values of the video*/
module_param(width, int, 0);
MODULE_PARM_DESC(width,"width of the video");

module_param(height,int,0);
MODULE_PARM_DESC(height,"height of the video");

module_param(fps,int,0);
MODULE_PARM_DESC(fps,"framerate of the video in FPS (frame per second)");

module_param(palette,int,0);
MODULE_PARM_DESC(palette,"v4l palette used, see linux/videodev.h");

module_param(depth,int,0);
MODULE_PARM_DESC(depth,"bitdepth");


/*
	VIDIOCGCAP		_IOR('v',1,struct video_capability)     		// Get capabilities
	VIDIOCGCHAN		_IOWR('v',2,struct video_channel)       		// Get channel info (sources)
	VIDIOCSCHAN		_IOW('v',3,struct video_channel)        		// Set channel 
	VIDIOCGPICT		_IOR('v',6,struct video_picture)        		// Get picture properties
	VIDIOCSPICT		_IOW('v',7,struct video_picture)        		// Set picture properties
	VIDIOCGWIN		_IOR('v',9, struct video_window)        		// Get the video overlay window 
	VIDIOCSWIN		_IOW('v',10, struct video_window)       		// Set the video overlay window - passes clip list for hardware smarts , chromakey etc 
	VIDIOCGMBUF		_IOR('v',20, struct video_mbuf)         		// Memory map buffer info 
	VIDIOCMCAPTURE	_IOW('v',19, struct video_mmap)         		// Grab frames 
	VIDIOCSYNC		_IOW('v',18, int)                       		// Sync with mmap grabbing 
	VIDIOCGCAPTURE	_IOR('v',22, struct video_capture)      		// Get subcapture 
	VIDIOCSCAPTURE	_IOW('v',23, struct video_capture)      		// Set subcapture 
	VIDIOCGUNIT		_IOR('v',21, struct video_unit)         		// Get attached units 
	VIDIOCCAPTURE	_IOW('v',8,int)                         		// Start, end capture 
	VIDIOCGFBUF		_IOR('v',11, struct video_buffer)       		// Get frame buffer 
	VIDIOCSFBUF		_IOW('v',12, struct video_buffer)       		// Set frame buffer - root only 
	VIDIOCKEY		_IOR('v',13, struct video_key)          		// Video key event - to dev 255 is to all - cuts capture on all DMA windows with this key (0xFFFFFFFF == all) 

	// tuner interface - we have none 
	VIDIOCGTUNER	_IOWR('v',4,struct video_tuner)         		// Get tuner abilities 
	VIDIOCSTUNER	_IOW('v',5,struct video_tuner)          		// Tune the tuner for the current channel 
	VIDIOCGFREQ		_IOR('v',14, unsigned long)             		// Set tuner 
	VIDIOCSFREQ		_IOW('v',15, unsigned long)             		// Set tuner 
	// audio interface - we have none 
	VIDIOCGAUDIO	_IOR('v',16, struct video_audio)        		// Get audio info 
	VIDIOCSAUDIO	_IOW('v',17, struct video_audio)        		//Audio source, mute etc 
	
	VIDIOCSPLAYMODE 	_IOW('v',24, struct video_play_mode)    / Set output video mode/feature
	VIDIOCSWRITEMODE	_IOW('v',25, int)                       // Set write mode 
	VIDIOCGPLAYINFO		_IOR('v',26, struct video_info)         // Get current playback info from hardware 
	VIDIOCSMICROCODE	_IOW('v',27, struct video_code)         // Load microcode into hardware 
	VIDIOCGVBIFMT       _IOR('v',28, struct vbi_format)         // Get VBI information 
	VIDIOCSVBIFMT       _IOW('v',29, struct vbi_format)         // Set VBI information 
*/

/*
 * How to test v4l2loopback:
 * 1. launch this test program (even in background), it will initialize the
 *    loopback device and keep it open so it won't loose the settings.
 * 2. Feed the video device with data according to the settings specified
 *    below: size, pixelformat, etc.
 *    For instance, you can try the default settings with this command:
 *    mencoder video.avi -ovc raw -nosound -vf scale=640:480,format=yuy2 -o /dev/video1
 *    TODO: a command that limits the fps would be better :)
 *
 * Test the video in your favourite viewer, for instance:
 *   luvcview -d /dev/video1 -f yuyv
 */

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define ROUND_UP_2(num)  (((num)+1)&~1)
#define ROUND_UP_4(num)  (((num)+3)&~3)
#define ROUND_UP_8(num)  (((num)+7)&~7)
#define ROUND_UP_16(num) (((num)+15)&~15)
#define ROUND_UP_32(num) (((num)+31)&~31)
#define ROUND_UP_64(num) (((num)+63)&~63)




#if 0
# define CHECK_REREAD
#endif

#define VIDEO_DEVICE "/dev/video0"
#if 1
# define FRAME_WIDTH  640
# define FRAME_HEIGHT 480
#else
# define FRAME_WIDTH  512
# define FRAME_HEIGHT 512
#endif

#if 0
# define FRAME_FORMAT V4L2_PIX_FMT_YUYV
#else
# define FRAME_FORMAT V4L2_PIX_FMT_YVU420
#endif

static int debug=0;


int format_properties(const unsigned int format,
		const unsigned int width,
		const unsigned int height,
		size_t*linewidth,
		size_t*framewidth) {
size_t lw, fw;
	switch(format) {
	case V4L2_PIX_FMT_YUV420: case V4L2_PIX_FMT_YVU420:
		lw = width; /* ??? */
		fw = ROUND_UP_4 (width) * ROUND_UP_2 (height);
		fw += 2 * ((ROUND_UP_8 (width) / 2) * (ROUND_UP_2 (height) / 2));
	break;
	case V4L2_PIX_FMT_UYVY: case V4L2_PIX_FMT_Y41P: case V4L2_PIX_FMT_YUYV: case V4L2_PIX_FMT_YVYU:
		lw = (ROUND_UP_2 (width) * 2);
		fw = lw * height;
	break;
	default:
		return 0;
	}

	if(linewidth)*linewidth=lw;
	if(framewidth)*framewidth=fw;
	
	return 1;
}


void print_format(struct v4l2_format*vid_format) {
  printf("	vid_format->type                =%d\n",	vid_format->type );
  printf("	vid_format->fmt.pix.width       =%d\n",	vid_format->fmt.pix.width );
  printf("	vid_format->fmt.pix.height      =%d\n",	vid_format->fmt.pix.height );
  printf("	vid_format->fmt.pix.pixelformat =%d\n",	vid_format->fmt.pix.pixelformat);
  printf("	vid_format->fmt.pix.sizeimage   =%d\n",	vid_format->fmt.pix.sizeimage );
  printf("	vid_format->fmt.pix.field       =%d\n",	vid_format->fmt.pix.field );
  printf("	vid_format->fmt.pix.bytesperline=%d\n",	vid_format->fmt.pix.bytesperline );
  printf("	vid_format->fmt.pix.colorspace  =%d\n",	vid_format->fmt.pix.colorspace );
}

int main(int argc, char**argv)
{
	struct v4l2_capability vid_caps;
	struct v4l2_format vid_format;

	size_t framesize;
	size_t linewidth;

	__u8*buffer;
	__u8*check_buffer;

  const char*video_device=VIDEO_DEVICE;
	int fdwr = 0;
	int ret_code = 0;

	int i;

	if(argc>1) {
		video_device=argv[1];
		printf("using output device: %s\n", video_device);
	}

	fdwr = open(video_device, O_RDWR);
	assert(fdwr >= 0);

	ret_code = ioctl(fdwr, VIDIOC_QUERYCAP, &vid_caps);
	assert(ret_code != -1);

	memset(&vid_format, 0, sizeof(vid_format));

	ret_code = ioctl(fdwr, VIDIOC_G_FMT, &vid_format);
  if(debug)print_format(&vid_format);

	vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vid_format.fmt.pix.width = FRAME_WIDTH;
	vid_format.fmt.pix.height = FRAME_HEIGHT;
	vid_format.fmt.pix.pixelformat = FRAME_FORMAT;
	vid_format.fmt.pix.sizeimage = framesize;
	vid_format.fmt.pix.field = V4L2_FIELD_NONE;
	vid_format.fmt.pix.bytesperline = linewidth;
	vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

  if(debug)print_format(&vid_format);
	ret_code = ioctl(fdwr, VIDIOC_S_FMT, &vid_format);

	assert(ret_code != -1);

	if(debug)printf("frame: format=%d\tsize=%d\n", FRAME_FORMAT, framesize);
  print_format(&vid_format);

	if(!format_properties(vid_format.fmt.pix.pixelformat,
                        vid_format.fmt.pix.width, vid_format.fmt.pix.height,
                        &linewidth,
                        &framesize)) {
		printf("unable to guess correct settings for format '%d'\n", FRAME_FORMAT);
	}
	buffer=(__u8*)malloc(sizeof(__u8)*framesize);
	check_buffer=(__u8*)malloc(sizeof(__u8)*framesize);

	memset(buffer, 0, framesize);
	memset(check_buffer, 0, framesize);
	for (i = 0; i < framesize; ++i) {
		//buffer[i] = i % 2;
		check_buffer[i] = 0;
	}





	write(fdwr, buffer, framesize);

#ifdef CHECK_REREAD
	do {
	/* check if we get the same data on output */
	int fdr = open(video_device, O_RDONLY);
	read(fdr, check_buffer, framesize);
	for (i = 0; i < framesize; ++i) {
		if (buffer[i] != check_buffer[i])
			assert(0);
	}
	close(fdr);
	} while(0);
#endif

	pause();

	close(fdwr);

	free(buffer);
	free(check_buffer);

	return 0;
}

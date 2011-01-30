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
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#if 0
# define CHECK_REREAD
#endif

#define VIDEO_DEVICE "/dev/video0"
#define FRAME_WIDTH  640
#define FRAME_HEIGHT 480

#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 2)

int main(int argc, char**argv)
{
	struct v4l2_capability vid_caps;
	struct v4l2_format vid_format;

	__u8 buffer[FRAME_SIZE];
	__u8 check_buffer[FRAME_SIZE];

        const char*video_device=VIDEO_DEVICE;
	if(argc>1) {
		video_device=argv[1];
		printf("using output device: %s\n", video_device);
	}

	int i;
	for (i = 0; i < FRAME_SIZE; ++i) {
		buffer[i] = i % 2;
		check_buffer[i] = 0;
	}

	int fdwr = open(video_device, O_RDWR);
	assert(fdwr >= 0);

	int ret_code = ioctl(fdwr, VIDIOC_QUERYCAP, &vid_caps);
	assert(ret_code != -1);

	memset(&vid_format, 0, sizeof(vid_format));

	vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vid_format.fmt.pix.width = FRAME_WIDTH;
	vid_format.fmt.pix.height = FRAME_HEIGHT;
	vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	vid_format.fmt.pix.sizeimage = FRAME_WIDTH * FRAME_HEIGHT * 2;
	vid_format.fmt.pix.field = V4L2_FIELD_NONE;
	vid_format.fmt.pix.bytesperline = FRAME_WIDTH * 2;
	vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	ret_code = ioctl(fdwr, VIDIOC_S_FMT, &vid_format);
	assert(ret_code != -1);

	write(fdwr, buffer, FRAME_SIZE);

#ifdef CHECK_REREAD
	do {
	/* check if we get the same data on output */
	int fdr = open(video_device, O_RDONLY);
	read(fdr, check_buffer, FRAME_SIZE);
	for (i = 0; i < FRAME_SIZE; ++i) {
		if (buffer[i] != check_buffer[i])
			assert(0);
	}
	close(fdr);
	} while(0);
#endif

	pause();

	close(fdwr);

	return 0;
}

/* Read a yuv file directly and play with infinte loop
 *
 * Example:
 * $ ./yuv420_infiniteloop /dev/video1 akiyo_qcif.yuv 176 144 30

 * This will loop a yuv file named akiyo_qcif.yuv over video 1
 * 
 * Modified by T. Xu <x.tongda@nyu.edu> from yuv4mpeg_to_v4l2 example, 
 * original Copyright (C) 2011  Eric C. Cooper <ecc@cmu.edu>
 * Released under the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

char *prog;

struct yuv_setup {
	char *device;
	char *file_name;
	int frame_width;
	int frame_height;
	int frame_bytes;
	float fps;
};

void
fail(char *msg)
{
	fprintf(stderr, "%s: %s\n", prog, msg);
	exit(1);
}

struct yuv_setup 
process_args(int argc, char **argv)
{
	prog = argv[0];
	struct yuv_setup setup;	
	if (argc != 6){
		fail("invalid argument");
	} else {
		setup.device = argv[1];
		setup.file_name = argv[2];
		setup.frame_width = atoi(argv[3]);
		setup.frame_height = atoi(argv[4]);
		setup.frame_bytes = 3 * setup.frame_height * setup.frame_width / 2;
		setup.fps = atof(argv[5]);
	}
	return setup;
}

void
copy_frames(struct yuv_setup setup, int dev_fd)
{	

	FILE * yuv_file = fopen (setup.file_name,"rb");
	if (yuv_file == NULL){
		fail("can not open yuv file");
	}

	char *frame = malloc(setup.frame_bytes);

	if (frame == NULL) {
		fail("cannot malloc frame");
	}

	while (1) {
		int read_size = fread(frame, 1, setup.frame_bytes, yuv_file);
		usleep(1.0f/setup.fps * 1000000.0f);
		if (read_size == setup.frame_bytes){
			write(dev_fd, frame, setup.frame_bytes);
		} else if (read_size == 0){
			fclose(yuv_file);
			yuv_file = fopen (setup.file_name,"rb");
		} else {
			free(frame);
			fail("invalid frame size or file ending");
		}
	}

  free(frame);
}

int
open_video(struct yuv_setup setup)
{
	struct v4l2_format v;

	int dev_fd = open(setup.device, O_RDWR);
	if (dev_fd == -1) {
		fail("cannot open video device");
	}
	v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (ioctl(dev_fd, VIDIOC_G_FMT, &v) == -1){
		fail("cannot setup video device");
	}
	v.fmt.pix.width = setup.frame_width;
	v.fmt.pix.height = setup.frame_height;
	v.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	v.fmt.pix.sizeimage = setup.frame_bytes;
	v.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(dev_fd, VIDIOC_S_FMT, &v) == -1){
		fail("cannot setup video device");
	}

	return dev_fd;
}

int
main(int argc, char **argv)
{
	struct yuv_setup loc_setup = process_args(argc, argv);
	int loc_dev = open_video(loc_setup);
	copy_frames(loc_setup, loc_dev);
	return 0;
}

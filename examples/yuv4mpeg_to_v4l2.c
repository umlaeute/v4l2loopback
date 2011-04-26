/*
 * Copy a YUV4MPEG stream to a v4l2 output device.
 * The stream is read from standard input.
 * The device can be specified as argument; it defaults to /dev/video0.
 *
 * Example using mplayer as a producer for the v4l2loopback driver:
 *
 * $ mkfifo /tmp/pipe
 * $ ./yuv4mpeg_to_v4l2 < /tmp/pipe &
 * $ mplayer movie.mp4 -vo yuv4mpeg:file=/tmp/pipe
 *
 * Copyright (C) 2011  Eric C. Cooper <ecc@cmu.edu>
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

char *device;
int dev_fd;

int frame_width;
int frame_height;
int frame_bytes;

void
usage(void)
{
	fprintf(stderr, "Usage: %s [/dev/videoN]\n", prog);
	exit(1);
}

void
process_args(int argc, char **argv)
{
	prog = argv[0];
	switch (argc) {
	case 1:
		device = "/dev/video0";
		break;
	case 2:
		device = argv[1];
		break;
	default:
		usage();
		break;
	}
}

void
sysfail(char *msg)
{
	perror(msg);
	exit(1);
}

void
fail(char *msg)
{
	fprintf(stderr, "%s: %s\n", prog, msg);
	exit(1);
}

void
bad_header(char *kind)
{
	char msg[64];

	sprintf(msg, "malformed %s header", kind);
	fail(msg);
}

void
do_tag(char tag, char *value)
{
	switch (tag) {
	case 'W':
		frame_width = strtoul(value, NULL, 10);
		break;
	case 'H':
		frame_height = strtoul(value, NULL, 10);
		break;
	}
}

int
read_header(char *magic)
{
	char *p, *q;
	size_t n;
	int first, done;

	p = NULL;
	if (getline(&p, &n, stdin) == -1) return 0;
	q = p;
	first = 1;
	done = 0;
	while (!done) {
		while (*q != ' ' && *q != '\n')
			if (*q++ == '\0') bad_header(magic);
		done = (*q == '\n');
		*q = '\0';
		if (first)
			if (strcmp(p, magic) == 0) first = 0;
			else bad_header(magic);
		else
			do_tag(*p, p + 1);
		p = ++q;
	}
	return 1;
}

void
process_header(void)
{
	if (!read_header("YUV4MPEG2")) fail("missing YUV4MPEG2 header");
	frame_bytes = 3 * frame_width * frame_height / 2;
	if (frame_bytes == 0) fail("frame width or height is missing");
}

void
copy_frames(void)
{
	char *frame;

	frame = malloc(frame_bytes);
	if (frame == NULL) fail("cannot malloc frame");
	while (read_header("FRAME")) {
		if (fread(frame, 1, frame_bytes, stdin) != frame_bytes)
			fail("malformed frame");
		else if (write(dev_fd, frame, frame_bytes) != frame_bytes)
			sysfail("write");
	}
}

#define vidioc(op, arg) \
	if (ioctl(dev_fd, VIDIOC_##op, arg) == -1) \
		sysfail(#op); \
	else

void
open_video(void)
{
	struct v4l2_format v;

	dev_fd = open(device, O_RDWR);
	if (dev_fd == -1) sysfail(device);
	v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vidioc(G_FMT, &v);
	v.fmt.pix.width = frame_width;
	v.fmt.pix.height = frame_height;
	v.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	v.fmt.pix.sizeimage = frame_bytes;
	vidioc(S_FMT, &v);
}

int
main(int argc, char **argv)
{
	process_args(argc, argv);
	process_header();
	open_video();
	copy_frames();
	return 0;
}

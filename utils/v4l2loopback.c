#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "v4l2loopback.h"

#define CONTROLDEVICE "/dev/v4l2loopback"

#if 0
#define MARK() dprintf(2, "%s:%d @ %s\n", __FILE__, __LINE__, __func__)
#else
#define MARK()
#endif

static void help_shortcmdline(const char *program, const char *argstring)
{
	dprintf(2, "\n       %s %s", program, argstring);
}
static void help(const char *name, int status)
{
	dprintf(2, "usage: %s [general commands]", name);
	help_shortcmdline(name, "add {<args>} [<device>]");
	help_shortcmdline(name, "delete <device>");
	help_shortcmdline(name, "query <device>");
	help_shortcmdline(name, "set-fps <fps> <device>");
	help_shortcmdline(name, "get-fps <device>");
	help_shortcmdline(name, "set-caps <caps> <device>");
	help_shortcmdline(name, "get-caps <device>");
	help_shortcmdline(name, "set-timeout-image <image> <device>");
	dprintf(2, "\n\n");
	dprintf(2, "\n general commands"
		   "\n ================"
		   "\n\t-v : print version and exit"
		   "\n\t-h : print this help and exit"
		   "\n\n");
	dprintf(2,
		"\n adding devices ('add')"
		"\n ======================"
		"\n <args>  \tany of the following arguments may be present"
		"\n\t -n <name>           : pretty name for the device"
		"\n\t -w <max_width>      : maximum allowed frame width"
		"\n\t -h <max_height>     : maximum allowed frame height"
		"\n\t -x <exclusive_caps> : whether to announce OUTPUT/CAPTURE capabilities exclusively"
		"\n\t -b <buffers>        : buffers to queue"
		"\n\t -o <max_openers>    : maximum allowed concurrent openers"
		"\n\t -v                  : verbose mode (print properties of device after successfully creating it)"
		"\n"
		"\n <device>\tif given, create a specific device (otherwise just create a free one)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n");
	dprintf(2,
		"\n deleting devices ('delete')"
		"\n ==========================="
		"\n <device>\tcan be given one more more times (to delete multiple devices at once)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n");
	dprintf(2,
		"\n querying devices ('query')"
		"\n =========================="
		"\n <device>\tcan be given one more more times (to query multiple devices at once)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n");
	dprintf(2,
		"\n setting framerate ('set-fps')"
		"\n ============================="
		"\n    <fps>\tframes per second, either as integer ('30') or fraction ('50/2')."
		"\n <device>\teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n");
	dprintf(2,
		"\n setting capabilities ('set-caps')"
		"\n ================================="
		"\n    <caps>\tformat specification."
		"\n <device>\teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n");
	dprintf(2,
		"\n setting timeout image ('set-timeout-image')"
		"\n ==========================================="
		"\n  <image>\timage file"
		"\n <device>\teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n");
	exit(status);
}
static void usage(const char *name)
{
	help(name, 1);
}

static int my_atoi(const char *name, const char *s)
{
	char *endptr = 0;
	int n = strtol(s, &endptr, 10);
	if (*endptr) {
		dprintf(2, "%s must be a number (got: '%s')\n", name, s);
		exit(1);
	}
	return n;
}

static int parse_device(const char *devicename)
{
	int ret = strncmp(devicename, "/dev/video", 10);
	const char *device = (ret) ? devicename : (devicename + 10);
	char *endptr = 0;
	int dev = strtol(device, &endptr, 10);
	if (*endptr)
		return -1;
	return dev;
}

static void print_conf(struct v4l2_loopback_config *cfg)
{
	MARK();
	if (!cfg) {
		printf("configuration: %s\n", cfg);
		return;
	}
	MARK();
	printf("\tcapture_device#  : %d"
	       "\n\toutput_device#   : %d"
	       "\n\tcard_label       : %s"
	       "\n\tmax_width        : %d"
	       "\n\tmax_height       : %d"
	       "\n\tannounce_all_caps: %d"
	       "\n\tmax_buffers      : %d"
	       "\n\tmax_openers      : %d"
	       "\n\tdebug            : %d"
	       "\n",
	       cfg->capture_nr, cfg->output_nr, cfg->card_label, cfg->max_width,
	       cfg->max_height, cfg->announce_all_caps, cfg->max_buffers,
	       cfg->max_openers, cfg->debug);
	MARK();
}

static struct v4l2_loopback_config *make_conf(struct v4l2_loopback_config *cfg,
					      const char *label, int max_width,
					      int max_height,
					      int exclusive_caps, int buffers,
					      int openers, int device)
{
	if (!cfg)
		return 0;
	if (!label && max_width <= 0 && max_height <= 0 && exclusive_caps < 0 &&
	    buffers <= 0 && openers <= 0 && device < 0)
		return 0;
	cfg->capture_nr = -1;
	cfg->output_nr = device;
	cfg->card_label[0] = 0;
	if (label)
		snprintf(cfg->card_label, 32, "%s", label);
	cfg->max_height = max_height;
	cfg->max_width = max_width;
	cfg->announce_all_caps = (exclusive_caps < 0) ? -1 : !exclusive_caps;
	cfg->max_buffers = buffers;
	cfg->max_openers = openers;
	cfg->debug = 0;
	return cfg;
}

static void add_device(int fd, struct v4l2_loopback_config *cfg, int verbose)
{
	MARK();
	int ret = ioctl(fd, V4L2LOOPBACK_CTL_ADD, cfg);
	MARK();
	if (ret < 0) {
		perror("failed to create device");
		return;
	}
	MARK();

	printf("/dev/video%d\n", ret);

	if (verbose > 0) {
		MARK();
		struct v4l2_loopback_config config;
		memset(&config, 0, sizeof(config));
		config.output_nr = config.capture_nr = ret;
		ret = ioctl(fd, V4L2LOOPBACK_CTL_QUERY, &config);
		if (!ret)
			perror("failed querying newly added device");
		MARK();
		print_conf(&config);
		MARK();
	}
}

static int delete_device(int fd, const char *devicename)
{
	int dev = parse_device(devicename);
	if (dev < 0) {
		dprintf(2, "ignoring illegal devicename '%s'\n", devicename);
		return 1;
	}
	if (ioctl(fd, V4L2LOOPBACK_CTL_REMOVE, dev) < 0)
		perror(devicename);

	return 0;
}

static int query_device(int fd, const char *devicename)
{
	int err;
	struct v4l2_loopback_config config;
	int dev = parse_device(devicename);
	if (dev < 0) {
		dprintf(2, "ignoring illegal devicename '%s'\n", devicename);
		return 1;
	}

	memset(&config, 0, sizeof(config));
	config.output_nr = config.capture_nr = dev;
	err = ioctl(fd, V4L2LOOPBACK_CTL_QUERY, &config);
	if (err)
		perror("query failed");
	else {
		printf("%s\n", devicename);
		print_conf(&config);
		return 0;
	}
	return err;
}
static int open_controldevice()
{
	int fd = open(CONTROLDEVICE, 0);
	if (fd < 0) {
		perror("unable to open control device '" CONTROLDEVICE "'");
		exit(1);
	}
}

static int open_sysfs_file(const char *devicename, const char *filename,
			   int flags)
{
	int fd = -1;
	char sysdev[100];
	int dev = parse_device(devicename);
	if (dev < 0) {
		dprintf(2, "ignoring illegal devicename '%s'\n", devicename);
		return -1;
	}
	snprintf(sysdev, sizeof(sysdev) - 1,
		 "/sys/devices/virtual/video4linux/video%d/%s", dev, filename);
	sysdev[sizeof(sysdev) - 1] = 0;
	fd = open(sysdev, flags);
	if (fd < 0) {
		perror("unable to open /sys-device");
		return -1;
	}
	return fd;
}

static int set_fps(const char *devicename, const char *fps)
{
	int result = 1;
	char _fps[100];
	int fd = open_sysfs_file(devicename, "format", O_WRONLY);
	if (fd < 0)
		return 1;
	snprintf(_fps, sizeof(_fps) - 1, "@%s", fps);
	_fps[sizeof(_fps) - 1] = 0;

	if (write(fd, _fps, strnlen(_fps, sizeof(_fps))) < 0) {
		perror("failed to set fps");
		goto done;
	}

	result = 0;
done:
	close(fd);
	return result;
}

static char *fourcc2str(unsigned int fourcc, char buf[4])
{
	buf[0] = (fourcc >> 0) & 0xFF;
	buf[1] = (fourcc >> 8) & 0xFF;
	buf[2] = (fourcc >> 16) & 0xFF;
	buf[3] = (fourcc >> 24) & 0xFF;

	return buf;
}
unsigned int str2fourcc(char buf[4])
{
	return (buf[0]) + (buf[1] << 8) + (buf[2] << 16) + (buf[3] << 24);
}

typedef struct _caps {
	unsigned int fourcc;
	int width, height;
	int fps_num, fps_denom;
} t_caps;

static void print_caps(t_caps *caps)
{
	char fourcc[4];
	if (!caps) {
		dprintf(2, "no caps\n");
		return;
	}
	dprintf(2, "FOURCC : %.4s\n", fourcc2str(caps->fourcc, fourcc));
	dprintf(2, "dimen  : %dx%d\n", caps->width, caps->height);
	dprintf(2, "fps    : %d/%d\n", caps->fps_num, caps->fps_denom);
}
static int parse_caps(const char *buffer, t_caps *caps)
{
	char fourcc[5];
	memset(caps, 0, sizeof(*caps));
	memset(fourcc, 0, sizeof(*fourcc));

	if (!(buffer && *buffer))
		return 1;

	if (sscanf(buffer, "%4c:%dx%d@%d/%d", &fourcc, &caps->width,
		   &caps->height, &caps->fps_num, &caps->fps_denom) <= 0) {
		if (sscanf(buffer, "%4c:%dx%d", &fourcc, &caps->width,
			   &caps->height, &caps->fps_num) <= 0) {
			dprintf(2, "oops...%s\n", buffer);
		}
	}
	caps->fourcc = str2fourcc(fourcc);
	return (0 == caps->fourcc);
}
static int read_caps(const char *devicename, t_caps *caps)
{
	int result = 1;
	char _caps[100];
	int fd = open_sysfs_file(devicename, "format", O_RDONLY);
	if (fd < 0)
		return 1;

	if (read(fd, _caps, 100) < 0) {
		perror("failed to read fps");
		goto done;
	}
	_caps[100 - 1] = 0;
	//dprintf(2, "fps: %s\n", _caps);
	if (caps) {
		if (parse_caps(_caps, caps)) {
			dprintf(2, "unable to parse format '%s'\n", _caps);
			goto done;
		}
	}
	result = 0;
done:
	close(fd);
	return result;
}

static int get_fps(const char *devicename)
{
	t_caps caps;
	if (read_caps(devicename, &caps))
		return 1;
	printf("%d/%d\n", caps.fps_num, caps.fps_denom);
	return 0;
}
static int set_caps(const char *devicename, const char *capsstring)
{
	int result = 1;
	int fd = -1;
	struct v4l2_format vid_format;
	struct v4l2_capability vid_caps;
	t_caps caps;
	if (parse_caps(capsstring, &caps)) {
		dprintf(2, "unable to parse format '%s'\n", capsstring);
		return 1;
	}
	print_caps(&caps);

	memset(&vid_format, 0, sizeof(vid_format));

	vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vid_format.fmt.pix.width = caps.width;
	vid_format.fmt.pix.height = caps.height;
	vid_format.fmt.pix.pixelformat = caps.fourcc;
	//vid_format.fmt.pix.sizeimage = w * h * m_image.csize;
	vid_format.fmt.pix.field = V4L2_FIELD_NONE;
	//vid_format.fmt.pix.bytesperline = w * m_image.csize;
	//vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

	fd = open(devicename, O_RDWR);
	if (fd < 0) {
		int devnr = parse_device(devicename);
		if (devnr >= 0) {
			char devname[100];
			snprintf(devname, 99, "/dev/video%d", devnr);
			devname[99] = 0;
			fd = open(devname, O_RDWR);
		}
	}

	if (fd < 0)
		goto done;

	if (ioctl(fd, VIDIOC_QUERYCAP, &vid_caps) == -1) {
		perror("VIDIOC_QUERYCAP");
	}

	if (ioctl(fd, VIDIOC_S_FMT, &vid_format) == -1) {
		perror("unable to set requested format");
		goto done;
	}

	if (caps.fps_num && caps.fps_denom) {
		char fps[100];
		snprintf(fps, 100, "%d/%d", caps.fps_num, caps.fps_denom);
		dprintf(2, "now setting fps to '%s'\n", fps);
		close(fd);
		fd = -1;
		return set_fps(devicename, fps);
	}

	result = 0;
done:
	close(fd);
	return result;
}
static int get_caps(const char *devicename)
{
	int format = 0;
	t_caps caps;
	char fourcc[4];
	if (read_caps(devicename, &caps))
		return 1;
	switch (format) {
	default:
		printf("%.4s:%dx%d@%d/%d\n", fourcc2str(caps.fourcc, fourcc),
		       caps.width, caps.height, caps.fps_num, caps.fps_denom);
		break;
	case 1: /* GStreamer-1.0 */

		/* FOURCC is different everywhere... */
		switch (caps.fourcc) {
		default:
			break;
		case 0x56595559: /* YUYV */
			caps.fourcc = str2fourcc("YUY2");
			break;
		}

		printf("video/x-raw,format=%.4s,width=%d,height=%d,framerate=%d/%d\n",
		       fourcc2str(caps.fourcc, fourcc), caps.width, caps.height,
		       caps.fps_num, caps.fps_denom);
		break;
	}
	return 0;
}
static int set_timeoutimage(const char *devicename, const char *fps)
{
	dprintf(2, "'set-timeout-image' not implemented yet.\n");
	return 1;
}

typedef enum {
	VERSION,
	HELP,
	ADD,
	DELETE,
	QUERY,
	SET_FPS,
	GET_FPS,
	SET_CAPS,
	GET_CAPS,
	SET_TIMEOUTIMAGE,
	_UNKNOWN
} t_command;
static t_command get_command(const char *command)
{
	if (!strncmp(command, "-h", 2))
		return HELP;
	if (!strncmp(command, "-?", 2))
		return HELP;
	if (!strncmp(command, "-v", 2))
		return VERSION;
	if (!strncmp(command, "add", 4))
		return ADD;
	if (!strncmp(command, "del", 3))
		return DELETE;
	if (!strncmp(command, "query", 5))
		return QUERY;
	if (!strncmp(command, "set-fps", 7))
		return SET_FPS;
	if (!strncmp(command, "get-fps", 7))
		return GET_FPS;
	if (!strncmp(command, "set-caps", 8))
		return SET_CAPS;
	if (!strncmp(command, "get-caps", 8))
		return GET_CAPS;
	if (!strncmp(command, "set-timeout-image", 17))
		return SET_TIMEOUTIMAGE;
	return _UNKNOWN;
}

int main(int argc, char **argv)
{
	int i;
	int fd = -1;
	int verbose = 0;
	t_command cmd;

	char *label = 0;
	int max_width = -1;
	int max_height = -1;
	int exclusive_caps = -1;
	int buffers = -1;
	int openers = -1;

	int c;

	if (argc < 2)
		usage(argv[0]);

	switch (get_command(argv[1])) {
	case _UNKNOWN:
		dprintf(2, "unknown command '%s'\n\n", argv[1]);
		usage(argv[0]);
		break;
	case HELP:
		help(argv[0], 0);
		break;
	case ADD:
		while ((c = getopt(argc - 1, argv + 1, "vn:w:h:x:b:o:")) != -1)
			switch (c) {
			case 'v':
				verbose++;
				break;
			case 'n':
				label = optarg;
				break;
			case 'w':
				max_width = my_atoi("max_width", optarg);
				break;
			case 'h':
				max_height = my_atoi("max_height", optarg);
				break;
			case 'x':
				exclusive_caps =
					my_atoi("exclusive_caps", optarg);
				break;
			case 'b':
				buffers = my_atoi("buffers", optarg);
				break;
			case 'o':
				openers = my_atoi("openers", optarg);
				break;
			case '?':
				usage(argv[0]);
				return 1;
			default:
				usage(argv[0]);
				return 1;
			}
		fd = open_controldevice();
		do {
			struct v4l2_loopback_config cfg;
			if ((optind + 1) == argc)
				add_device(fd,
					   make_conf(&cfg, label, max_width,
						     max_height, exclusive_caps,
						     buffers, openers, -1),
					   verbose);
			for (i = optind + 1; i < argc; i++) {
				int dev = parse_device(argv[i]);
				if (dev < 0) {
					dprintf(2,
						"skipping creation of illegal device '%s'\n",
						argv[1]);
				} else
					add_device(fd,
						   make_conf(&cfg, label,
							     max_width,
							     max_height,
							     exclusive_caps,
							     buffers, openers,
							     dev),
						   verbose);
			}
		} while (0);
		break;
	case DELETE:
		if (argc == 2)
			usage(argv[0]);
		fd = open_controldevice();
		for (i = 2; i < argc; i++) {
			delete_device(fd, argv[i]);
		}
		break;
	case QUERY:
		if (argc == 2)
			usage(argv[0]);
		fd = open_controldevice();
		for (i = 2; i < argc; i++) {
			query_device(fd, argv[i]);
		}
		break;
	case SET_FPS:
		if (argc != 4)
			usage(argv[0]);
		set_fps(argv[3], argv[2]);
		break;
	case GET_FPS:
		if (argc != 3)
			usage(argv[0]);
		get_fps(argv[2]);
		break;
	case SET_CAPS:
		if (argc != 4)
			usage(argv[0]);
		set_caps(argv[3], argv[2]);
		break;
	case GET_CAPS:
		if (argc != 3)
			usage(argv[0]);
		get_caps(argv[2]);
		break;
	case SET_TIMEOUTIMAGE:
		if (argc != 4)
			usage(argv[0]);
		set_timeoutimage(argv[3], argv[2]);
		break;
	case VERSION:
		printf("%s v%d.%d.%d\n", argv[0], V4L2LOOPBACK_VERSION_MAJOR,
		       V4L2LOOPBACK_VERSION_MINOR, V4L2LOOPBACK_VERSION_BUGFIX);
		break;
	default:
		dprintf(2, "not implemented '%s'\n", argv[1]);
		break;
	}

	if (fd >= 0)
		close(fd);

	return 0;
}

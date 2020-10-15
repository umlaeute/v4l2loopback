#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include <errno.h>

#include "v4l2loopback.h"

#define CONTROLDEVICE "/dev/v4l2loopback"

#if 0
#define MARK() dprintf(2, "%s:%d @ %s\n", __FILE__, __LINE__, __func__)
#else
#define MARK()
#endif

/********************/
/* helper functions */

/* running externals programs */
static char *which(char *outbuf, size_t bufsize, const char *filename)
{
	struct stat statbuf;
	char *paths, *saveptr = NULL;
	if (filename && '/' == *filename) {
		/* an absolute filename */
		int err = stat(filename, &statbuf);
		if (!err) {
			snprintf(outbuf, bufsize, "%s", filename);
			return outbuf;
		}
		return NULL;
	}
	for (paths = getenv("PATH");; paths = NULL) {
		char *path = strtok_r(paths, ":", &saveptr);
		int err;
		if (path == NULL)
			return NULL;
		snprintf(outbuf, bufsize, "%s/%s", path, filename);
		err = stat(outbuf, &statbuf);
		if (!err)
			return outbuf;
	}
	return NULL;
}

static pid_t pid;
void exec_cleanup(int signal)
{
	if (pid) {
		switch (signal) {
		default:
			break;
		case SIGINT:
			kill(pid, SIGTERM);
			break;
		}
	}

	while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {
	}
}
static int my_execv(char *const *cmdline)
{
	char exe[1024];
	//pid_t pid;
	int res = 0;
	char *const *argp = cmdline;
	if (!which(exe, 1024, cmdline[0])) {
		dprintf(2, "cannot find %s - is it installed???\n", cmdline[0]);
		return 1;
	}
#if 0
	dprintf(2, "%s:", exe);
	while (*argp) {
		dprintf(2, " %s", *argp++);
	};
	dprintf(2, "\n");
#endif

	pid = fork();
	if (pid == 0) {
		res = execv(exe, cmdline);
		if (res < 0) {
			dprintf(2, "ERROR running helper program (%d, %d)", res,
				errno);
			dprintf(2, "failed program was:\n\t");
			while (*cmdline)
				dprintf(2, " %s", *cmdline++);
			dprintf(2, "\n");
			exit(0);
		}
		exit(0);
	} else if (pid > 0) { // pid>0, parent, wait for child
		int status = 0;
		int waitoptions = 0;
		signal(SIGCHLD, exec_cleanup);
		signal(SIGINT, exec_cleanup);
		waitpid(pid, &status, waitoptions);
		pid = 0;
		if (WIFEXITED(status))
			return WEXITSTATUS(status);
		return 0;
	} else { //pid < 0, error
		dprintf(2, "ERROR: child fork failed\n");
		exit(1);
	}
	return 0;
}

/* misc */
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

/* helper functions */
/********************/

static unsigned int _get_control_id(int fd, const char *control)
{
	const size_t length = strnlen(control, 1024);
	const unsigned next = V4L2_CTRL_FLAG_NEXT_CTRL;
	struct v4l2_queryctrl qctrl;
	int id;

	memset(&qctrl, 0, sizeof(qctrl));
	while (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
		if (!strncmp(qctrl.name, control, length))
			return qctrl.id;
		qctrl.id |= next;
	}
	for (id = V4L2_CID_USER_BASE; id < V4L2_CID_LASTP1; id++) {
		qctrl.id = id;
		if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
			if (!strncmp(qctrl.name, control, length))
				return qctrl.id;
		}
	}
	for (qctrl.id = V4L2_CID_PRIVATE_BASE;
	     ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0; qctrl.id++) {
		if (!strncmp(qctrl.name, control, length)) {
			unsigned int id = qctrl.id;
			return id;
		}
	}
	return 0;
}

static int set_control_i(int fd, const char *control, int value)
{
	struct v4l2_control ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = _get_control_id(fd, control);
	ctrl.value = value;
	if (ctrl.id && ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0) {
		int value = ctrl.value;
		return value;
	}
	return 0;
}
static int get_control_i(int fd, const char *control)
{
	struct v4l2_control ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = _get_control_id(fd, control);

	if (ctrl.id && ioctl(fd, VIDIOC_G_CTRL, &ctrl) == 0) {
		int value = ctrl.value;
		return value;
	}
	return 0;
}

/********************/
/* main logic       */
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

static int help_shortcmdline(int brief, const char *program,
			     const char *argstring)
{
	dprintf(2, "\n");
	//if(!brief)dprintf(2, "  -->");
	dprintf(2, "\t");
	dprintf(2, "%s %s", program, argstring);
	return brief;
}
static void help_add(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n adding devices ('add')"
			   "\n ======================");
	if (help_shortcmdline(brief, program,
			      "add {<flags>} [<device> [<outputdevice>]]"))
		return;
	dprintf(2,
		"\n <flags>  \tany of the following flags may be present"
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
		"\n <outputdevice>\tif given, use separate output & capture devices (otherwise they are the same).");
}
static void help_delete(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n deleting devices ('delete')"
			   "\n ===========================");
	if (help_shortcmdline(brief, program, "delete <device>"))
		return;
	dprintf(2,
		"\n <device>\tcan be given one more more times (to delete multiple devices at once)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1').");
}
static void help_query(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n querying devices ('query')"
			   "\n ==========================");
	if (help_shortcmdline(brief, program, "query <device>"))
		return;
	dprintf(2,
		"\n <device>\tcan be given one more more times (to query multiple devices at once)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1').");
}
static void help_setfps(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n setting framerate ('set-fps')"
			   "\n =============================");
	if (help_shortcmdline(brief, program, "set-fps <device> <fps>"))
		return;
	dprintf(2,
		"\n <device>\teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n    <fps>\tframes per second, either as integer ('30') or fraction ('50/2').");
}
static void help_getfps(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n getting framerate ('get-fps')"
			   "\n =============================");
	if (help_shortcmdline(brief, program, "get-fps <device>"))
		return;
}
static void help_setcaps(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n setting capabilities ('set-caps')"
			   "\n =================================");
	if (help_shortcmdline(brief, program, "set-caps <device> <caps>"))
		return;
	dprintf(2,
		"\n <device>\teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n   <caps>\tformat specification, e.g. 'UYVY:3840x2160@60/1");
}
static void help_getcaps(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n getting capabilities ('get-caps')"
			   "\n =================================");
	if (help_shortcmdline(brief, program, "get-caps <device>"))
		return;
}
static void help_settimeoutimage(const char *program, int brief)
{
	if (!brief)
		dprintf(2, "\n setting timeout image ('set-timeout-image')"
			   "\n ===========================================");
	if (help_shortcmdline(brief, program,
			      "set-timeout-image {<flags>} <device> <image>"))
		return;
	dprintf(2,
		"\n  <flags>\tany of the following flags may be present"
		"\n\t -t <timeout>           : timeout (in ms)"
		"\n"
		"\n <device>\teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n  <image>\timage file");
}
static void help_none(const char *program, int brief)
{
}
typedef void (*t_help)(const char *, int);
static t_help get_help(t_command cmd)
{
	switch (cmd) {
	default:
		break;
	case ADD:
		return help_add;
	case DELETE:
		return help_delete;
	case QUERY:
		return help_query;
	case SET_FPS:
		return help_setfps;
	case GET_FPS:
		return help_getfps;
	case SET_CAPS:
		return help_setcaps;
	case GET_CAPS:
		return help_getcaps;
	case SET_TIMEOUTIMAGE:
		return help_settimeoutimage;
	}
	return help_none;
}

static void help(const char *name, int status)
{
	t_command cmd;
	dprintf(2, "usage: %s [general commands]", name);
	dprintf(2, "\n\n");
	dprintf(2, "\n general commands"
		   "\n ================"
		   "\n\t-v : print version and exit"
		   "\n\t-h : print this help and exit");
	/* brief helps */
	for (cmd = ADD; cmd < _UNKNOWN; cmd++)
		get_help(cmd)("", 1);
	dprintf(2, "\n\n");

	/* long helps */
	for (cmd = ADD; cmd < _UNKNOWN; cmd++) {
		get_help(cmd)(name, 0);
		dprintf(2, "\n\n");
	}

	exit(status);
}
static void usage(const char *name)
{
	help(name, 1);
}
static void usage_topic(const char *name, t_command cmd)
{
	t_help hlp = get_help(cmd);
	if (help_none == hlp)
		usage(name);
	else
		hlp(name, 0);
	dprintf(2, "\n");
	exit(1);
}

static const char *my_realpath(const char *path, char *resolved_path)
{
	char *str = realpath(path, resolved_path);
	return str ? str : path;
}
static int parse_device(const char *devicename_)
{
	char devicenamebuf[4096];
	const char *devicename = my_realpath(devicename_, devicenamebuf);
	int ret = strncmp(devicename, "/dev/video", 10);
	const char *device = (ret) ? devicename : (devicename + 10);
	char *endptr = 0;
	int dev = strtol(device, &endptr, 10);
	if (!*endptr)
		return dev;

	return -1;
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

static struct v4l2_loopback_config *
make_conf(struct v4l2_loopback_config *cfg, const char *label, int max_width,
	  int max_height, int exclusive_caps, int buffers, int openers,
	  int capture_device, int output_device)
{
	if (!cfg)
		return 0;
	if (!label && max_width <= 0 && max_height <= 0 && exclusive_caps < 0 &&
	    buffers <= 0 && openers <= 0 && capture_device < 0 &&
	    output_device < 0)
		return 0;
	cfg->capture_nr = capture_device;
	cfg->output_nr = output_device;
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

static int add_device(int fd, struct v4l2_loopback_config *cfg, int verbose)
{
	MARK();
	int ret = ioctl(fd, V4L2LOOPBACK_CTL_ADD, cfg);
	MARK();
	if (ret < 0) {
		perror("failed to create device");
		return 1;
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
	return (!ret);
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
static int open_videodevice(const char *devicename, int mode)
{
	int fd = open(devicename, mode);
	if (fd < 0) {
		int devnr = parse_device(devicename);
		if (devnr >= 0) {
			char devname[100];
			snprintf(devname, 99, "/dev/video%d", devnr);
			devname[99] = 0;
			fd = open(devname, mode);
		}
	}
	return fd;
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
	//dprintf(2, "%s\n", sysdev);
	return fd;
}

static int parse_fps(const char *fps, int *numerator, int *denominator)
{
	int num = 0;
	int denom = 1;
	if (sscanf(fps, "%d/%d", &num, &denom) <= 0) {
		return 1;
	}
	if (numerator)
		*numerator = num;
	if (denominator)
		*denominator = denom;
	return 0;
}
static int is_fps(const char *fps)
{
	return parse_fps(fps, 0, 0);
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
	int fd = open_videodevice(devicename, O_RDWR);
	struct v4l2_format vid_format;
	struct v4l2_capability vid_caps;
	t_caps caps;

	/* now open up the device */
	if (fd < 0)
		goto done;

	if (!strncmp("any", capsstring, 4)) {
		/* skip caps-parsing */
	} else if (parse_caps(capsstring, &caps)) {
		dprintf(2, "unable to parse format '%s'\n", capsstring);
		goto done;
	}
	//print_caps(&caps);

	/* check whether this is actually a video-device */
	if (ioctl(fd, VIDIOC_QUERYCAP, &vid_caps) == -1) {
		perror("VIDIOC_QUERYCAP");
		goto done;
	}

	if (!strncmp("any", capsstring, 4)) {
		set_control_i(fd, "keep_format", 0);
		//set_control_i(fd, "sustain_framerate", 0);
		result = 0;
		goto done;
	}

	/* try to get the default values for the format first */
	memset(&vid_format, 0, sizeof(vid_format));

	vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (ioctl(fd, VIDIOC_G_FMT, &vid_format) == -1) {
		perror("VIDIOC_G_FMT");
	}

	/* and set those caps that we have */
	if (caps.width)
		vid_format.fmt.pix.width = caps.width;
	if (caps.height)
		vid_format.fmt.pix.height = caps.height;
	if (caps.fourcc)
		vid_format.fmt.pix.pixelformat = caps.fourcc;

	if (ioctl(fd, VIDIOC_S_FMT, &vid_format) == -1) {
		perror("unable to set requested format");
		goto done;
	}

	set_control_i(fd, "keep_format", 1);

	/* finally, try setting the fps */
	if (caps.fps_num && caps.fps_denom) {
		char fps[100];
		int didit;
		snprintf(fps, 100, "%d/%d", caps.fps_num, caps.fps_denom);
		didit = set_fps(devicename, fps);
		if (!didit) {
			set_control_i(fd, "sustain_framerate", 1);
		}
		close(fd);
		fd = -1;
		return didit;
	}

	result = 0;
done:
	if (fd >= 0)
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
static int set_timeoutimage(const char *devicename, const char *imagefile,
			    int timeout)
{
	int fd = -1;
	char imagearg[4096], imagefile2[4096], devicearg[4096];
	char *args[] = { "gst-launch-1.0",
			 "uridecodebin",
			 0,
			 "!",
			 "videoconvert",
			 "!",
			 "videoscale",
			 "!",
			 "imagefreeze",
			 "!",
			 "identity",
			 "error-after=3",
			 "!",
			 "v4l2sink",
			 "show-preroll-frame=false",
			 0,
			 0 };
	snprintf(imagearg, 4096, "uri=file://%s",
		 realpath(imagefile, imagefile2));
	snprintf(devicearg, 4096, "device=%s", devicename);
	imagearg[4095] = devicearg[4095] = 0;
	args[2] = imagearg;
	args[15] = devicearg;

	fd = open_videodevice(devicename, O_RDWR);
	if (fd >= 0) {
		set_control_i(fd, "timeout_image_io", 1);
		close(fd);
	}

	dprintf(2,
		"v======================================================================v\n");
	if (my_execv(args)) {
		/*
          dprintf(2, "ERROR: setting time-out image failed\n");
          return 1;
          */
	}
	dprintf(2,
		"^======================================================================^\n");

	fd = open_videodevice(devicename, O_RDWR);
	if (fd >= 0) {
		/* finally check the timeout */
		if (timeout < 0) {
			timeout = get_control_i(fd, "timeout");
		} else {
			timeout = set_control_i(fd, "timeout", timeout);
		}
		if (timeout <= 0) {
			dprintf(2,
				"Timeout is currently disabled; you can set it to some positive value, e.g.:\n");
			dprintf(2, "    $  v4l2-ctl -d %s -c timeout=3000\n",
				devicename);
		}

		close(fd);
	}
}

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

typedef int (*t_argcheck)(const char *);
static int called_deprecated(const char *device, const char *argument,
			     const char *programname, const char *cmdname,
			     const char *argname, t_argcheck argcheck)
{
	/* check if <device> does not look like a device, but <argument> does
   * if so, assume that the user swapped the two */
	/* if the <device> looks about right, optionally do some extra
   * <argument>-check, to see if it can be used
   */

	int deviceswapped = 0;
	int argswapped = 0;

	if (argcheck)
		argswapped =
			((argcheck(argument) != 0) && (argcheck(device) == 0));

	if (!argswapped)
		deviceswapped = (parse_device(device) < 0 &&
				 parse_device(argument) >= 0);

	if (argswapped || deviceswapped) {
		dprintf(2, "WARNING: '%s %s <%s> <image>' is deprecated!\n",
			programname, cmdname, argname);
		dprintf(2, "WARNING: use '%s %s <device> <%s>' instead.\n",
			programname, cmdname, argname);
		return 1;
	}
	return 0;
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

	int ret = 0;

	int c;

	if (argc < 2)
		usage(argv[0]);
	cmd = get_command(argv[1]);
	switch (cmd) {
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
			default:
				usage_topic(argv[0], cmd);
				return 1;
			}
		fd = open_controldevice();
		do {
			struct v4l2_loopback_config cfg;
			int capture_nr = -1, output_nr = -1;
			if ((optind + 1) == argc) {
				/* no device given: pick some */
			} else if ((optind + 2) == argc) {
				/* single device given: use it for both input and output */
				capture_nr = output_nr =
					parse_device(argv[optind + 1]);
			} else if ((optind + 3) == argc) {
				/* two devices given: capture_device and output_device */
				capture_nr = parse_device(argv[optind + 1]);
				output_nr = parse_device(argv[optind + 2]);
			} else {
				usage_topic(argv[0], cmd);
			}
			ret = add_device(fd,
					 make_conf(&cfg, label, max_width,
						   max_height, exclusive_caps,
						   buffers, openers, capture_nr,
						   output_nr),
					 verbose);
		} while (0);
		break;
	case DELETE:
		if (argc == 2)
			usage_topic(argv[0], cmd);
		fd = open_controldevice();
		for (i = 2; i < argc; i++) {
			ret += (delete_device(fd, argv[i]) != 0);
		}
		ret = (ret > 0);
		break;
	case QUERY:
		if (argc == 2)
			usage_topic(argv[0], cmd);
		fd = open_controldevice();
		for (i = 2; i < argc; i++) {
			ret += query_device(fd, argv[i]);
		}
		ret = (ret > 0);
		break;
	case SET_FPS:
		if (argc != 4)
			usage_topic(argv[0], cmd);
		if (called_deprecated(argv[2], argv[3], argv[0], "set-fps",
				      "fps", is_fps)) {
			ret = set_fps(argv[3], argv[2]);
		} else
			ret = set_fps(argv[2], argv[3]);
		break;
	case GET_FPS:
		if (argc != 3)
			usage_topic(argv[0], cmd);
		ret = get_fps(argv[2]);
		break;
	case SET_CAPS:
		if (argc != 4)
			usage_topic(argv[0], cmd);
		if (called_deprecated(argv[2], argv[3], argv[0], "set-caps",
				      "caps", 0)) {
			ret = set_caps(argv[3], argv[2]);
		} else {
			ret = set_caps(argv[2], argv[3]);
		}
		break;
	case GET_CAPS:
		if (argc != 3)
			usage_topic(argv[0], cmd);
		ret = get_caps(argv[2]);
		break;
	case SET_TIMEOUTIMAGE:
		if ((4 == argc) && (strncmp("-t", argv[2], 4)) &&
		    (called_deprecated(argv[2], argv[3], argv[0],
				       "set-timeout-image", "image", 0))) {
			ret = set_timeoutimage(argv[3], argv[2], -1);
		} else {
			int timeout = -1;
			while ((c = getopt(argc - 1, argv + 1, "t:")) != -1)
				switch (c) {
				case 't':
					timeout = my_atoi("timeout", optarg);
					break;
				default:
					usage_topic(argv[0], cmd);
				}
			if (optind + 3 != argc)
				usage_topic(argv[0], cmd);
			ret = set_timeoutimage(argv[1 + optind],
					       argv[2 + optind], timeout);
		}
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

	return ret;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include "v4l2loopback.h"

#define CONTROLDEVICE "/dev/v4l2loopback"

#if 0
#define MARK() dprintf(2, "%s:%d @ %s\n", __FILE__, __LINE__, __func__)
#else
#define MARK()
#endif

void usage(const char *name)
{
	dprintf(2,
		"usage: %s add {<args>} [<device>]"
		"\n       %s delete <device>"
		"\n       %s query <device>"
		"\n\n"
		"\n adding devices"
		"\n =============="
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
		"\n\n"
		"\n deleting devices"
		"\n ================"
		"\n <device>\tcan be given one more more times (to delete multiple devices at once)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n"
		"\n querying devices"
		"\n ================"
		"\n <device>\tcan be given one more more times (to query multiple devices at once)."
		"\n         \teither specify a device name (e.g. '/dev/video1') or a device number ('1')."
		"\n\n",
		name, name, name);
	exit(1);
}

int my_atoi(const char *name, const char *s)
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

void print_conf(struct v4l2_loopback_config *cfg)
{
	MARK();
	if (!cfg) {
		printf("configuration: %s\n", cfg);
		return;
	}
	MARK();
	printf("\tdevice#          : %d"
	       "\n\tcard_label       : %s"
	       "\n\tmax_width        : %d"
	       "\n\tmax_height       : %d"
	       "\n\tannounce_all_caps: %d"
	       "\n\tmax_buffers      : %d"
	       "\n\tmax_openers      : %d"
	       "\n\tdebug            : %d"
	       "\n",
	       cfg->nr, cfg->card_label, cfg->max_width, cfg->max_height,
	       cfg->announce_all_caps, cfg->max_buffers, cfg->max_openers,
	       cfg->debug);
	MARK();
}

struct v4l2_loopback_config *make_conf(struct v4l2_loopback_config *cfg,
				       const char *label, int max_width,
				       int max_height, int exclusive_caps,
				       int buffers, int openers, int device)
{
	if (!cfg)
		return 0;
	if (!label && max_width <= 0 && max_height <= 0 && exclusive_caps < 0 &&
	    buffers <= 0 && openers <= 0 && device < 0)
		return 0;
	cfg->nr = device;
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
		config.nr = ret;
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
	config.nr = dev;
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

typedef enum { add, delete, query, unknown } t_command;
static t_command get_command(const char *command)
{
	if (!strncmp(command, "add", 4))
		return add;
	if (!strncmp(command, "del", 3))
		return delete;
	if (!strncmp(command, "query", 5))
		return query;
	return unknown;
}

int main(int argc, char **argv)
{
	int i;
	int fd = 0;
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
	cmd = get_command(argv[1]);
	if (unknown == cmd) {
		dprintf(2, "unknown command '%s'\n\n", argv[1]);
		usage(argv[0]);
	}
	fd = open(CONTROLDEVICE, 0);
	if (fd < 0) {
		perror("unable to open control device '" CONTROLDEVICE "'");
		return 1;
	}
	switch (cmd) {
	case add:
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
	case delete:
		if (argc == 2)
			usage(argv[0]);
		for (i = 2; i < argc; i++) {
			delete_device(fd, argv[i]);
		}
		break;
	case query:
		if (argc == 2)
			usage(argv[0]);
		for (i = 2; i < argc; i++) {
			query_device(fd, argv[i]);
		}
		break;
	default:
		dprintf(2, "unknown command '%s'\n", argv[1]);
		break;
	}

	close(fd);
	return 0;
}

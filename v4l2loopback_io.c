#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define DEV "/dev/"

/* arguments */
static int mode = '\0';
static int buf = 0;
static const char *device = "/dev/video0";

/* state */
static int device_fd = -1;
static void *buffer = NULL;
static unsigned long buffer_size = 0;

static void
die(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);

    exit(EXIT_FAILURE);
}

static int
open_buffer()
{
    const char *device_name;
    int open_mode;
    int mmap_mode;

    if (memcmp(device, DEV, sizeof(DEV) - 1) != 0)
        goto cant_parse_device;
    device_name = device + sizeof(DEV) - 1;
    if (strchr(device_name, '/') != NULL)
        goto cant_parse_device;

    open_mode = mode == 'r' ? O_RDONLY : O_WRONLY;
    if ((device_fd = open(device, open_mode)) < 0)
        die("can't open device\n");
    return 0;

cant_parse_device:
    die("can't parse device name\n");
}

static void
cleanup()
{
    if (buffer != NULL)
        munmap(buffer, buffer_size);
    if (device_fd >= 0)
        close(device_fd);
}

static int
do_read()
{
    return 0;
}

static int
do_write()
{
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc < 3)
        goto usage;

    if (sscanf(argv[1], "-%c", &mode) != 1)
        goto usage;
    if (sscanf(argv[2], "%i", &buf) != 1)
        goto usage;
    if (argc >= 4)
        device = argv[3];

    open_buffer();
    switch (mode) {
    case 'r':
        do_read();
        break;
    case 'w':
        do_write();
        break;
    default:
        goto usage;
    }

    cleanup();
    return 0;

usage:
    die("usage: %s (-r|-w) buffer_number [device]\n");
}

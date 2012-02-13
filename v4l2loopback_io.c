#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

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

static void
die_perror(const char *desc)
{
    perror(desc);
    exit(EXIT_FAILURE);
}

static long
read_long_attr(const char *filename_fmt, ...)
{
    va_list va;
    char filename[FILENAME_MAX];
    FILE *file;
    long value;

    va_start(va, filename_fmt);
    vsprintf(filename, filename_fmt, va);
    va_end(va);

    if ((file = fopen(filename, "r")) == NULL)
        die("can't open file: %s\n", filename);
    if (fscanf(file, "%ld", &value) != 1)
        die("can't read value from %s\n", filename);
    fclose(file);
    return value;
}

static int
open_buffer()
{
    const char *device_name;
    int max_buffers;
    int open_mode;
    int mmap_prot;
    int mmap_flags;

    if (memcmp(device, DEV, sizeof(DEV) - 1) != 0)
        goto cant_parse_device;
    device_name = device + sizeof(DEV) - 1;
    if (strchr(device_name, '/') != NULL)
        goto cant_parse_device;

    /* using O_WRONLY doesn't allow PROT_WRITE mmapping for me */
    open_mode = mode == 'r' ? O_RDONLY : O_RDWR;

    if ((device_fd = open(device, open_mode)) < 0)
        die_perror("open() failed");
    max_buffers = (int)read_long_attr("/sys/devices/virtual/video4linux/%s/max_buffers", device_name);
    buffer_size = (unsigned long)read_long_attr("/sys/devices/virtual/video4linux/%s/buffer_size", device_name);

    if (buf == -1)
        buf = max_buffers;

    if (buf == max_buffers)
        fprintf(stderr, "mmapping placeholder frame...\n");
    else if (buf < 0 || buf > max_buffers)
        die("buffer index out of range\n");
    else
        fprintf(stderr, "mmapping frame %d...\n", buf);

    mmap_prot = mode == 'r' ? PROT_READ : PROT_WRITE;
    mmap_flags = MAP_SHARED;
    buffer = mmap(NULL, buffer_size, mmap_prot, mmap_flags, device_fd, buf * buffer_size);
    if (buffer == MAP_FAILED)
        die_perror("mmap() failed");
    fprintf(stderr, "mmapped %ld bytes\n", buffer_size);
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
    die("reading unimplemented\n");
    return 0;
}

static int
do_write()
{
    if (freopen(NULL, "rb", stdin) == NULL)
        die("can't reopen stdin for binary input\n");

    ssize_t written = fread(buffer, 1, buffer_size, stdin);
    if (written < 0)
        die("fread() failed\n");

    fprintf(stderr, "written %ld bytes\n", written);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc < 3)
        goto usage;

    if (sscanf(argv[1], "-%c", &mode) != 1)
        goto usage;
    if (sscanf(argv[2], "%i", &buf) != 1) {
        if (!strcmp(argv[2], "placeholder"))
            buf = -1;
        else
            goto usage;
    }
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
    die("usage: %s (-r|-w) (buffer_number|'placeholder') [device]\n");
}

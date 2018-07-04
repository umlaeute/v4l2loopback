#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>                /* low-level i/o */
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <semaphore.h>

static char *v4l2dev = "/dev/video1";
static int v4l2sink = -1;
static int width = 80;                //640;    // Default for Flash
static int height = 60;        //480;    // Default for Flash
static char *vidsendbuf = NULL;
static int vidsendsiz = 0;

static void init_device() {

}

static void grab_frame() {

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    memset( vidsendbuf, 0, 3);
    switch( ts.tv_sec & 3 ) {
    case 0:
        vidsendbuf[0] = 255;
        break;
    case 1:
        vidsendbuf[0] = 255;
        vidsendbuf[1] = 255;
        break;
    case 2:
        vidsendbuf[1] = 255;
        break;
    case 3:
        vidsendbuf[2] = 255;
        break;
    }
    memcpy( vidsendbuf+3, vidsendbuf, vidsendsiz-3 );
}

static void stop_device() {

}

static void open_vpipe()
{
    v4l2sink = open(v4l2dev, O_WRONLY);
    if (v4l2sink < 0) {
        fprintf(stderr, "Failed to open v4l2sink device. (%s)\n", strerror(errno));
        exit(-2);
    }
    // setup video for proper format
    struct v4l2_format v;
    int t;
    v.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    t = ioctl(v4l2sink, VIDIOC_G_FMT, &v);
    if( t < 0 )
        exit(t);
    v.fmt.pix.width = width;
    v.fmt.pix.height = height;
    v.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    vidsendsiz = width * height * 3;
    v.fmt.pix.sizeimage = vidsendsiz;
    t = ioctl(v4l2sink, VIDIOC_S_FMT, &v);
    if( t < 0 )
        exit(t);
    vidsendbuf = malloc( vidsendsiz );
}

static pthread_t sender;
static sem_t lock1,lock2;
static void *sendvid(void *v)
{
    for (;;) {
        sem_wait(&lock1);
        if (vidsendsiz != write(v4l2sink, vidsendbuf, vidsendsiz))
            exit(-1);
        sem_post(&lock2);
    }
}

int main(int argc, char **argv)
{
    struct timespec ts;

    if( argc == 2 )
        v4l2dev = argv[1];

    open_vpipe();

    // open and lock response
    if (sem_init(&lock2, 0, 1) == -1)
        exit(-1);
    sem_wait(&lock2);

    if (sem_init(&lock1, 0, 1) == -1)
        exit(-1);
    pthread_create(&sender, NULL, sendvid, NULL);

    for (;;) {
        // wait until a frame can be written
        fprintf( stderr, "Waiting for sink\n" );
        sem_wait(&lock2);
        // setup source
        init_device(); // open and setup SPI
        for (;;) {
            grab_frame();
            // push it out
            sem_post(&lock1);
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2;
            // wait for it to get written (or is blocking)
            if (sem_timedwait(&lock2, &ts))
                break;
        }
        stop_device(); // close SPI
    }
    close(v4l2sink);
    return 0;
}

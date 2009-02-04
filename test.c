#include <linux/videodev2.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>


int main()
{
    struct v4l2_capability vid_caps;
    struct v4l2_format vid_format;
    __u8 buffer[640*480*3];
          
    int fd = open("/dev/video1",O_RDWR);
    assert(fd>=0); 
    int ret_code = ioctl(fd, VIDIOC_QUERYCAP, vid_caps);
    assert(ret_code != -1); 
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vid_format.fmt.pix.width = 640;
    vid_format.fmt.pix.height = 480;
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    ret_code = ioctl(fd, VIDIOC_S_FMT, vid_format);
    assert(ret_code != -1);
    write(fd, buffer, 640*480*3);                                        
}
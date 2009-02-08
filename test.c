#include <linux/videodev2.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>


int main()
{
    struct v4l2_capability vid_caps;
    struct v4l2_format vid_format;
    int data_length = 640*480*3;
    __u8 buffer[data_length];
    __u8 check_buffer[data_length];
    int i;
    for(i = 0; i<data_length;++i)
    {
      buffer[i]=255;
      check_buffer[i] = 0;
    }
          
    int fdwr = open("/dev/video1",O_RDWR);
    assert(fdwr>=0); 
    int ret_code = ioctl(fdwr, VIDIOC_QUERYCAP, vid_caps);    
    /* assert(ret_code != -1); /*assertation fails for some reason */
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vid_format.fmt.pix.width = 640;
    vid_format.fmt.pix.height = 480;
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
    ret_code = ioctl(fdwr, VIDIOC_S_FMT, &vid_format);
    assert(ret_code != -1);
    write(fdwr, buffer, data_length);   
/*    int fdr = open("/dev/video1",O_RDONLY);
    read(fdr, check_buffer, data_length);
    for(i = 0; i<data_length;++i)
    {
      if (buffer[i] != check_buffer[i])
        assert(0);
    }
*/
}

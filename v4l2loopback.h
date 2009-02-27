/* 
 * File:   v4l2loopback.h
 * Author: vasaka
 *
 */

#ifndef _V4L2LOOPBACK_H
#define	_V4L2LOOPBACK_H

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
 /* fixed inner buffers number */
#define V4L2_LOOPBACK_BUFFERS_NUMBER 4

/* TODO(vasaka) use typenames which are common to kernel, but first find out if
 * it is needed */
/* struct keeping state and settings of loopback device */
struct v4l2_loopback_device {
  struct video_device *vdev;
  /* pixel and stream format */
  struct v4l2_pix_format pix_format;
  struct v4l2_captureparm	capture_param;
  /* buffers stuff */
  __u8 *image; /* pointer to actual buffers data */
  int buffers_number; /* should not be big 4 is a good choise */
  struct v4l2_buffer *buffers; /* inner driver buffers */
  int queued_number; /* number of buffers in incoming queue */
  int done_number;   /* number of buffers in outgoing queue 
                      * queued_number + done_number <= buffers_number 
                      * should be changed by helper functions only */                      
  long buffer_size;
  int num_collect; /* how many buffers to capture before give it to app*/
  /* sync stuff */
  int open_count;
  int redy_for_capture; /* set to true when at least one writer opened device 
                         * and negotiated format */
  wait_queue_head_t read_event;
};
/* types of opener shows what opener wants to do with loopback */
enum opener_type {
  UNNEGOTIATED = 0,
  READER = 1,
  WRITER = 2,
};
/* struct keeping state and type of opener */
struct opener {
  enum opener_type type;
  int buffers_number;
  struct v4l2_buffer *buffer;
};
extern const struct v4l2_file_operations v4l2_loopback_fops;
#endif	/* _V4L2LOOPBACK_H */


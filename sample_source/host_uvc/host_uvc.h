/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef HOST_UVC_H
#define HOST_UVC_H

#include <linux/videodev2.h>
#include "sample_comm.h"

#ifndef V4L2_PIX_FMT_H265
#define V4L2_PIX_FMT_H265     v4l2_fourcc('H', '2', '6', '5') /* H.265 aka HEVC */
#endif

enum buffer_fill_mode {
    BUFFER_FILL_NONE = 0,
    BUFFER_FILL_FRAME = 1 << 0,
    BUFFER_FILL_PADDING = 1 << 1,
};

typedef struct {
    hi_u32 idx;
    hi_u32 padding[VIDEO_MAX_PLANES];
    hi_u32 size[VIDEO_MAX_PLANES];
    hi_void *mem[VIDEO_MAX_PLANES];
} buffer_info;

typedef struct {
    hi_s32 fd;
    hi_bool opened;
    enum v4l2_buf_type type;
    enum v4l2_memory memtype;
    hi_u32 nbufs;
    buffer_info *buffers;
    hi_u32 width;
    hi_u32 height;
    uint32_t buffer_output_flags;
    uint32_t timestamp_type;
    hi_u32 num_planes;
    struct v4l2_plane_pix_format plane_fmt[VIDEO_MAX_PLANES];
    hi_void *pattern[VIDEO_MAX_PLANES];
    hi_u32 patternsize[VIDEO_MAX_PLANES];
    hi_bool write_data_prefix;
} device_info;

typedef struct {
    enum v4l2_buf_type type;
    hi_bool supported;
    const hi_char *name;
    const hi_char *string;
} buf_type;

typedef struct {
    const hi_char *name;
    hi_u32 fourcc;
    hi_char n_planes;
} format_info;

typedef struct {
    const hi_char *name;
    enum v4l2_field field;
} field_info;

typedef struct {
    hi_u32 nframes;
    hi_u32 skip;
    hi_u32 delay;
    hi_u32 pause;
    hi_bool do_requeue_last;
    hi_bool do_queue_late;
    enum buffer_fill_mode fill;
    const hi_char *pattern;
    const hi_char *type_name;

    hi_bool do_capture;
    hi_bool do_set_format;
    hi_u32 pixelformat;
    hi_bool do_file;
    hi_bool do_set_input;
    hi_s32 input;
    hi_bool do_list_controls;
    hi_u32 nbufs;
    hi_u32 quality;
    hi_s32 ctrl_name;
    hi_bool do_get_control;
    hi_bool do_rt;
    hi_s32 rt_priority;
    hi_u32 width;
    hi_u32 height;
    hi_u32 stride;
    hi_u32 buffer_size;
    hi_bool do_set_time_per_frame;
    struct v4l2_fract time_per_frame;
    enum v4l2_memory memtype;
    const hi_char *ctrl_value;
    hi_bool do_set_control;
    hi_s32 extension_name;
    const hi_char *extension_channel;
    hi_bool do_send_extension;
    hi_bool do_enum_formats;
    hi_bool do_enum_inputs;
    enum v4l2_field field;
    hi_bool do_log_status;
    hi_bool no_query;
    hi_u32 fmt_flags;
    hi_bool do_reset_control;
    hi_bool do_sleep_forever;
    hi_u32 userptr_offset;
    hi_bool do_reset_controls;
} uvc_ctrl_info;

#endif

/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include "host_uvc.h"
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/uvcvideo.h>
#include <linux/usb/video.h>
#include "media_vdec.h"


#define array_size(a)    (sizeof(a) / sizeof((a)[0]))

#define SAMPLE_UVC_V4L_BUFFERS_DEFAULT  1
#define SAMPLE_UVC_V4L_BUFFERS_MAX      4

#define SAMPLE_UVC_OPT_ENUM_FORMATS   256
#define SAMPLE_UVC_OPT_ENUM_INPUTS    257
#define SAMPLE_UVC_OPT_SKIP_FRAMES    258
#define SAMPLE_UVC_OPT_NO_QUERY       259
#define SAMPLE_UVC_OPT_SLEEP_FOREVER  260
#define SAMPLE_UVC_OPT_USERPTR_OFFSET 261
#define SAMPLE_UVC_OPT_REQUEUE_LAST   262
#define SAMPLE_UVC_OPT_STRIDE         263
#define SAMPLE_UVC_OPT_FD             264
#define SAMPLE_UVC_OPT_TSTAMP_SRC     265
#define SAMPLE_UVC_OPT_FIELD          266
#define SAMPLE_UVC_OPT_LOG_STATUS     267
#define SAMPLE_UVC_OPT_BUFFER_SIZE    268
#define SAMPLE_UVC_OPT_PREMULTIPLIED  269
#define SAMPLE_UVC_OPT_QUEUE_LATE     270
#define SAMPLE_UVC_OPT_DATA_PREFIX    271
#define SAMPLE_UVC_OPT_RESET_CONTROLS 272

#define SAMPLE_UVC_MAX_FILE_NAME_LEN  23

#define DEVICE_UVC_SIZE_NUM           20

hi_size g_device_size[DEVICE_UVC_SIZE_NUM] = {0};
hi_u32 g_device_size_num = 0;

hi_bool g_uvc_exit = HI_FALSE;

buf_type g_buf_types[] = {
    { V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 1, "Video capture mplanes", "capture-mplane", },
    { V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 1, "Video output", "output-mplane", },
    { V4L2_BUF_TYPE_VIDEO_CAPTURE, 1, "Video capture", "capture", },
    { V4L2_BUF_TYPE_VIDEO_OUTPUT, 1, "Video output mplanes", "output", },
    { V4L2_BUF_TYPE_VIDEO_OVERLAY, 0, "Video overlay", "overlay" },
    { V4L2_BUF_TYPE_META_CAPTURE, 1, "Meta-data capture", "meta-capture", },
};

format_info g_pixel_formats[] = {
    { "YUYV", V4L2_PIX_FMT_YUYV, 1 },
    { "MJPEG", V4L2_PIX_FMT_MJPEG, 1 },
    { "H264", V4L2_PIX_FMT_H264, 1 },
    { "H265", V4L2_PIX_FMT_H265, 1 },
    { "NV12", V4L2_PIX_FMT_NV12, 1 },
    { "NV21", V4L2_PIX_FMT_NV21, 1 },
};

field_info g_fields[] = {
    { "filed_any", V4L2_FIELD_ANY },
    { "filed_none", V4L2_FIELD_NONE },
    { "filed_top", V4L2_FIELD_TOP },
    { "filed_bottom", V4L2_FIELD_BOTTOM },
    { "filed_interlaced", V4L2_FIELD_INTERLACED },
    { "filed_seq_tb", V4L2_FIELD_SEQ_TB },
    { "filed_seq_bt", V4L2_FIELD_SEQ_BT },
    { "filed_alternate", V4L2_FIELD_ALTERNATE },
    { "filed_interlaced_tb", V4L2_FIELD_INTERLACED_TB },
    { "filed_interlaced_bt", V4L2_FIELD_INTERLACED_BT },
};

static struct option g_opts[] = {
    {"enum-formats", 0, 0, SAMPLE_UVC_OPT_ENUM_FORMATS},
    {"file", 2, 0, 'F'},
    {"format", 1, 0, 'f'},
    {"help", 0, 0, 'h'},
    {"size", 1, 0, 's'},
    {0, 0, 0, 0}
};

static hi_bool g_pause_resume;
static struct termios g_pause_term;
static hi_bool g_pause_no_term;
static hi_bool g_pause_term_configured;
static hi_char g_pause_filename[SAMPLE_UVC_MAX_FILE_NAME_LEN];

static hi_void sample_uvc_pause_signal_handler(hi_s32 signal __attribute__((__unused__)))
{
    g_pause_resume = HI_TRUE;
}

static hi_void sample_uvc_exit_signal_handler(hi_s32 signal __attribute__((__unused__)))
{
    g_uvc_exit = HI_TRUE;
}

static hi_s32 open_by_real_path(const hi_char *file_path, int flags)
{
    if (file_path == NULL) {
        return -1;
    }
    hi_char *regular_path = realpath(file_path, NULL);
    if (regular_path != NULL) {
        int fd = open(regular_path, flags);
        free(regular_path);
        return fd;
    }
    sample_print("realpath(%s) fail errno %d\n", file_path, errno);
    return -1;
}

static hi_void sample_uvc_pause_wait(hi_void)
{
    hi_s32 ret;
    hi_s32 fd;

    fd = open(g_pause_filename, O_CREAT, 0);
    if (fd != -1) {
        close(fd);
    }

    if (g_pause_no_term) {
        sample_print("Paused waiting for SIGUSR1\n");
        while (!g_pause_resume) {
            pause();
        }
        goto done;
    }

    sample_print("Paused waiting for key press or SIGUSR1\n");
    g_pause_resume = HI_FALSE;

    while (!g_pause_resume) {
        fd_set read_fds;
        hi_char c;

        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);

        ret = select(1, &read_fds, HI_NULL, HI_NULL, HI_NULL);
        if (ret < 0 && errno != EINTR) {
            break;
        }
        if (ret == 1) {
            ret = read(0, &c, 1);
            break;
        }
    }

done:
    unlink(g_pause_filename);
}

static hi_void sample_uvc_pause_cleanup(hi_void)
{
    if (g_pause_term_configured) {
        tcsetattr(0, TCSANOW, &g_pause_term);
    }

    unlink(g_pause_filename);
}

static hi_s32 sample_uvc_pause_init(hi_void)
{
    struct sigaction sig_usr1;
    struct termios term;
    hi_s32 ret;

    (hi_void)sprintf_s(g_pause_filename, SAMPLE_UVC_MAX_FILE_NAME_LEN, ".host_uvc.wait.%d", getpid());

    (hi_void)memset_s(&sig_usr1, sizeof(sig_usr1), 0, sizeof(sig_usr1));
    sig_usr1.sa_handler = sample_uvc_pause_signal_handler;
    ret = sigaction(SIGUSR1, &sig_usr1, HI_NULL);
    if (ret < 0) {
        sample_print("Unable to install SIGUSR1 handler\n");
        return -errno;
    }

    ret = tcgetattr(0, &term);
    if (ret < 0) {
        if (errno == ENOTTY) {
            g_pause_no_term = HI_TRUE;
            return 0;
        }

        sample_print("Unable to retrieve terminal attributes\n");
        return -errno;
    }

    g_pause_term = term;
    g_pause_term_configured = HI_TRUE;

    term.c_lflag &= ~ICANON;
    term.c_lflag &= ~ECHO;
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;

    ret = tcsetattr(0, TCSANOW, &term);
    if (ret < 0) {
        sample_print("Unable to set terminal attributes\n");
        return -errno;
    }

    return 0;
}

static hi_bool sample_uvc_video_is_mplane(const device_info *dev)
{
    return (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE || dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
}

static hi_bool sample_uvc_video_is_meta(const device_info *dev)
{
    return (dev->type == V4L2_BUF_TYPE_META_CAPTURE);
}

static hi_bool sample_uvc_video_is_capture(const device_info *dev)
{
    return (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE || dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
        dev->type == V4L2_BUF_TYPE_META_CAPTURE);
}

static hi_bool sample_uvc_video_is_output(const device_info *dev)
{
    return (dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE || dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT);
}

static const hi_char *sample_uvc_v4l2_buf_type_name(enum v4l2_buf_type type)
{
    hi_u32 i;

    for (i = 0; i < array_size(g_buf_types); ++i) {
        if (g_buf_types[i].type == type) {
            return g_buf_types[i].name;
        }
    }

    if (type & V4L2_BUF_TYPE_PRIVATE) {
        return "Private";
    } else {
        return "Unknown";
    }
}

static hi_void sample_uvc_list_formats(hi_void)
{
    hi_u32 i;

    for (i = 0; i < array_size(g_pixel_formats); i++) {
        sample_print("%s (\"%u%u%u%u\", %d planes)\n", g_pixel_formats[i].name, g_pixel_formats[i].fourcc & 0xff,
            (g_pixel_formats[i].fourcc >> 8) & 0xff, (g_pixel_formats[i].fourcc >> 16) & 0xff,  /* 8,16:shift */
            (g_pixel_formats[i].fourcc >> 24) & 0xff, g_pixel_formats[i].n_planes);             /* 24:shift */
    }
}

static const format_info *sample_uvc_v4l2_format_by_fourcc(hi_u32 fourcc)
{
    hi_u32 i;

    for (i = 0; i < array_size(g_pixel_formats); ++i) {
        if (g_pixel_formats[i].fourcc == fourcc) {
            return &g_pixel_formats[i];
        }
    }

    return HI_NULL;
}

static const format_info *sample_uvc_v4l2_format_by_name(const hi_char *name)
{
    hi_u32 i;

    for (i = 0; i < array_size(g_pixel_formats); ++i) {
        if (strcasecmp(g_pixel_formats[i].name, name) == 0) {
            return &g_pixel_formats[i];
        }
    }

    return HI_NULL;
}

static const hi_char *sample_uvc_v4l2_format_name(hi_u32 fourcc)
{
    const format_info *format;
    static hi_char format_name[5];     /* 5: array len */
    hi_u32 i;

    format = sample_uvc_v4l2_format_by_fourcc(fourcc);
    if (format) {
        return format->name;
    }

    for (i = 0; i < 4; ++i) {       /* 4: format */
        format_name[i] = fourcc & 0xff;
        fourcc >>= 8;               /* 8: shift */
    }

    format_name[4] = '\0';     /* 4: end */
    return format_name;
}

static const hi_char *sample_uvc_v4l2_field_name(enum v4l2_field field)
{
    hi_u32 i;

    for (i = 0; i < array_size(g_fields); ++i) {
        if (g_fields[i].field == field) {
            return g_fields[i].name;
        }
    }

    return "unknown";
}

static hi_void sample_uvc_video_set_buf_type(device_info *dev, enum v4l2_buf_type type)
{
    dev->type = type;
}

static hi_bool sample_uvc_video_has_valid_buf_type(const device_info *dev)
{
    return (hi_s32)dev->type != -1;
}

static hi_void sample_uvc_video_init(device_info *dev)
{
    dev->fd = -1;
    dev->memtype = V4L2_MEMORY_MMAP;
    dev->buffers = HI_NULL;
    dev->type = (enum v4l2_buf_type)-1;
}

static hi_bool sample_uvc_video_has_fd(const device_info *dev)
{
    return dev->fd != -1;
}

static hi_s32 sample_uvc_video_open(device_info *dev, const hi_char *devname)
{
    if (sample_uvc_video_has_fd(dev)) {
        sample_print("Can't open device (already open).\n");
        return -1;
    }
    hi_char *regular_path = realpath(devname, NULL);
    if (regular_path == NULL) {
        sample_print("Can't get regular path of %s\n", devname);
        return -1;
    }
    dev->fd = open(regular_path, O_RDWR);
    free(regular_path);
    if (dev->fd < 0) {
        sample_print("Error opening device %s\n", devname);
        return dev->fd;
    }

    printf("Device %s opened.\n", devname);

    dev->opened = HI_TRUE;

    return 0;
}

static hi_s32 sample_uvc_video_query_cap(device_info *dev, hi_u32 *capabilities)
{
    struct v4l2_capability cap;
    hi_u32 caps;
    hi_bool is_support_video;
    hi_bool is_support_meta;
    hi_bool is_support_capture;
    hi_bool is_support_output;
    hi_bool is_support_mplane;
    hi_s32 ret;

    (hi_void)memset_s(&cap, sizeof(cap), 0, sizeof(cap));
    ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        return 0;
    }

    caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;

    is_support_video = caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_CAPTURE |
        V4L2_CAP_VIDEO_OUTPUT_MPLANE | V4L2_CAP_VIDEO_OUTPUT);
    is_support_meta = caps & (V4L2_CAP_META_CAPTURE);
    is_support_capture = caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_META_CAPTURE);
    is_support_output = caps & (V4L2_CAP_VIDEO_OUTPUT_MPLANE | V4L2_CAP_VIDEO_OUTPUT);
    is_support_mplane = caps & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE);

    printf("Device `%s' on `%s' (driver '%s') supports%s%s%s%s %s mplanes.\n", cap.card, cap.bus_info, cap.driver,
        is_support_video ? " video," : "", is_support_meta ? " meta-data," : "",
        is_support_capture ? " capture," : "", is_support_output ? " output," : "",
        is_support_mplane ? "with" : "without");

    *capabilities = caps;

    return 0;
}

static hi_s32 sample_uvc_cap_get_buf_type(hi_u32 capa)
{
    if (capa & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        return V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    }

    if (capa & V4L2_CAP_VIDEO_OUTPUT_MPLANE) {
        return V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    }

    if (capa & V4L2_CAP_VIDEO_CAPTURE) {
        return  V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    if (capa & V4L2_CAP_VIDEO_OUTPUT) {
        return V4L2_BUF_TYPE_VIDEO_OUTPUT;
    }

    if (capa & V4L2_CAP_META_CAPTURE) {
        return V4L2_BUF_TYPE_META_CAPTURE;
    }

    sample_print("Device supports neither capture nor output.\n");
    return -EINVAL;
}

static hi_void sample_uvc_video_close(device_info *dev)
{
    hi_u32 i;

    for (i = 0; i < dev->num_planes; i++) {
        if (dev->pattern[i] != HI_NULL) {
            free(dev->pattern[i]);
            dev->pattern[i] = HI_NULL;
        }
    }

    if (dev->buffers != HI_NULL) {
        free(dev->buffers);
        dev->buffers = HI_NULL;
    }

    if (dev->opened) {
        close(dev->fd);
    }
}

static hi_void sample_uvc_video_log_status(device_info *dev)
{
    ioctl(dev->fd, VIDIOC_LOG_STATUS);
}

static hi_s32 sample_uvc_query_control(device_info *dev, hi_u32 id, struct v4l2_query_ext_ctrl *query_ext_ctrl)
{
    hi_s32 ret;
    struct v4l2_queryctrl query_ctrl = {0};

    query_ctrl.id = id;

    ret = ioctl(dev->fd, VIDIOC_QUERYCTRL, &query_ctrl);
    if (ret < 0) {
        ret = -errno;
        sample_print("unable to query control 0x%8.8x\n", id);
        return ret;
    }

    (hi_void)memset_s(query_ext_ctrl, sizeof(*query_ext_ctrl), 0, sizeof(*query_ext_ctrl));
    query_ext_ctrl->id = query_ctrl.id;
    query_ext_ctrl->type = query_ctrl.type;
    (hi_void)memcpy_s(query_ext_ctrl->name, sizeof(query_ext_ctrl->name),
        query_ctrl.name, sizeof(query_ext_ctrl->name));
    query_ext_ctrl->minimum = query_ctrl.minimum;
    query_ext_ctrl->maximum = query_ctrl.maximum;
    query_ext_ctrl->step = (hi_u32)query_ctrl.step;
    query_ext_ctrl->default_value = query_ctrl.default_value;
    query_ext_ctrl->flags = query_ctrl.flags;

    if (query_ctrl.type == V4L2_CTRL_TYPE_STRING && !(query_ctrl.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD)) {
        query_ext_ctrl->elem_size = (hi_u32)(query_ctrl.maximum + 1);
        query_ext_ctrl->elems = 1;
    }

    return 0;
}

static hi_s32 sample_uvc_query_ext_control(device_info *dev, hi_u32 id, struct v4l2_query_ext_ctrl *query_ext_ctrl)
{
    struct v4l2_queryctrl query_ctrl = {0};
    hi_s32 ret;

    (hi_void)memset_s(query_ext_ctrl, sizeof(*query_ext_ctrl), 0, sizeof(*query_ext_ctrl));
    query_ext_ctrl->id = id;

    ret = ioctl(dev->fd, VIDIOC_QUERY_EXT_CTRL, query_ext_ctrl);
    if (ret < 0) {
        ret = -errno;
    }
    if (!ret || ret == -EINVAL) {
        return ret;
    }

    if (ret != -ENOTTY) {
        sample_print("unable to query control 0x%8.8x\n", id);
        return ret;
    }

    query_ctrl.id = id;

    ret = ioctl(dev->fd, VIDIOC_QUERYCTRL, &query_ctrl);
    if (ret < 0) {
        ret = -errno;
        sample_print("unable to query control 0x%8.8x\n", id);
        return ret;
    }

    (hi_void)memset_s(query_ext_ctrl, sizeof(*query_ext_ctrl), 0, sizeof(*query_ext_ctrl));
    query_ext_ctrl->id = query_ctrl.id;
    query_ext_ctrl->type = query_ctrl.type;
    (hi_void)memcpy_s(query_ext_ctrl->name, sizeof(query_ext_ctrl->name),
        query_ctrl.name, sizeof(query_ext_ctrl->name));
    query_ext_ctrl->minimum = query_ctrl.minimum;
    query_ext_ctrl->maximum = query_ctrl.maximum;
    query_ext_ctrl->step = (hi_u32)query_ctrl.step;
    query_ext_ctrl->default_value = query_ctrl.default_value;
    query_ext_ctrl->flags = query_ctrl.flags;
    if (query_ctrl.type == V4L2_CTRL_TYPE_STRING && !(query_ctrl.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD)) {
        query_ext_ctrl->elem_size = (hi_u32)(query_ctrl.maximum + 1);
        query_ext_ctrl->elems = 1;
    }
    return 0;
}

static hi_s32 sample_uvc_get_control(device_info *dev, const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    struct v4l2_ext_control *ctrl, hi_u32 which)
{
    struct v4l2_ext_controls controls = {0};
    struct v4l2_control control;
    hi_s32 ret;

    (hi_void)memset_s(ctrl, sizeof(*ctrl), 0, sizeof(*ctrl));

    controls.which = which;
    controls.count = 1;
    controls.controls = ctrl;

    ctrl->id = query_ext_ctrl->id;

    if (query_ext_ctrl->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) {
        ctrl->size = query_ext_ctrl->elems * query_ext_ctrl->elem_size;
        ctrl->ptr = malloc(ctrl->size);
        if (ctrl->ptr == HI_NULL) {
            return -ENOMEM;
        }
    }

    ret = ioctl(dev->fd, VIDIOC_G_EXT_CTRLS, &controls);
    if (ret != -1) {
        return 0;
    }

    if (query_ext_ctrl->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) {
        free(ctrl->ptr);
        ctrl->ptr = HI_NULL;
    }

    if ((query_ext_ctrl->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) || (query_ext_ctrl->type == V4L2_CTRL_TYPE_INTEGER64) ||
        ((errno != EINVAL) && (errno != ENOTTY))) {
        return -errno;
    }

    control.id = query_ext_ctrl->id;
    ret = ioctl(dev->fd, VIDIOC_G_CTRL, &control);
    if (ret < 0) {
        return -errno;
    }

    ctrl->value = control.value;
    return 0;
}

static hi_s32 sample_uvc_set_control(device_info *dev, const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    struct v4l2_ext_control *ctrl)
{
    struct v4l2_ext_controls controls = {0};
    struct v4l2_control control;
    hi_s32 ret;

    controls.ctrl_class = V4L2_CTRL_ID2CLASS(ctrl->id);
    controls.count = 1;
    controls.controls = ctrl;

    ctrl->id = query_ext_ctrl->id;

    ret = ioctl(dev->fd, VIDIOC_S_EXT_CTRLS, &controls);
    if (ret != -1) {
        return 0;
    }

    if ((query_ext_ctrl->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) || (query_ext_ctrl->type == V4L2_CTRL_TYPE_INTEGER64) ||
        ((errno != EINVAL) && (errno != ENOTTY))) {
        return -1;
    }

    control.id = ctrl->id;
    control.value = ctrl->value;
    ret = ioctl(dev->fd, VIDIOC_S_CTRL, &control);
    if (ret != -1) {
        ctrl->value = control.value;
    }

    return ret;
}

static hi_s32 sample_uvc_video_get_format(device_info *dev)
{
    struct v4l2_format format = {0};
    hi_u32 i;
    hi_s32 ret;

    format.type = dev->type;

    ret = ioctl(dev->fd, VIDIOC_G_FMT, &format);
    if (ret < 0) {
        sample_print("get format failed!\n");
        return ret;
    }

    if (sample_uvc_video_is_mplane(dev)) {
        dev->width = format.fmt.pix_mp.width;
        dev->height = format.fmt.pix_mp.height;
        dev->num_planes = format.fmt.pix_mp.num_planes;

        printf("video format: %s (%08x) %ux%u field %s, %u planes: \n",
            sample_uvc_v4l2_format_name(format.fmt.pix_mp.pixelformat), format.fmt.pix_mp.pixelformat,
            format.fmt.pix_mp.width, format.fmt.pix_mp.height,
            sample_uvc_v4l2_field_name(format.fmt.pix_mp.field),
            format.fmt.pix_mp.num_planes);

        for (i = 0; i < format.fmt.pix_mp.num_planes; i++) {
            dev->plane_fmt[i].bytesperline = format.fmt.pix_mp.plane_fmt[i].bytesperline;
            dev->plane_fmt[i].sizeimage = format.fmt.pix_mp.plane_fmt[i].bytesperline ?
                format.fmt.pix_mp.plane_fmt[i].sizeimage : 0;

            printf(" * stride %u, buffer size %u\n", format.fmt.pix_mp.plane_fmt[i].bytesperline,
                format.fmt.pix_mp.plane_fmt[i].sizeimage);
        }
    } else if (sample_uvc_video_is_meta(dev)) {
        dev->width = 0;
        dev->height = 0;
        dev->num_planes = 1;

        printf("meta-data format: %s (%08x) buffer size %u\n",
            sample_uvc_v4l2_format_name(format.fmt.meta.dataformat), format.fmt.meta.dataformat,
            format.fmt.meta.buffersize);
    } else {
        dev->width = format.fmt.pix.width;
        dev->height = format.fmt.pix.height;
        dev->num_planes = 1;

        dev->plane_fmt[0].bytesperline = format.fmt.pix.bytesperline;
        dev->plane_fmt[0].sizeimage = format.fmt.pix.bytesperline ? format.fmt.pix.sizeimage : 0;

        printf("video format: %s (%08x) %ux%u (stride %u) field %s buffer size %u\n",
            sample_uvc_v4l2_format_name(format.fmt.pix.pixelformat), format.fmt.pix.pixelformat,
            format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.bytesperline,
            sample_uvc_v4l2_field_name(format.fmt.pix_mp.field),
            format.fmt.pix.sizeimage);
    }

    return 0;
}

static hi_void sample_uvc_print_format(device_info *dev, const struct v4l2_format *fmt)
{
    hi_u32 i;

    if (sample_uvc_video_is_mplane(dev)) {
        printf("video format set: %s (%08x) %ux%u field %s, %u planes: \n",
            sample_uvc_v4l2_format_name(fmt->fmt.pix_mp.pixelformat), fmt->fmt.pix_mp.pixelformat,
            fmt->fmt.pix_mp.width, fmt->fmt.pix_mp.height,
            sample_uvc_v4l2_field_name(fmt->fmt.pix_mp.field),
            fmt->fmt.pix_mp.num_planes);

        for (i = 0; i < fmt->fmt.pix_mp.num_planes; i++) {
            printf(" * stride %u, buffer size %u\n",
                fmt->fmt.pix_mp.plane_fmt[i].bytesperline,
                fmt->fmt.pix_mp.plane_fmt[i].sizeimage);
        }
    } else if (sample_uvc_video_is_meta(dev)) {
        printf("meta-data format: %s (%08x) buffer size %u\n",
            sample_uvc_v4l2_format_name(fmt->fmt.meta.dataformat), fmt->fmt.meta.dataformat,
            fmt->fmt.meta.buffersize);
    } else {
        printf("video format set: %s (%08x) %ux%u (stride %u) field %s buffer size %u\n",
            sample_uvc_v4l2_format_name(fmt->fmt.pix.pixelformat), fmt->fmt.pix.pixelformat,
            fmt->fmt.pix.width, fmt->fmt.pix.height, fmt->fmt.pix.bytesperline,
            sample_uvc_v4l2_field_name(fmt->fmt.pix.field),
            fmt->fmt.pix.sizeimage);
    }
}

static hi_s32 sample_uvc_get_device_size(device_info *dev, hi_u32 pixelformat, enum v4l2_buf_type type)
{
    struct v4l2_frmsizeenum frame;
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; ; ++i) {
        (hi_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));

        frame.index = i;
        frame.pixel_format = pixelformat;

        ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMESIZES, &frame);
        if (ret < 0) {
            break;
        }

        g_device_size[i].width = frame.discrete.width;
        g_device_size[i].height = frame.discrete.height;
        g_device_size_num++;
    }

    if (i >= DEVICE_UVC_SIZE_NUM) {
        sample_print("DEVICE_UVC_SIZE_NUM(%d) is less than %u, change it's value by yourself!\n",
            DEVICE_UVC_SIZE_NUM, i);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_s32 sample_uvc_check_video_size(device_info *dev, hi_u32 pixelformat, hi_u32 width, hi_u32 height)
{
    hi_u32 i;
    hi_s32 ret;

    if (pixelformat == V4L2_PIX_FMT_NV21 || pixelformat == V4L2_PIX_FMT_NV12) {
        return HI_SUCCESS;
    }

    ret = sample_uvc_get_device_size(dev, pixelformat, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    for (i = 0; i < g_device_size_num; i++) {
        if ((width == g_device_size[i].width) && (height == g_device_size[i].height)) {
            return HI_SUCCESS;
        }
    }

    sample_print("usb device not support this video size(w:%u h:%u) or not support this format!\n", width, height);
    return HI_FAILURE;
}

static hi_s32 sample_uvc_video_set_format(device_info *dev, uvc_ctrl_info *ctrl_info)
{
    struct v4l2_format format = {0};
    hi_u32 i;
    hi_s32 ret;

    format.type = dev->type;

    if (sample_uvc_video_is_mplane(dev)) {
        const format_info *info = sample_uvc_v4l2_format_by_fourcc(ctrl_info->pixelformat);

        format.fmt.pix_mp.width = ctrl_info->width;
        format.fmt.pix_mp.height = ctrl_info->height;
        format.fmt.pix_mp.pixelformat = ctrl_info->pixelformat;
        format.fmt.pix_mp.field = ctrl_info->field;
        format.fmt.pix_mp.num_planes = info->n_planes;
        format.fmt.pix_mp.flags = ctrl_info->fmt_flags;

        for (i = 0; i < format.fmt.pix_mp.num_planes; i++) {
            format.fmt.pix_mp.plane_fmt[i].bytesperline = ctrl_info->stride;
            format.fmt.pix_mp.plane_fmt[i].sizeimage = ctrl_info->buffer_size;
        }
    } else if (sample_uvc_video_is_meta(dev)) {
        format.fmt.meta.dataformat = ctrl_info->pixelformat;
        format.fmt.meta.buffersize = ctrl_info->buffer_size;
    } else {
        format.fmt.pix.width = ctrl_info->width;
        format.fmt.pix.height = ctrl_info->height;
        format.fmt.pix.pixelformat = ctrl_info->pixelformat;
        format.fmt.pix.field = ctrl_info->field;
        format.fmt.pix.bytesperline = ctrl_info->stride;
        format.fmt.pix.sizeimage = ctrl_info->buffer_size;
        format.fmt.pix.priv = V4L2_PIX_FMT_PRIV_MAGIC;
        format.fmt.pix.flags = ctrl_info->fmt_flags;
    }

    ret = sample_uvc_check_video_size(dev, ctrl_info->pixelformat, format.fmt.pix.width, format.fmt.pix.height);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    ret = ioctl(dev->fd, VIDIOC_S_FMT, &format);
    if (ret < 0) {
        sample_print("set format failed!\n");
        return ret;
    }

    sample_uvc_print_format(dev, &format);

    return 0;
}

static hi_s32 sample_uvc_video_set_frame_rate(device_info *dev, struct v4l2_fract *time_per_frame)
{
    struct v4l2_streamparm stream_parm = {0};
    hi_s32 ret;

    stream_parm.type = dev->type;

    ret = ioctl(dev->fd, VIDIOC_G_PARM, &stream_parm);
    if (ret < 0) {
        sample_print("get frame rate failed!\n");
        return ret;
    }

    printf("Current frame rate: %u/%u\n", stream_parm.parm.capture.timeperframe.numerator,
        stream_parm.parm.capture.timeperframe.denominator);

    printf("Setting frame rate to: %u/%u\n", time_per_frame->numerator,
        time_per_frame->denominator);

    stream_parm.parm.capture.timeperframe.numerator = time_per_frame->numerator;
    stream_parm.parm.capture.timeperframe.denominator = time_per_frame->denominator;

    ret = ioctl(dev->fd, VIDIOC_S_PARM, &stream_parm);
    if (ret < 0) {
        sample_print("Unable to set frame rate.\n");
        return ret;
    }

    ret = ioctl(dev->fd, VIDIOC_G_PARM, &stream_parm);
    if (ret < 0) {
        sample_print("Unable to get frame rate.\n");
        return ret;
    }

    printf("Frame rate set: %u/%u\n", stream_parm.parm.capture.timeperframe.numerator,
        stream_parm.parm.capture.timeperframe.denominator);
    return 0;
}

static hi_s32 sample_uvc_video_buffer_mmap(device_info *dev, buffer_info *buffer, struct v4l2_buffer *v4l2buf)
{
    hi_u32 length;
    hi_u32 offset;
    hi_u32 i;
    hi_s32 ret;

    (hi_void)memset_s(buffer->mem, sizeof(buffer->mem), 0, sizeof(buffer->mem));
    for (i = 0; i < dev->num_planes; i++) {
        if (sample_uvc_video_is_mplane(dev)) {
            length = v4l2buf->m.planes[i].length;
            offset = v4l2buf->m.planes[i].m.mem_offset;
        } else {
            length = v4l2buf->length;
            offset = v4l2buf->m.offset;
        }

        buffer->mem[i] = mmap(0, length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, offset);
        if (buffer->mem[i] == MAP_FAILED) {
            sample_print("Unable to map buffer %u/%u\n", buffer->idx, i);
            goto fail;
        }

        buffer->size[i] = length;
        buffer->padding[i] = 0;

        printf("Buffer %u/%u mapped\n", buffer->idx, i);
    }

    return 0;
fail:
    for (i = 0; i < dev->num_planes; i++) {
        if (buffer->mem[i] == HI_NULL) {
            continue;
        }
        if (sample_uvc_video_is_mplane(dev)) {
            length = v4l2buf->m.planes[i].length;
            offset = v4l2buf->m.planes[i].m.mem_offset;
        } else {
            length = v4l2buf->length;
            offset = v4l2buf->m.offset;
        }
        ret = munmap(buffer->mem[i], length);
        if (ret != 0) {
            sample_print("munmap fail, errno %d\n", errno);
        }
        buffer->mem[i] = HI_NULL;
    }
    return -1;
}

static hi_s32 sample_uvc_video_buffer_munmap(const device_info *dev, buffer_info *buffer)
{
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; i < dev->num_planes; i++) {
        ret = munmap(buffer->mem[i], buffer->size[i]);
        if (ret < 0) {
            sample_print("Unable to unmap buffer %u/%u\n", buffer->idx, i);
        }

        buffer->mem[i] = HI_NULL;
    }

    return 0;
}

static hi_s32 sample_uvc_video_buffer_alloc_userptr(device_info *dev, buffer_info *buffer,
    struct v4l2_buffer *v4l2buf, hi_u32 offset, hi_u32 padding)
{
    hi_s32 page_size = getpagesize();
    hi_u32 length;
    hi_u32 i;
    hi_s32 ret;

    (hi_void)memset_s(buffer->mem, sizeof(buffer->mem), 0, sizeof(buffer->mem));
    for (i = 0; i < dev->num_planes; i++) {
        if (sample_uvc_video_is_mplane(dev)) {
            length = v4l2buf->m.planes[i].length;
        } else {
            length = v4l2buf->length;
        }

        ret = posix_memalign(&buffer->mem[i], page_size, length + offset + padding);
        if (ret != 0 || buffer->mem[i] == NULL) {
            sample_print("Unable to allocate buffer %u/%u (%d)\n", buffer->idx, i, ret);
            goto fail;
        }

        buffer->mem[i] += offset;
        buffer->size[i] = length;
        buffer->padding[i] = padding;

        printf("Buffer %u/%u allocated\n", buffer->idx, i);
    }

    return 0;
fail:
    for (i = 0; i < dev->num_planes; i++) {
        if (buffer->mem[i] != HI_NULL) {
            free(buffer->mem[i]);
            buffer->mem[i] = HI_NULL;
        }
    }
    return -1;
}

static hi_void sample_uvc_video_buffer_free_userptr(const device_info *dev, buffer_info *buffer)
{
    hi_u32 i;

    for (i = 0; i < dev->num_planes; i++) {
        free(buffer->mem[i]);
        buffer->mem[i] = HI_NULL;
    }
}

static hi_void sample_uvc_video_buffer_fill_userptr(device_info *dev, buffer_info *buffer, struct v4l2_buffer *v4l2buf)
{
    hi_u32 i;

    if (!sample_uvc_video_is_mplane(dev)) {
        v4l2buf->m.userptr = (hi_ulong)(uintptr_t)buffer->mem[0];
        return;
    }

    for (i = 0; i < dev->num_planes; i++) {
        v4l2buf->m.planes[i].m.userptr = (hi_ulong)(uintptr_t)buffer->mem[i];
    }
}

static hi_void sample_uvc_get_ts_flags(hi_u32 buf_flag, const hi_char **ts_type, const hi_char **ts_src)
{
    switch (buf_flag & V4L2_BUF_FLAG_TSTAMP_SRC_MASK) {
        case V4L2_BUF_FLAG_TSTAMP_SRC_SOE:
            *ts_src = "soe";
            break;
        case V4L2_BUF_FLAG_TSTAMP_SRC_EOF:
            *ts_src = "eof";
            break;
        default:
            *ts_src = "inv";
    }

    switch (buf_flag & V4L2_BUF_FLAG_TIMESTAMP_MASK) {
        case V4L2_BUF_FLAG_TIMESTAMP_COPY:
            *ts_type = "copy";
            break;
        case V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC:
            *ts_type = "monotonic";
            break;
        case V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN:
            *ts_type = "unknown";
            break;
        default:
            *ts_type = "inv";
    }
}

static hi_s32 sample_uvc_map_buffer(device_info *dev, buffer_info *buffers,
    hi_u32 count, hi_u32 offset, hi_u32 padding)
{
    hi_u32 i;
    hi_s32 ret;
    struct v4l2_buffer buffer;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    for (i = 0; i < count; ++i) {
        const hi_char *ts_type;
        const hi_char *ts_source;

        (hi_void)memset_s(&buffer, sizeof(buffer), 0, sizeof(buffer));
        (hi_void)memset_s(planes, sizeof(planes), 0, sizeof(planes));

        buffer.index = i;
        buffer.type = dev->type;
        buffer.memory = dev->memtype;
        buffer.length = VIDEO_MAX_PLANES;
        buffer.m.planes = planes;

        ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &buffer);
        if (ret < 0) {
            sample_print("Unable to query buffer %u\n", i);
            return ret;
        }
        sample_uvc_get_ts_flags(buffer.flags, &ts_type, &ts_source);
        printf("length: %u offset: %u timestamp type/source: %s/%s\n",
            buffer.length, buffer.m.offset, ts_type, ts_source);

        buffers[i].idx = i;

        if (dev->memtype == V4L2_MEMORY_USERPTR) {
            ret = sample_uvc_video_buffer_alloc_userptr(dev, &buffers[i], &buffer, offset, padding);
        } else if (dev->memtype == V4L2_MEMORY_MMAP) {
            ret = sample_uvc_video_buffer_mmap(dev, &buffers[i], &buffer);
        }

        if (ret < 0) {
            return ret;
        }
    }

    dev->timestamp_type = buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
    dev->buffers = buffers;
    dev->nbufs = count;

    return HI_SUCCESS;
}

static hi_s32 sample_uvc_video_alloc_buffers(device_info *dev, hi_u32 nbufs, hi_u32 offset, hi_u32 padding)
{
    struct v4l2_requestbuffers rb;
    buffer_info *buffers;
    hi_s32 ret;

    (hi_void)memset_s(&rb, sizeof(rb), 0, sizeof(rb));
    rb.count = nbufs;
    rb.type = dev->type;
    rb.memory = dev->memtype;

    ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
    if (ret < 0) {
        sample_print("Unable to request buffers.\n");
        return ret;
    }

    printf("%u buffers requested.\n", rb.count);

    buffers = calloc(rb.count, sizeof(buffer_info));
    if (buffers == HI_NULL) {
        return -ENOMEM;
    }
    ret = sample_uvc_map_buffer(dev, buffers, rb.count, offset, padding);
    if (ret != HI_SUCCESS) {
        free(buffers);
        return ret;
    }

    return 0;
}

static hi_s32 sample_uvc_video_free_buffers(device_info *dev)
{
    struct v4l2_requestbuffers rb = {0};
    hi_u32 i;
    hi_s32 ret;

    if (dev->nbufs == 0) {
        return 0;
    }

    for (i = 0; i < dev->nbufs; ++i) {
        if (dev->memtype == V4L2_MEMORY_USERPTR) {
            sample_uvc_video_buffer_free_userptr(dev, &dev->buffers[i]);
        } else if (dev->memtype == V4L2_MEMORY_MMAP) {
            ret = sample_uvc_video_buffer_munmap(dev, &dev->buffers[i]);
            if (ret < 0) {
                return ret;
            }
        }
    }

    rb.count = 0;
    rb.type = dev->type;
    rb.memory = dev->memtype;

    ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
    if (ret < 0) {
        return ret;
    }

    sample_print("%u buffers released.\n", dev->nbufs);

    free(dev->buffers);
    dev->nbufs = 0;
    dev->buffers = HI_NULL;

    return 0;
}

static hi_void sample_uvc_get_time(device_info *dev, struct v4l2_buffer *buf)
{
    if (sample_uvc_video_is_output(dev)) {
        buf->flags = dev->buffer_output_flags;
        if (dev->timestamp_type == V4L2_BUF_FLAG_TIMESTAMP_COPY) {
            struct timespec ts;

            clock_gettime(CLOCK_MONOTONIC, &ts);
            buf->timestamp.tv_sec = ts.tv_sec;
            buf->timestamp.tv_usec = ts.tv_nsec / 1000;  /* 1000: ratio */
        }
    }
}

static hi_s32 sample_uvc_video_queue_buffer(device_info *dev, hi_u32 index, enum buffer_fill_mode fill)
{
    struct v4l2_buffer buf = {0};
    struct v4l2_plane planes[VIDEO_MAX_PLANES] = {0};
    hi_u32 i;

    buf.index = index;
    buf.type = dev->type;
    buf.memory = dev->memtype;

    sample_uvc_get_time(dev, &buf);

    if (sample_uvc_video_is_mplane(dev)) {
        buf.m.planes = planes;
        buf.length = dev->num_planes;
    }

    if (dev->memtype == V4L2_MEMORY_USERPTR) {
        if (sample_uvc_video_is_mplane(dev)) {
            for (i = 0; i < dev->num_planes; i++) {
                buf.m.planes[i].m.userptr = (hi_ulong)(uintptr_t)dev->buffers[index].mem[i];
                buf.m.planes[i].length = dev->buffers[index].size[i];
            }
        } else {
            buf.m.userptr = (hi_ulong)(uintptr_t)dev->buffers[index].mem[0];
            buf.length = dev->buffers[index].size[0];
        }
    }

    for (i = 0; i < dev->num_planes; i++) {
        if (sample_uvc_video_is_output(dev)) {
            if (sample_uvc_video_is_mplane(dev)) {
                buf.m.planes[i].bytesused = dev->patternsize[i];
            } else {
                buf.bytesused = dev->patternsize[i];
            }

            errno_t ret = memcpy_s(dev->buffers[buf.index].mem[i], dev->patternsize[i],
                dev->pattern[i], dev->patternsize[i]);
            if (ret != EOK) {
                sample_print("memcpy_s buffers mem fail %d\n", ret);
                return -1;
            }
        } else {
            if (fill & BUFFER_FILL_FRAME) {
                (hi_void)memset_s(dev->buffers[buf.index].mem[i], dev->buffers[index].size[i],
                    0x55, dev->buffers[index].size[i]);
            }
            if (fill & BUFFER_FILL_PADDING) {
                (hi_void)memset_s(dev->buffers[buf.index].mem[i] + dev->buffers[index].size[i],
                    dev->buffers[index].padding[i], 0x55, dev->buffers[index].padding[i]);
            }
        }
    }

    return ioctl(dev->fd, VIDIOC_QBUF, &buf);
}

static hi_s32 sample_uvc_video_enable(device_info *dev, hi_bool enable)
{
    hi_s32 type = dev->type;
    hi_s32 ret;

    ret = ioctl(dev->fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        sample_print("Unable to %s streaming.\n", enable ? "start" : "stop");
        return ret;
    }

    return 0;
}

static hi_s32 sample_uvc_video_for_each_control(device_info *dev,
    hi_s32(*callback)(device_info *dev, const struct v4l2_query_ext_ctrl *query))
{
    struct v4l2_query_ext_ctrl query_ext_ctrl;
    hi_s32 nctrls = 0;
    hi_u32 id;
    hi_s32 ret;

    id = 0;
    while (1) {
        id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;

        ret = sample_uvc_query_ext_control(dev, id, &query_ext_ctrl);
        if (ret == -EINVAL) {
            break;
        }
        if (ret < 0) {
            return ret;
        }

        id = query_ext_ctrl.id;

        ret = callback(dev, &query_ext_ctrl);
        if (ret < 0) {
            return ret;
        }

        if (ret > 0) {
            nctrls++;
        }
    }

    return nctrls;
}

static hi_void sample_uvc_video_query_menu(device_info *dev, const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    hi_u32 value)
{
    struct v4l2_querymenu menu;
    hi_s32 ret;

    for (menu.index = query_ext_ctrl->minimum; menu.index <= (hi_u32)query_ext_ctrl->maximum; menu.index++) {
        menu.id = query_ext_ctrl->id;
        ret = ioctl(dev->fd, VIDIOC_QUERYMENU, &menu);
        if (ret < 0) {
            continue;
        }

        if (query_ext_ctrl->type == V4L2_CTRL_TYPE_MENU) {
            sample_print("  %u: %.32s%s\n", menu.index, menu.name, menu.index == value ? " (*)" : "");
        } else {
            sample_print("  %u: %lld%s\n", menu.index, menu.value, menu.index == value ? " (*)" : "");
        }
    };
}

static hi_void sample_uvc_video_print_control_array(const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    const struct v4l2_ext_control *ext_control)
{
    hi_u32 i;

    printf("{");

    for (i = 0; i < query_ext_ctrl->elems; ++i) {
        switch (query_ext_ctrl->type) {
            case V4L2_CTRL_TYPE_U8:
                printf("%u", ext_control->p_u8[i]);
                break;
            case V4L2_CTRL_TYPE_U16:
                printf("%u", ext_control->p_u16[i]);
                break;
            case V4L2_CTRL_TYPE_U32:
                printf("%u", ext_control->p_u32[i]);
                break;
            default:
                printf("invalid");
                break;
        }

        if (i != query_ext_ctrl->elems - 1) {
            printf(", ");
        }
    }

    printf("}");
}

static hi_void sample_uvc_video_print_control_value(const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    struct v4l2_ext_control *ext_control)
{
    if (query_ext_ctrl->nr_of_dims == 0) {
        switch (query_ext_ctrl->type) {
            case V4L2_CTRL_TYPE_INTEGER:
            case V4L2_CTRL_TYPE_BOOLEAN:
            case V4L2_CTRL_TYPE_MENU:
            case V4L2_CTRL_TYPE_INTEGER_MENU:
                sample_print("%d", ext_control->value);
                break;
            case V4L2_CTRL_TYPE_BITMASK:
                sample_print("0x%08x", ext_control->value);
                break;
            case V4L2_CTRL_TYPE_INTEGER64:
                sample_print("%lld", ext_control->value64);
                break;
            case V4L2_CTRL_TYPE_STRING:
                sample_print("%s", ext_control->string);
                break;
            default:
                sample_print("invalid");
                break;
        }

        return;
    }

    switch (query_ext_ctrl->type) {
        case V4L2_CTRL_TYPE_U8:
        case V4L2_CTRL_TYPE_U16:
        case V4L2_CTRL_TYPE_U32:
            sample_uvc_video_print_control_array(query_ext_ctrl, ext_control);
            break;
        default:
            sample_print("unsupported type %u", query_ext_ctrl->type);
            break;
    }
}

static hi_s32 sample_uvc_video_get_control(device_info *dev, const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    hi_bool full)
{
    struct v4l2_ext_control ctrl;
    hi_s32 ret;

    if (query_ext_ctrl->flags & V4L2_CTRL_FLAG_DISABLED) {
        return 0;
    }

    if (query_ext_ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS) {
        printf("--- %s (class 0x%08x) ---\n", query_ext_ctrl->name, query_ext_ctrl->id);
        return 0;
    }

    if (full == HI_TRUE) {
        printf("control 0x%08x `%s' min %lld max %lld step %llu default %lld ",
            query_ext_ctrl->id, query_ext_ctrl->name, query_ext_ctrl->minimum, query_ext_ctrl->maximum,
            query_ext_ctrl->step, query_ext_ctrl->default_value);
        if (query_ext_ctrl->nr_of_dims != 0) {
            for (hi_u32 i = 0; i < query_ext_ctrl->nr_of_dims; ++i) {
                printf("[%u]", query_ext_ctrl->dims[i]);
            }
            printf(" ");
        }
    } else {
        printf("control 0x%08x ", query_ext_ctrl->id);
    }

    if (query_ext_ctrl->type == V4L2_CTRL_TYPE_BUTTON) {
        printf("\n");
        return 1;
    }

    printf("current ");

    ret = sample_uvc_get_control(dev, query_ext_ctrl, &ctrl, V4L2_CTRL_WHICH_CUR_VAL);
    if (ret < 0) {
        printf("n/a\n");
        printf("unable to get control 0x%8.8x\n", query_ext_ctrl->id);
    } else {
        sample_uvc_video_print_control_value(query_ext_ctrl, &ctrl);
        printf("\n");
    }

    if (query_ext_ctrl->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) {
        if (ctrl.ptr != HI_NULL) {
            free(ctrl.ptr);
            ctrl.ptr = HI_NULL;
        }
    }

    if (!full) {
        return 1;
    }

    if (query_ext_ctrl->type == V4L2_CTRL_TYPE_MENU || query_ext_ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU) {
        sample_uvc_video_query_menu(dev, query_ext_ctrl, ctrl.value);
    }

    return 1;
}

static hi_s32 sample_uvc_video_get_ctrl(device_info *dev, const struct v4l2_query_ext_ctrl *query)
{
    return sample_uvc_video_get_control(dev, query, HI_TRUE);
}

static hi_s32 sample_uvc_read_ctrl_file(struct v4l2_ext_control *ext_control, const hi_char *val)
{
    ssize_t size;
    hi_s32 fd;

    val++;
    hi_char *regular_path = realpath(val, NULL);
    if (regular_path == NULL) {
        sample_print("unable to get regular path\n");
        return -EINVAL;
    }
    fd = open(regular_path, O_RDONLY);
    free(regular_path);
    if (fd < 0) {
        sample_print("unable to open control file `%s'\n", val);
        return -EINVAL;
    }

    size = read(fd, ext_control->ptr, ext_control->size);
    if (size != (ssize_t)ext_control->size) {
        sample_print("error reading control file `%s'\n", val);
        close(fd);
        return -EINVAL;
    }

    close(fd);
    return HI_SUCCESS;
}

static hi_s32 sample_uvc_video_parse_control_array(const struct v4l2_query_ext_ctrl *query_ext_ctrl,
    struct v4l2_ext_control *ext_control, const hi_char *value)
{
    hi_u32 i;
    hi_char *end_ptr;

    for (; isspace(*value); ++value) { }

    if (*value == '<') {
        return sample_uvc_read_ctrl_file(ext_control, value);
    }

    if (*value++ != '{') {
        return -EINVAL;
    }

    for (i = 0; i < query_ext_ctrl->elems; ++i) {
        for (; isspace(*value); ++value) { }

        unsigned long val = strtoul(value, &end_ptr, 0);
        if (end_ptr == HI_NULL || val == ULONG_MAX) {
            return -EINVAL;
        }
        switch (query_ext_ctrl->type) {
            case V4L2_CTRL_TYPE_U8:
                ext_control->p_u8[i] = (td_u8)val;
                break;
            case V4L2_CTRL_TYPE_U16:
                ext_control->p_u16[i] = (td_u16)val;
                break;
            case V4L2_CTRL_TYPE_U32:
                ext_control->p_u32[i] = (td_u32)val;
                break;
            default:
                ext_control->p_u8[i] = (td_u8)val;
                break;
        }

        value = end_ptr;
        for (; isspace(*value); ++value) { }
        if (*value != ',') {
            break;
        }
        value++;
    }

    if (i < query_ext_ctrl->elems - 1) {
        return -EINVAL;
    }

    for (; isspace(*value); ++value) { }
    if (*value++ != '}') {
        return -EINVAL;
    }

    for (; isspace(*value); ++value) { }
    if (*value++ != '\0') {
        return -EINVAL;
    }
    return 0;
}

static hi_s32 sample_uvc_get_non_nr_of_dims_ctrl(struct v4l2_query_ext_ctrl *query_ext_ctrl, const hi_char *val,
    struct v4l2_ext_control *ext_control)
{
    hi_char *end_ptr;
    errno_t ret;

    switch (query_ext_ctrl->type) {
        case V4L2_CTRL_TYPE_INTEGER:
        case V4L2_CTRL_TYPE_BOOLEAN:
        case V4L2_CTRL_TYPE_MENU:
        case V4L2_CTRL_TYPE_INTEGER_MENU:
        case V4L2_CTRL_TYPE_BITMASK:
            ext_control->value = (hi_s32)strtol(val, &end_ptr, 0);
            if (*end_ptr != 0) {
                sample_print("control value '%s' error!\n", val);
                return HI_FAILURE;
            }
            break;
        case V4L2_CTRL_TYPE_INTEGER64:
            ext_control->value64 = strtoll(val, &end_ptr, 0);
            if (*end_ptr != 0) {
                sample_print("control value '%s' error!\n", val);
                return HI_FAILURE;
            }
            break;
        case V4L2_CTRL_TYPE_STRING:
            ext_control->size = query_ext_ctrl->elem_size;
            ext_control->ptr = malloc(ext_control->size);
            if (ext_control->ptr == HI_NULL) {
                return HI_FAILURE;
            }
            ret = strncpy_s(ext_control->string, ext_control->size, val, ext_control->size);
            if (ret != EOK) {
                sample_print("strncpy_s string fail %x\n", ret);
                return HI_FAILURE;
            }
            break;
        default:
            sample_print("control type not support!\n");
            return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_void sample_uvc_video_set_control(device_info *dev, hi_u32 id, const hi_char *val)
{
    struct v4l2_query_ext_ctrl query_ext_ctrl;
    struct v4l2_ext_control ext_control = {0};
    hi_s32 ret;

    ret = sample_uvc_query_control(dev, id, &query_ext_ctrl);
    if (ret < 0) {
        return;
    }

    if (query_ext_ctrl.nr_of_dims == 0) {
        ret = sample_uvc_get_non_nr_of_dims_ctrl(&query_ext_ctrl, val, &ext_control);
        if (ret != HI_SUCCESS) {
            return;
        }
    } else {
        switch (query_ext_ctrl.type) {
            case V4L2_CTRL_TYPE_U8:
            case V4L2_CTRL_TYPE_U16:
            case V4L2_CTRL_TYPE_U32:
                ext_control.size = query_ext_ctrl.elem_size * query_ext_ctrl.elems;
                ext_control.ptr = malloc(ext_control.size);
                if (ext_control.ptr == HI_NULL) {
                    return;
                }
                ret = sample_uvc_video_parse_control_array(&query_ext_ctrl, &ext_control, val);
                if (ret < 0) {
                    free(ext_control.ptr);
                    ext_control.ptr = HI_NULL;
                    sample_print("Invalid compound control value '%s'\n", val);
                    return;
                }
                break;
            default:
                sample_print("Unsupported control type %u\n", query_ext_ctrl.type);
                break;
        }
    }

    ret = sample_uvc_set_control(dev, &query_ext_ctrl, &ext_control);
    if (ret < 0) {
        sample_print("unable to set control 0x%8.8x\n", id);
    } else {
        sample_print("Control 0x%08x set to %s, is ", id, val);

        sample_uvc_video_print_control_value(&query_ext_ctrl, &ext_control);
        sample_print("\n");
    }

    if ((query_ext_ctrl.flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) && ext_control.ptr) {
        free(ext_control.ptr);
        ext_control.ptr = HI_NULL;
    }
}

static hi_void sample_uvc_video_list_controls(device_info *dev)
{
    hi_s32 ret;

    ret = sample_uvc_video_for_each_control(dev, sample_uvc_video_get_ctrl);
    if (ret < 0) {
        return;
    }

    if (ret != 0) {
        sample_print("%d control%s found.\n", ret, ret > 1 ? "s" : "");
    } else {
        sample_print("No control found.\n");
    }
}

static hi_s32 sample_uvc_video_reset_control(device_info *dev, const struct v4l2_query_ext_ctrl *query)
{
    struct v4l2_ext_control ctrl = { .value = query->default_value, };
    hi_s32 ret;

    if (query->flags & V4L2_CTRL_FLAG_DISABLED) {
        return 0;
    }

    if (query->type == V4L2_CTRL_TYPE_CTRL_CLASS) {
        return 0;
    }

    if (query->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) {
        ret = sample_uvc_get_control(dev, query, &ctrl, V4L2_CTRL_WHICH_DEF_VAL);
        if (ret < 0) {
            return 0;
        }
    }

    sample_uvc_set_control(dev, query, &ctrl);

    if (query->flags & V4L2_CTRL_FLAG_HAS_PAYLOAD) {
        if (ctrl.ptr != HI_NULL) {
            free(ctrl.ptr);
            ctrl.ptr = HI_NULL;
        }
    }

    return 1;
}

static hi_void sample_uvc_video_reset_controls(device_info *dev)
{
    hi_s32 ret;

    ret = sample_uvc_video_for_each_control(dev, sample_uvc_video_reset_control);
    if (ret < 0) {
        return;
    }

    if (ret != 0) {
        sample_print("%d control%s reset.\n", ret, ret > 1 ? "s" : "");
    }
}

static hi_void sample_uvc_video_send_extension(device_info *dev, hi_u32 id, const hi_char *val)
{
    hi_u8 data[64] = {0};
    hi_u32 len;
    struct uvc_xu_control_query ext = {
        .unit = id,
        .query = UVC_GET_LEN,
        .selector = (hi_u8)strtol(val, NULL, 0),
        .size = 0x2,
        .data = data,
    };
    if (ioctl(dev->fd, UVCIOC_CTRL_QUERY, &ext) != 0) {
        sample_print("Invalid Unit '%u'? or Invalid channel '%u'?\n", id, ext.selector);
        return;
    }

    (hi_void)memcpy_s(&len, sizeof(len), data, sizeof(len));
    for (hi_u32 i = 0; i < len; i++) {
        data[i] = (hi_u8)i;
    }

    ext.query = UVC_SET_CUR;
    ext.size = (hi_u16)len;
    if (ioctl(dev->fd, UVCIOC_CTRL_QUERY, &ext) != 0) {
        sample_print("send extension cmd (unit:%u channel:%u) fail.\n", id, ext.selector);
        return;
    } else {
        sample_print("send extension cmd (unit:%u channel:%u) success.\n", id, ext.selector);
    }

    return;
}

static hi_void sample_uvc_video_enum_frame_intervals(device_info *dev, hi_u32 pixel_format,
    hi_u32 width, hi_u32 height)
{
    struct v4l2_frmivalenum frmivalenum;
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; ; ++i) {
        (hi_void)memset_s(&frmivalenum, sizeof(frmivalenum), 0, sizeof(frmivalenum));
        frmivalenum.index = i;
        frmivalenum.pixel_format = pixel_format;
        frmivalenum.width = width;
        frmivalenum.height = height;
        ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmivalenum);
        if (ret < 0) {
            break;
        }

        if (i != frmivalenum.index) {
            sample_print("Warning: driver returned wrong ival index %u.\n", frmivalenum.index);
        }
        if (pixel_format != frmivalenum.pixel_format) {
            sample_print("Warning: driver returned wrong ival pixel format %08x.\n", frmivalenum.pixel_format);
        }
        if (width != frmivalenum.width) {
            sample_print("Warning: driver returned wrong ival width %u.\n", frmivalenum.width);
        }
        if (height != frmivalenum.height) {
            sample_print("Warning: driver returned wrong ival height %u.\n", frmivalenum.height);
        }

        if (i != 0) {
            printf(", ");
        }

        switch (frmivalenum.type) {
            case V4L2_FRMIVAL_TYPE_DISCRETE:
                printf("%u/%u", frmivalenum.discrete.numerator, frmivalenum.discrete.denominator);
                break;

            case V4L2_FRMIVAL_TYPE_CONTINUOUS:
                printf("%u/%u - %u/%u", frmivalenum.stepwise.min.numerator, frmivalenum.stepwise.min.denominator,
                    frmivalenum.stepwise.max.numerator, frmivalenum.stepwise.max.denominator);
                return;

            case V4L2_FRMIVAL_TYPE_STEPWISE:
                printf("%u/%u - %u/%u (by %u/%u)", frmivalenum.stepwise.min.numerator,
                    frmivalenum.stepwise.min.denominator, frmivalenum.stepwise.max.numerator,
                    frmivalenum.stepwise.max.denominator, frmivalenum.stepwise.step.numerator,
                    frmivalenum.stepwise.step.denominator);
                return;

            default:
                break;
        }
    }
}

static hi_void sample_uvc_video_enum_frame_sizes(device_info *dev, hi_u32 pixelformat)
{
    struct v4l2_frmsizeenum frame;
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; ; ++i) {
        (hi_void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));
        frame.index = i;
        frame.pixel_format = pixelformat;
        ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMESIZES, &frame);
        if (ret < 0) {
            break;
        }

        if (i != frame.index) {
            sample_print("Warning: driver returned wrong frame index %u.\n", frame.index);
        }
        if (pixelformat != frame.pixel_format) {
            sample_print("Warning: driver returned wrong frame pixel format %08x.\n", frame.pixel_format);
        }

        switch (frame.type) {
            case V4L2_FRMSIZE_TYPE_DISCRETE:
                printf("\tFrame size: %ux%u (", frame.discrete.width, frame.discrete.height);
                sample_uvc_video_enum_frame_intervals(dev, frame.pixel_format, frame.discrete.width,
                    frame.discrete.height);
                printf(")\n");
                break;

            case V4L2_FRMSIZE_TYPE_CONTINUOUS:
                printf("\tFrame size: %ux%u - %ux%u (", frame.stepwise.min_width, frame.stepwise.min_height,
                    frame.stepwise.max_width, frame.stepwise.max_height);
                sample_uvc_video_enum_frame_intervals(dev, frame.pixel_format,
                    frame.stepwise.max_width, frame.stepwise.max_height);
                printf(")\n");
                break;

            case V4L2_FRMSIZE_TYPE_STEPWISE:
                printf("\tFrame size: %ux%u - %ux%u (by %ux%u) (\n",
                    frame.stepwise.min_width, frame.stepwise.min_height,
                    frame.stepwise.max_width, frame.stepwise.max_height,
                    frame.stepwise.step_width, frame.stepwise.step_height);
                sample_uvc_video_enum_frame_intervals(dev, frame.pixel_format,
                    frame.stepwise.max_width, frame.stepwise.max_height);
                printf(")\n");
                break;

            default:
                break;
        }
    }
}

static hi_void sample_uvc_video_enum_formats(device_info *dev, enum v4l2_buf_type type)
{
    struct v4l2_fmtdesc fmt;
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; ; ++i) {
        (hi_void)memset_s(&fmt, sizeof(fmt), 0, sizeof(fmt));
        fmt.index = i;
        fmt.type = type;
        ret = ioctl(dev->fd, VIDIOC_ENUM_FMT, &fmt);
        if (ret < 0) {
            break;
        }

        if (i != fmt.index) {
            sample_print("Warning: driver returned wrong format index %u.\n", fmt.index);
        }
        if (type != fmt.type) {
            sample_print("Warning: driver returned wrong format type %u.\n", fmt.type);
        }

        printf("\tFormat %u: %s (%08x)\n", i, sample_uvc_v4l2_format_name(fmt.pixelformat), fmt.pixelformat);
        printf("\tType: %s (%u)\n", sample_uvc_v4l2_buf_type_name(fmt.type), fmt.type);
        printf("\tName: %.32s\n", fmt.description);
        sample_uvc_video_enum_frame_sizes(dev, fmt.pixelformat);
        printf("\n");
    }
}

static hi_void sample_uvc_video_enum_inputs(device_info *dev)
{
    struct v4l2_input input;
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; ; ++i) {
        (hi_void)memset_s(&input, sizeof(input), 0, sizeof(input));
        input.index = i;
        ret = ioctl(dev->fd, VIDIOC_ENUMINPUT, &input);
        if (ret < 0) {
            break;
        }

        if (i != input.index) {
            sample_print("Warning: driver returned wrong input index %u.\n", input.index);
        }

        sample_print("\tInput %u: %s.\n", i, input.name);
    }

    sample_print("\n");
}

static hi_s32 sample_uvc_video_get_input(device_info *dev)
{
    hi_s32 input;
    hi_s32 ret;

    ret = ioctl(dev->fd, VIDIOC_G_INPUT, &input);
    if (ret < 0) {
        sample_print("Unable to get current input.\n");
        return ret;
    }

    return input;
}

static hi_s32 sample_uvc_video_set_input(device_info *dev, hi_s32 input)
{
    hi_s32 _input = input;
    hi_s32 ret;

    ret = ioctl(dev->fd, VIDIOC_S_INPUT, &_input);
    if (ret < 0) {
        sample_print("Unable to select input %d\n", input);
    }

    return ret;
}

static hi_s32 sample_uvc_video_set_quality(device_info *dev, hi_u32 quality)
{
    struct v4l2_jpegcompression jpeg;
    hi_s32 ret;

    if (quality == (hi_u32)(-1)) {
        return 0;
    }

    (hi_void)memset_s(&jpeg, sizeof(jpeg), 0, sizeof(jpeg));
    jpeg.quality = (hi_s32)quality;

    ret = ioctl(dev->fd, VIDIOC_S_JPEGCOMP, &jpeg);
    if (ret < 0) {
        sample_print("Unable to set quality to %u\n", quality);
        return ret;
    }

    ret = ioctl(dev->fd, VIDIOC_G_JPEGCOMP, &jpeg);
    if (ret >= 0) {
        sample_print("Quality set to %d\n", jpeg.quality);
    }

    return 0;
}

static void sample_uvc_free_dev_pattern(device_info *dev)
{
    for (hi_u32 i = 0; i < dev->num_planes; i++) {
        if (dev->pattern[i] != HI_NULL) {
            free(dev->pattern[i]);
            dev->pattern[i] = HI_NULL;
        }
    }
}

static hi_s32 sample_uvc_video_load_test_pattern(device_info *dev, const hi_char *file_name)
{
    hi_u32 size;
    hi_s32 fd = -1;
    hi_s32 ret;

    if (file_name != HI_NULL) {
        fd = open_by_real_path(file_name, O_RDONLY);
        if (fd == -1) {
            sample_print("Unable to open test pattern file '%s'\n", file_name);
            return -errno;
        }
    }

    (hi_void)memset_s(dev->pattern, sizeof(dev->pattern), 0, sizeof(dev->pattern));
    for (hi_u32 i = 0; i < dev->num_planes; i++) {
        size = dev->buffers[0].size[i];
        dev->pattern[i] = malloc(size);
        if (dev->pattern[i] == HI_NULL) {
            ret = -ENOMEM;
            goto fail;
        }

        if (file_name != HI_NULL) {
            ret = read(fd, dev->pattern[i], size);
            if (ret != (hi_s32)size && dev->plane_fmt[i].bytesperline != 0) {
                sample_print("Test pattern file size %d doesn't match image size %u\n", ret, size);
                ret = -EINVAL;
                goto fail;
            }
        } else {
            uint8_t *data = dev->pattern[i];
            if (dev->plane_fmt[i].bytesperline == 0) {
                sample_print("Compressed format detected for plane %u, test pattern not generated automatically\n", i);
                ret = -EINVAL;
                goto fail;
            }

            for (hi_u32 j = 0; j < dev->plane_fmt[i].sizeimage; ++j) {
                *data++ = j;
            }
        }

        dev->patternsize[i] = size;
    }

    ret = TD_SUCCESS;
    goto done;
fail:
    sample_uvc_free_dev_pattern(dev);
done:
    close(fd);
    return ret;
}

static hi_s32 sample_uvc_video_prepare_capture(device_info *dev, hi_u32 nbufs, hi_u32 offset,
    const hi_char *filename, enum buffer_fill_mode fill)
{
    hi_u32 padding;
    hi_s32 ret;

    padding = (fill & BUFFER_FILL_PADDING) ? 4096 : 0;  /* 4096:padding */
    if ((ret = sample_uvc_video_alloc_buffers(dev, nbufs, offset, padding)) < 0) {
        return ret;
    }

    if (sample_uvc_video_is_output(dev)) {
        ret = sample_uvc_video_load_test_pattern(dev, filename);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static hi_s32 sample_uvc_video_queue_all_buffers(device_info *dev, enum buffer_fill_mode fill)
{
    hi_u32 i;
    hi_s32 ret;

    for (i = 0; i < dev->nbufs; ++i) {
        ret = sample_uvc_video_queue_buffer(dev, i, fill);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static hi_void sample_uvc_video_verify_buffer(device_info *dev, struct v4l2_buffer *buf)
{
    buffer_info *buffer = &dev->buffers[buf->index];
    hi_u32 plane;
    hi_u32 i;

    for (plane = 0; plane < dev->num_planes; ++plane) {
        const uint8_t *data = buffer->mem[plane] + buffer->size[plane];
        hi_u32 errors = 0;
        hi_u32 dirty = 0;
        hi_u32 length;

        if (sample_uvc_video_is_mplane(dev)) {
            length = buf->m.planes[plane].bytesused;
        } else {
            length = buf->bytesused;
        }

        if (dev->plane_fmt[plane].sizeimage && dev->plane_fmt[plane].sizeimage != length) {
            if (buffer->padding[plane] == 0) {
                continue;
            }
        }
        for (i = 0; i < buffer->padding[plane]; ++i) {
            if (data[i] != 0x55) {
                errors++;
                dirty = i + 1;
            }
        }

        if (errors == 0) {
            continue;
        }

        sample_print("Warning: %u bytes overwritten among %u first padding bytes for plane %u\n", errors, dirty, plane);

        dirty = (dirty + 15) & ~15;         /* 15:align */
        dirty = dirty > 32 ? 32 : dirty;    /* 32:limit */

        for (i = 0; i < dirty; ++i) {
            printf("%02x ", data[i]);
            if (i % 16 == 15) {             /* 15:end, 16:circle */
                printf("\n");
            }
        }
    }
}

static hi_void sample_uvc_video_save_image(device_info *dev, struct v4l2_buffer *buf,
    hi_u32 sequence, uvc_ctrl_info *ctrl_info)
{
    hi_u32 i;
    hi_size pic_size;
    hi_u32 stride;
    const hi_char *type_name = ctrl_info->type_name;

    pic_size.width = ctrl_info->width;
    pic_size.height = ctrl_info->height;
    stride = dev->plane_fmt[0].bytesperline;

    for (i = 0; i < dev->num_planes; i++) {
        hi_void *data = dev->buffers[buf->index].mem[i];
        hi_u32 length;

        if (sample_uvc_video_is_mplane(dev)) {
            length = buf->m.planes[i].bytesused;

            if (!dev->write_data_prefix) {
                data += buf->m.planes[i].data_offset;
                length -= buf->m.planes[i].data_offset;
            }
        } else {
            length = buf->bytesused;
        }

        if (sample_uvc_media_send_data(data, length, stride, &pic_size, type_name) != HI_SUCCESS) {
            sample_print("media_send_data failed!\n");
        }
    }
}

static hi_void sample_uvc_get_buf_cfg(device_info *dev, struct v4l2_plane planes[], hi_u32 len, struct v4l2_buffer *buf)
{
    hi_unused(len);

    (hi_void)memset_s(buf, sizeof(struct v4l2_buffer), 0, sizeof(struct v4l2_buffer));

    buf->type = dev->type;
    buf->memory = dev->memtype;
    buf->length = VIDEO_MAX_PLANES;
    buf->m.planes = planes;
}

static hi_s32 sample_uvc_dqueue_buf(device_info *dev, hi_u32 index, struct v4l2_buffer *buf)
{
    if (ioctl(dev->fd, VIDIOC_DQBUF, buf) < 0) {
        if (errno != EIO) {
            return HI_FAILURE;
        }
        buf->type = dev->type;
        buf->memory = dev->memtype;
        if (dev->memtype == V4L2_MEMORY_USERPTR) {
            sample_uvc_video_buffer_fill_userptr(dev, &dev->buffers[index], buf);
        }
    }
    return HI_SUCCESS;
}

static hi_void sample_uvc_print_fps(struct timespec *start, hi_u32 size, hi_u32 frame_num, struct timespec *ts)
{
    hi_double bps;
    hi_double fps;

    ts->tv_sec -= start->tv_sec;
    ts->tv_nsec -= start->tv_nsec;
    if (ts->tv_nsec < 0) {
        ts->tv_sec--;
        ts->tv_nsec += 1000000000;  /* 1000000000:time */
    }

    bps = size / (ts->tv_nsec / 1000.0 + 1000000.0 * ts->tv_sec) * 1000000.0;       /* 1000.0,1000000.0:time */
    fps = frame_num / (ts->tv_nsec / 1000.0 + 1000000.0 * ts->tv_sec) * 1000000.0;  /* 1000.0,1000000.0:time */

    sample_print("Captured %u frames in %ld.%06ld seconds (%f fps, %f B/s).\n",
        frame_num, (hi_slong)ts->tv_sec, ts->tv_nsec / 1000, fps, bps);                       /* 1000:time */
}

static hi_s32 sample_uvc_capture(device_info *dev, uvc_ctrl_info *ctrl_info,
    struct timespec *ts, hi_u32 *frame_num, hi_u32 *size)
{
    hi_u32 i;
    hi_s32 ret;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf;
    hi_u32 skip = ctrl_info->skip;

    for (i = 0; i < ctrl_info->nframes; ++i) {
        if (g_uvc_exit == HI_TRUE) {
            break;
        }

        (hi_void)memset_s(planes, sizeof(planes), 0, sizeof(planes));

        sample_uvc_get_buf_cfg(dev, planes, VIDEO_MAX_PLANES, &buf);

        ret = sample_uvc_dqueue_buf(dev, i, &buf);
        if (ret != HI_SUCCESS) {
            return HI_FAILURE;
        }

        if (sample_uvc_video_is_capture(dev)) {
            sample_uvc_video_verify_buffer(dev, &buf);
        }

        *size += buf.bytesused;

        clock_gettime(CLOCK_MONOTONIC, ts);

        if (sample_uvc_video_is_capture(dev) && ctrl_info->pattern && skip == 0) {
            sample_uvc_video_save_image(dev, &buf, i, ctrl_info);
        }

        if (skip > 0) {
            --skip;
        }

        if (ctrl_info->delay > 0) {
            usleep(ctrl_info->delay * 1000);    /* 1000:time */
        }

        (hi_void)fflush(stdout);

        if (ctrl_info->pause == i + 1) {
            sample_uvc_pause_wait();
        }

        if (i >= ctrl_info->nframes - dev->nbufs && !ctrl_info->do_requeue_last) {
            continue;
        }

        ret = sample_uvc_video_queue_buffer(dev, buf.index, ctrl_info->fill);
        if (ret < 0) {
            sample_print("Unable to requeue buffer.\n");
            return HI_FAILURE;
        }
    }

    *frame_num = i;

    return HI_SUCCESS;
}

static hi_s32 sample_uvc_video_do_capture(device_info *dev, uvc_ctrl_info *ctrl_info)
{
    struct timespec start;
    struct timespec ts;
    hi_u32 size;
    hi_s32 ret;
    hi_u32 frame_num;

    if (ctrl_info->pause == 0) {
        sample_uvc_pause_wait();
    }

    ret = sample_uvc_video_enable(dev, HI_TRUE);
    if (ret < 0) {
        goto done;
    }

    if (ctrl_info->do_queue_late) {
        sample_uvc_video_queue_all_buffers(dev, ctrl_info->fill);
    }

    size = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);

    ret = sample_uvc_capture(dev, ctrl_info, &ts, &frame_num, &size);
    if (ret != HI_SUCCESS) {
        goto done;
    }

    ret = sample_uvc_video_enable(dev, HI_FALSE);
    if (ret < 0) {
        return ret;
    }

    if (ctrl_info->nframes == 0) {
        sample_print("No frames captured.\n");
        goto done;
    }

    if (ts.tv_sec == start.tv_sec && ts.tv_nsec == start.tv_nsec) {
        sample_print("Captured %u frames (%u bytes) 0 seconds\n", frame_num, size);
        goto done;
    }

    sample_uvc_print_fps(&start, size, frame_num, &ts);

done:
    return sample_uvc_video_free_buffers(dev);
}

static hi_void sample_uvc_usage(const hi_char *argv0)
{
    printf("sample_uvc_usage: %s device [options]\n", argv0);
    printf("supported options:\n");
    printf("-f, --format format             set the video format\n");
    printf("-F, --file[=name]               write file\n");
    printf("-h, --help                      show help info\n");
    printf("-s, --size WxH                  set the frame size (eg. 1920x1080)\n\n");
    printf("inquire USB device format: ./sample_uvc /dev/video0 --enum-formats\n\n");
    printf("example of setting USB device format:\n");
    printf("    ./sample_uvc /dev/video0 -fH264  -s1920x1080 -Ftest.h264\n");
    printf("    ./sample_uvc /dev/video0 -fH265  -s1920x1080 -Ftest.h265\n");
    printf("    ./sample_uvc /dev/video0 -fMJPEG -s1920x1080 -Ftest.mjpg\n");
    printf("    ./sample_uvc /dev/video0 -fYUYV  -s1920x1080 -Ftest.yuv\n");
    printf("    ./sample_uvc /dev/video0 -fNV21  -s640x360   -Ftest.yuv\n\n");
}

static const hi_char *sample_uvc_video_get_data_type(device_info *dev)
{
    struct v4l2_format fmt;
    hi_s32 ret;
    (hi_void)memset_s(&fmt, sizeof(fmt), 0, sizeof(fmt));
    fmt.type = dev->type;

    ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        sample_print("Unable to get format.\n");
        return HI_NULL;
    }

    return sample_uvc_v4l2_format_name(fmt.fmt.pix_mp.pixelformat);
}

static hi_s32 sample_uvc_get_opt(hi_s32 argc, hi_char *argv[], device_info *dev, uvc_ctrl_info *ctrl_info)
{
    hi_s32 c;
    hi_char *endptr;

    while ((c = getopt_long(argc, argv, "f:F:s:h", g_opts, HI_NULL)) != -1) {
        switch (c) {
            case 'f':
                if (strcmp("help", optarg) == 0) {
                    sample_uvc_list_formats();
                    return HI_FAILURE;
                }
                ctrl_info->do_set_format = HI_TRUE;
                const format_info *info = sample_uvc_v4l2_format_by_name(optarg);
                if (info == HI_NULL) {
                    sample_print("Unsupported video format '%s'\n", optarg);
                    return HI_FAILURE;
                }
                ctrl_info->pixelformat = info->fourcc;
                break;
            case 'F':
                ctrl_info->do_file = HI_TRUE;
                if (optarg != HI_NULL) {
                    ctrl_info->pattern = optarg;
                }
                break;
            case 's':
                ctrl_info->do_capture = HI_TRUE;
                ctrl_info->do_set_format = HI_TRUE;
                ctrl_info->width = (hi_u32)strtol(optarg, &endptr, 10); /* 10:base */
                if (*endptr != 'x' || endptr == optarg) {
                    sample_print("Invalid size '%s'\n", optarg);
                    return HI_FAILURE;
                }
                ctrl_info->height = (hi_u32)strtol(endptr + 1, &endptr, 10);    /* 10:base */
                if (*endptr != 0) {
                    sample_print("Invalid size '%s'\n", optarg);
                    return HI_FAILURE;
                }
                break;
            case SAMPLE_UVC_OPT_ENUM_FORMATS:
                ctrl_info->do_enum_formats = HI_TRUE;
                break;
            case 'h':
            default:
                sample_uvc_usage(argv[0]);
                return HI_FAILURE;
        }
    }
    return HI_SUCCESS;
}

static hi_void sample_uvc_ctrl_info_init(uvc_ctrl_info *ctrl_info)
{
    ctrl_info->pause = (hi_u32)-1;
    ctrl_info->quality = (hi_u32)-1;
    ctrl_info->time_per_frame.numerator = 1;
    ctrl_info->time_per_frame.denominator = 25;     /* 25 frames per second */
    ctrl_info->pattern = "frame-#.bin";
    ctrl_info->nbufs = SAMPLE_UVC_V4L_BUFFERS_DEFAULT;
    ctrl_info->memtype = V4L2_MEMORY_MMAP;
    ctrl_info->nframes = (hi_u32)-1;
    ctrl_info->width = 640;     /* 640:width */
    ctrl_info->height = 480;    /* 480:height */
    ctrl_info->pixelformat = V4L2_PIX_FMT_YUYV;
    ctrl_info->fill = BUFFER_FILL_NONE;
    ctrl_info->rt_priority = 1;
}

static hi_s32 sample_uvc_prepare_to_set_ctrl(hi_s32 argc, hi_char *argv[], device_info *dev, uvc_ctrl_info *ctrl_info,
    hi_s32 *type)
{
    hi_s32 ret;
    hi_u32 capabilities = V4L2_CAP_VIDEO_CAPTURE;

    if (ctrl_info->pause != (hi_u32)(-1)) {
        ret = sample_uvc_pause_init();
        if (ret < 0) {
            return HI_FAILURE;
        }
    }

    if ((ctrl_info->fill & BUFFER_FILL_PADDING) && (ctrl_info->memtype != V4L2_MEMORY_USERPTR)) {
        sample_print("Buffer overrun can only be checked in USERPTR mode.\n");
        return HI_FAILURE;
    }

    if (!sample_uvc_video_has_fd(dev)) {
        if (optind >= argc) {
            sample_uvc_usage(argv[0]);
            return HI_FAILURE;
        }
        ret = sample_uvc_video_open(dev, argv[optind]);
        if (ret < 0) {
            return HI_FAILURE;
        }
    }

    if (!ctrl_info->no_query) {
        ret = sample_uvc_video_query_cap(dev, &capabilities);
        if (ret < 0) {
            return HI_FAILURE;
        }
    }

    *type = sample_uvc_cap_get_buf_type(capabilities);
    if (*type < 0) {
        return HI_FAILURE;
    }

    if (!ctrl_info->do_file) {
        ctrl_info->pattern = HI_NULL;
    }

    return HI_SUCCESS;
}

static hi_void sample_uvc_set_ctrl(device_info *dev, uvc_ctrl_info *ctrl_info, hi_s32 type)
{
    hi_s32 ret;

    if (!sample_uvc_video_has_valid_buf_type(dev)) {
        sample_uvc_video_set_buf_type(dev, type);
    }

    dev->memtype = ctrl_info->memtype;

    if (ctrl_info->do_log_status) {
        sample_uvc_video_log_status(dev);
    }

    if (ctrl_info->do_get_control) {
        struct v4l2_query_ext_ctrl query;

        ret = sample_uvc_query_ext_control(dev, ctrl_info->ctrl_name, &query);
        if (ret == 0) {
            sample_uvc_video_get_control(dev, &query, HI_FALSE);
        }
    }

    if (ctrl_info->do_set_control) {
        sample_uvc_video_set_control(dev, ctrl_info->ctrl_name, ctrl_info->ctrl_value);
    }

    if (ctrl_info->do_send_extension) {
        sample_uvc_video_send_extension(dev, ctrl_info->extension_name, ctrl_info->extension_channel);
    }

    if (ctrl_info->do_list_controls) {
        sample_uvc_video_list_controls(dev);
    }

    if (ctrl_info->do_reset_controls) {
        sample_uvc_video_reset_controls(dev);
    }
}

static hi_s32 sample_uvc_set_video_format(device_info *dev, uvc_ctrl_info *ctrl_info)
{
    hi_s32 ret;

    if (ctrl_info->do_enum_formats) {
        printf("- Available formats:\n");
        sample_uvc_video_enum_formats(dev, V4L2_BUF_TYPE_VIDEO_CAPTURE);
        sample_uvc_video_enum_formats(dev, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
        sample_uvc_video_enum_formats(dev, V4L2_BUF_TYPE_VIDEO_OUTPUT);
        sample_uvc_video_enum_formats(dev, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
        sample_uvc_video_enum_formats(dev, V4L2_BUF_TYPE_VIDEO_OVERLAY);
        sample_uvc_video_enum_formats(dev, V4L2_BUF_TYPE_META_CAPTURE);
    }

    if (ctrl_info->do_enum_inputs) {
        printf("- Available inputs:\n");
        sample_uvc_video_enum_inputs(dev);
    }

    if (ctrl_info->do_set_input) {
        sample_uvc_video_set_input(dev, ctrl_info->input);
        ret = sample_uvc_video_get_input(dev);
        printf("Input %d selected\n", ret);
    }

    if (ctrl_info->do_set_format) {
        if (sample_uvc_video_set_format(dev, ctrl_info) < 0) {
            sample_uvc_video_close(dev);
            return HI_FAILURE;
        }
    }

    if (!ctrl_info->no_query || ctrl_info->do_capture) {
        sample_uvc_video_get_format(dev);
    }

    if (ctrl_info->do_set_time_per_frame) {
        if (sample_uvc_video_set_frame_rate(dev, &ctrl_info->time_per_frame) < 0) {
            sample_uvc_video_close(dev);
            return HI_FAILURE;
        }
    }

    return HI_SUCCESS;
}

static hi_bool sample_uvc_is_close_video(device_info *dev, uvc_ctrl_info *ctrl_info)
{
    struct sched_param sched;
    hi_s32 ret;

    while (ctrl_info->do_sleep_forever) {
        sleep(1000);    /* 1000:time */
    }

    if (!ctrl_info->do_capture) {
        sample_uvc_video_close(dev);
        return HI_TRUE;
    }

    if (sample_uvc_video_set_quality(dev, ctrl_info->quality) < 0) {
        sample_uvc_video_close(dev);
        return HI_TRUE;
    }

    if (sample_uvc_video_prepare_capture(dev, ctrl_info->nbufs, ctrl_info->userptr_offset,
        ctrl_info->pattern, ctrl_info->fill)) {
        sample_uvc_video_close(dev);
        return HI_TRUE;
    }

    if (!ctrl_info->do_queue_late && sample_uvc_video_queue_all_buffers(dev, ctrl_info->fill)) {
        sample_uvc_video_close(dev);
        return HI_TRUE;
    }

    if (ctrl_info->do_rt) {
        (hi_void)memset_s(&sched, sizeof(sched), 0, sizeof(sched));
        sched.sched_priority = ctrl_info->rt_priority;
        ret = sched_setscheduler(0, SCHED_RR, &sched);
        if (ret < 0) {
            sample_print("Failed to select RR scheduler\n");
        }
    }

    return HI_FALSE;
}

hi_s32 main(hi_s32 argc, hi_char *argv[])
{
    device_info dev = {0};
    hi_s32 ret;
    hi_s32 type;
    uvc_ctrl_info ctrl_info = {0};

    struct sigaction sig_exit = {0};
    sig_exit.sa_handler = sample_uvc_exit_signal_handler;
    sigaction(SIGINT, &sig_exit, HI_NULL);
    sigaction(SIGTERM, &sig_exit, HI_NULL);

    sample_uvc_ctrl_info_init(&ctrl_info);

    sample_uvc_video_init(&dev);

    opterr = 0;

    ret = sample_uvc_get_opt(argc, argv, &dev, &ctrl_info);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }

    ret = sample_uvc_prepare_to_set_ctrl(argc, argv, &dev, &ctrl_info, &type);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    sample_uvc_set_ctrl(&dev, &ctrl_info, type);

    ret = sample_uvc_set_video_format(&dev, &ctrl_info);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    if (sample_uvc_is_close_video(&dev, &ctrl_info) == HI_TRUE) {
        return HI_FAILURE;
    }

    ret = sample_uvc_media_init(sample_uvc_video_get_data_type(&dev), ctrl_info.width, ctrl_info.height,
        ctrl_info.pattern);
    if (ret != HI_SUCCESS) {
        sample_print("media start failed!\n");
        goto video_close;
    }

    ctrl_info.type_name = sample_uvc_v4l2_format_name(ctrl_info.pixelformat);
    if (sample_uvc_video_do_capture(&dev, &ctrl_info) < 0) {
        ret = HI_FAILURE;
    }

    sample_uvc_media_stop_receive_data();
    sample_uvc_media_exit();
    printf("media exit...\n");

video_close:
    sample_uvc_video_close(&dev);
    sample_uvc_pause_cleanup();

    return ret;
}

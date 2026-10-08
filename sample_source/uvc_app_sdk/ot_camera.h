/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef OT_CAMERA_H
#define OT_CAMERA_H

typedef enum ot_stream_type_e {
    HI_FORMAT_H264 = 0x1,
    HI_FORMAT_MJPEG = 0x2,
    HI_FORMAT_MJPG = 0x2,
    HI_FORMAT_YUV = 0x3,
    HI_FORMAT_YUV420 = 0x3
} ot_stream_type_e;

typedef enum ot_stream_resolution_e {
    HI_RESOLUTION_1080 = 0x1,
    HI_RESOLUTION_720 = 0x2,
    HI_RESOLUTION_360 = 0x3
} ot_stream_resolution_e;

typedef struct encoder_property {
    unsigned int format;
    unsigned int width;
    unsigned int height;
    unsigned int fps;
    unsigned char compsite;
} encoder_property;

#endif

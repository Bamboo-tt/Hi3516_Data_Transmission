/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef __MEDIA_VDEC_H__
#define __MEDIA_VDEC_H__

#include "sample_comm.h"

hi_s32 sample_uvc_media_init(const hi_char *type_name, hi_u32 width, hi_u32 height, const hi_char *file_name);
hi_s32 sample_uvc_media_exit(hi_void);
hi_s32 sample_uvc_media_send_data(hi_void *data, hi_u32 size, hi_u32 stride,
    const hi_size *pic_size, const hi_char *type_name);
hi_s32 sample_uvc_media_stop_receive_data(hi_void);

#endif /* end of #ifndef __MEDIA_VDEC_H__ */

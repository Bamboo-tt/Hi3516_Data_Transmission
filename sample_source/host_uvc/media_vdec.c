/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include "media_vdec.h"
#include <unistd.h>


static hi_u32 g_input_width;
static hi_u32 g_input_height;
static FILE* g_save_file;


hi_s32 sample_uvc_media_init(const hi_char *type_name, hi_u32 width, hi_u32 height, const hi_char *file_name)
{
    if (type_name == HI_NULL || file_name == NULL) {
        printf("NULL type name or file name\n");
        return HI_FAILURE;
    }
    g_input_width = width;
    g_input_height = height;
    g_save_file = fopen(file_name, "w+");
    if (g_save_file == HI_NULL) {
        printf("create file failed, errno %d\n", errno);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

hi_s32 sample_uvc_media_exit(hi_void)
{
    if (g_save_file != HI_NULL) {
        fclose(g_save_file);
        g_save_file = HI_NULL;
    }
    return HI_SUCCESS;
}

hi_s32 sample_uvc_media_send_data(hi_void *data, hi_u32 size, hi_u32 stride,
    const hi_size *pic_size, const hi_char *type_name)
{
    size_t total = 0;
    size_t data_size = size;

    if (data == NULL || pic_size == NULL || type_name == NULL) {
        sample_print("data, pic_size or type_name is NULL\n");
        return HI_FAILURE;
    }

    if (g_save_file == HI_NULL) {
        return HI_FAILURE;
    }
    while (total < data_size) {
        size_t ret = fwrite(data, 1, data_size - total, g_save_file);
        if (ret > 0) {
            total += ret;
        } else {
            printf("write file return %u, failed errno %d\n", ret, errno);
            break;
        }
    }

    return HI_SUCCESS;
}

hi_s32 sample_uvc_media_stop_receive_data(hi_void)
{
    return HI_SUCCESS;
}

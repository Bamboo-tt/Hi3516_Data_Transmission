/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <errno.h>

#include "sample_comm.h"
#include "loadbmp.h"

#define SAMPLE_VGS_READ_PATH "./res/1920_1080_p420.yuv"
#define SAMPLE_VGS_SAVE_PATH "./"
#define FILE_NAME_LENGTH 128 /* max filename length is 128 */
#define MAX_LUMA_NUM 4
#define CORNER_RECT_NUM 5

typedef struct {
    hi_bool scale;
    hi_bool cover;
    hi_bool corner_rect_en;
    hi_bool draw_line;
    hi_bool rotate;
    hi_bool luma;
    hi_bool online_en;

    hi_vgs_scale_coef_mode *vgs_scl_coef_mode;
    hi_cover *vgs_add_cover;
    hi_corner_rect_attr *corner_rect_attr;
    hi_corner_rect *corner_rect;
    hi_u32 corner_rect_num;
    hi_vgs_line *vgs_draw_line;
    hi_rotation *rotation_angle;
    hi_u32 luma_num;
    hi_rect *luma_rect;
    hi_u64 *luma_data;
    hi_vgs_online *online_opt;

    sample_vb_base_info *in_img_vb_info;
    sample_vb_base_info *out_img_vb_info;
    hi_s32 sample_num;
} sample_vgs_func_param;

typedef struct {
    hi_vb_blk vb_handle;
    hi_void *virt_addr;
    hi_u32 vb_size;
    hi_bool vb_used;
} sample_vgs_vb_info;

typedef struct {
    hi_bool fill_en;
    hi_u32 fill_color;
    hi_size osd_size;
    hi_u32 stride;
    hi_pixel_format img_pixel_format;
} sample_vgs_osd_load_bmp_info;

sample_vgs_vb_info g_in_img_vb_info;
sample_vgs_vb_info g_out_img_vb_info;
static hi_u32 g_vgs_sample_signal_flag = 0;

/* function : show usage */
static hi_void sample_vgs_usage(hi_char *s_prg_nm)
{
    printf("\n/*****************************************/\n");
    printf("usage: %s <index>\n", s_prg_nm);
    printf("index:\n");
    printf("\t0) FILE -> VGS(scale) -> FILE.\n");
    printf("\t1) FILE -> VGS(cover+corner_rect) -> FILE.\n");
    printf("\t2) FILE -> VGS(draw_line) -> FILE.\n");
    printf("\t3) FILE -> VGS(rotate) -> FILE.\n");
    printf("\t4) FILE -> VGS(luma) -> FILE + data.\n");
    printf("/*****************************************/\n");
    return;
}

/* function : to process abnormal case */
static hi_void sample_vgs_handle_sig(hi_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_vgs_sample_signal_flag = 1;
    }
}

static hi_void sample_vgs_get_yuv_buffer_cfg(const sample_vb_base_info *vb_base_info, hi_vb_calc_cfg *vb_cal_config)
{
    hi_pic_buf_attr buf_attr;
    buf_attr.width = vb_base_info->width;
    buf_attr.height = vb_base_info->height;
    buf_attr.pixel_format = vb_base_info->pixel_format;
    buf_attr.bit_width = HI_DATA_BIT_WIDTH_8;
    buf_attr.compress_mode = vb_base_info->compress_mode;
    buf_attr.align = vb_base_info->align;
    buf_attr.video_format = vb_base_info->video_format;
    hi_common_get_pic_buf_cfg(&buf_attr, vb_cal_config);
    return;
}

static hi_void sample_vgs_set_frame_info(const hi_vb_calc_cfg *vb_cal_config, const sample_vb_base_info *vb_info,
    const sample_vgs_vb_info *vgs_vb_info, hi_phys_addr_t phys_addr, hi_video_frame_info *frame_info)
{
    frame_info->mod_id = HI_ID_VGS;
    frame_info->pool_id = hi_mpi_vb_handle_to_pool_id(vgs_vb_info->vb_handle);

    frame_info->video_frame.width       = vb_info->width;
    frame_info->video_frame.height      = vb_info->height;
    frame_info->video_frame.field        = HI_VIDEO_FIELD_FRAME;
    frame_info->video_frame.pixel_format  = vb_info->pixel_format;
    frame_info->video_frame.video_format  = vb_info->video_format;
    frame_info->video_frame.compress_mode = vb_info->compress_mode;
    frame_info->video_frame.dynamic_range = HI_DYNAMIC_RANGE_SDR8;
    frame_info->video_frame.color_gamut   = HI_COLOR_GAMUT_BT601;

    frame_info->video_frame.header_stride[0]  = vb_cal_config->head_stride;
    frame_info->video_frame.header_stride[1]  = vb_cal_config->head_stride;
    frame_info->video_frame.header_phys_addr[0] = phys_addr;
    frame_info->video_frame.header_phys_addr[1] = frame_info->video_frame.header_phys_addr[0] +
                                                  vb_cal_config->head_y_size;
    frame_info->video_frame.header_virt_addr[0] = vgs_vb_info->virt_addr;
    frame_info->video_frame.header_virt_addr[1] = frame_info->video_frame.header_virt_addr[0] +
                                                  vb_cal_config->head_y_size;

    frame_info->video_frame.stride[0]  = vb_cal_config->main_stride;
    frame_info->video_frame.stride[1]  = vb_cal_config->main_stride;
    frame_info->video_frame.phys_addr[0] = frame_info->video_frame.header_phys_addr[0] + vb_cal_config->head_size;
    frame_info->video_frame.phys_addr[1] = frame_info->video_frame.phys_addr[0] + vb_cal_config->main_y_size;
    frame_info->video_frame.virt_addr[0] = frame_info->video_frame.header_virt_addr[0] + vb_cal_config->head_size;
    frame_info->video_frame.virt_addr[1] = frame_info->video_frame.virt_addr[0] + vb_cal_config->main_y_size;

    return;
}

static hi_s32 sample_vgs_get_frame_vb(const sample_vb_base_info *vb_info, const hi_vb_calc_cfg *vb_cal_config,
    hi_video_frame_info *frame_info, sample_vgs_vb_info *vgs_vb_info)
{
    hi_phys_addr_t phys_addr;

    vgs_vb_info->vb_handle = hi_mpi_vb_get_blk(HI_VB_INVALID_POOL_ID, vb_cal_config->vb_size, HI_NULL);
    if (vgs_vb_info->vb_handle == HI_VB_INVALID_HANDLE) {
        sample_print("mpi_vb_get_block failed!\n");
        return HI_FAILURE;
    }
    vgs_vb_info->vb_used = HI_TRUE;

    phys_addr = hi_mpi_vb_handle_to_phys_addr(vgs_vb_info->vb_handle);
    if (phys_addr == 0) {
        sample_print("mpi_vb_handle2_phys_addr failed!\n");
        hi_mpi_vb_release_blk(vgs_vb_info->vb_handle);
        vgs_vb_info->vb_used = HI_FALSE;
        return HI_FAILURE;
    }

    vgs_vb_info->virt_addr = (hi_u8*)hi_mpi_sys_mmap(phys_addr, vb_cal_config->vb_size);
    if (vgs_vb_info->virt_addr == HI_NULL) {
        sample_print("mpi_sys_mmap failed!\n");
        hi_mpi_vb_release_blk(vgs_vb_info->vb_handle);
        vgs_vb_info->vb_used = HI_FALSE;
        return HI_FAILURE;
    }
    vgs_vb_info->vb_size = vb_cal_config->vb_size;

    sample_vgs_set_frame_info(vb_cal_config, vb_info, vgs_vb_info, phys_addr, frame_info);

    return HI_SUCCESS;
}

static hi_s32 sample_vgs_convert_chroma_planar_to_sp42x(FILE *file, hi_u8 *chroma_data,
    hi_u32 luma_stride, hi_u32 chroma_width, hi_u32 chroma_height)
{
    hi_u32 chroma_stride = luma_stride >> 1;
    hi_u8 *planar_buffer = HI_NULL;
    hi_u8 *dst = HI_NULL;
    hi_u32 row, list;

    planar_buffer = (hi_u8*)malloc(chroma_stride);
    if (planar_buffer == HI_NULL) {
        sample_print("vgs malloc failed!\n");
        return HI_FAILURE;
    }
    if (memset_s(planar_buffer, chroma_stride, 0, chroma_stride) != EOK) {
        sample_print("vgs memset_s failed!\n");
        free(planar_buffer);
        planar_buffer = HI_NULL;
        return HI_FAILURE;
    }

    /* U */
    dst = chroma_data + 1;
    for (row = 0; row < chroma_height; ++row) {
        /* sp420 U-component data starts 1/2 way from the beginning */
        (hi_void)fread(planar_buffer, chroma_width, 1, file);
        for (list = 0; list < chroma_stride; ++list) {
            *dst = *(planar_buffer + list);
            dst += 2; /* traverse 2 steps away to the next U-component data */
        }
        dst = chroma_data + 1;
        dst += (row + 1) * luma_stride;
    }

    /* V */
    dst = chroma_data;
    for (row = 0; row < chroma_height; ++row) {
        /* sp420 V-component data starts 1/2 way from the beginning */
        (hi_void)fread(planar_buffer, chroma_width, 1, file);
        for (list = 0; list < chroma_stride; ++list) {
            *dst = *(planar_buffer + list);
            dst += 2; /* traverse 2 steps away to the next V-component data */
        }
        dst = chroma_data;
        dst += (row + 1) * luma_stride;
    }

    free(planar_buffer);
    planar_buffer = HI_NULL;
    return HI_SUCCESS;
}

static hi_s32 sample_vgs_read_file_to_sp42_x(FILE *file, hi_video_frame *frame)
{
    hi_u8 *luma = (hi_u8*)(hi_uintptr_t)frame->virt_addr[0];
    hi_u8 *chroma = (hi_u8*)(hi_uintptr_t)frame->virt_addr[1];
    hi_u32 luma_width = frame->width;
    hi_u32 chroma_width = luma_width >> 1;
    hi_u32 luma_height = frame->height;
    hi_u32 chroma_height = luma_height;
    hi_u32 luma_stride = frame->stride[0];

    hi_u8 *dst = HI_NULL;
    hi_u32 row;

    if (frame->video_format == HI_VIDEO_FORMAT_LINEAR) {
        /* Y */
        dst = luma;
        for (row = 0; row < luma_height; ++row) {
            (hi_void)fread(dst, luma_width, 1, file);
            dst += luma_stride;
        }

        if (HI_PIXEL_FORMAT_YUV_400 == frame->pixel_format) {
            return HI_SUCCESS;
        } else if (HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420 == frame->pixel_format) {
            chroma_height = chroma_height >> 1;
        }
        if (sample_vgs_convert_chroma_planar_to_sp42x(
            file, chroma, luma_stride, chroma_width, chroma_height) != HI_SUCCESS) {
            return HI_FAILURE;
        }
    } else {
        /* Tile 64x16 size = stride x height * 3 / 2 */
        (hi_void)fread(luma, luma_stride * luma_height * 3 / 2, 1, file);
    }
    return HI_SUCCESS;
}

static hi_s32 sample_vgs_get_frame_from_file(sample_vgs_func_param *param, hi_vb_calc_cfg *in_img_vb_cal_config,
    hi_video_frame_info *frame_info)
{
    hi_s32 ret = HI_FAILURE;
    hi_char sz_in_file_name[FILE_NAME_LENGTH] = SAMPLE_VGS_READ_PATH;
    FILE *file_read = HI_NULL;

    file_read = fopen(sz_in_file_name, "rb");
    if (file_read == HI_NULL) {
        sample_print("can't open file %s\n", sz_in_file_name);
        goto EXIT;
    }

    ret = sample_vgs_get_frame_vb(param->in_img_vb_info, in_img_vb_cal_config, frame_info, &g_in_img_vb_info);
    if (ret != HI_SUCCESS) {
        goto EXIT1;
    }

    ret = sample_vgs_read_file_to_sp42_x(file_read, &frame_info->video_frame);
    if (ret != HI_SUCCESS) {
        goto EXIT2;
    } else {
        goto EXIT1;
    }

EXIT2:
    hi_mpi_sys_munmap(frame_info->video_frame.header_virt_addr[0], in_img_vb_cal_config->vb_size);
    hi_mpi_vb_release_blk(g_in_img_vb_info.vb_handle);
    g_in_img_vb_info.vb_used = HI_FALSE;
EXIT1:
    (hi_void)fclose(file_read);
EXIT:
    return ret;
}

static hi_s32 sample_vgs_convert_chroma_sp42x_to_planar(FILE *file, hi_u8 *chroma_data,
    hi_u32 luma_stride, hi_u32 chroma_width, hi_u32 chroma_height)
{
    hi_u8 *planar_buffer = HI_NULL;
    hi_u8 *dst = HI_NULL;
    hi_u32 row, list;

    planar_buffer = (hi_u8*)malloc(chroma_width);
    if (planar_buffer == HI_NULL) {
        sample_print("vgs malloc failed!.\n");
        return HI_FAILURE;
    }

    if (memset_s(planar_buffer, chroma_width, 0, chroma_width) != EOK) {
        sample_print("vgs memset_s failed!\n");
        free(planar_buffer);
        planar_buffer = HI_NULL;
        return HI_FAILURE;
    }

    /* U */
    for (row = 0; row < chroma_height; ++row) {
        dst = chroma_data + row * luma_stride + 1;
        for (list = 0; list < chroma_width; ++list) {
            *(planar_buffer + list) = *dst;
            dst += 2; /* traverse 2 steps away to the next U-component data */
        }
        (hi_void)fwrite(planar_buffer, 1, chroma_width, file);
    }

    /* V */
    for (row = 0; row < chroma_height; ++row) {
        dst = chroma_data + row * luma_stride;
        for (list = 0; list < chroma_width; ++list) {
            *(planar_buffer + list) = *dst;
            dst += 2; /* traverse 2 steps away to the next V-component data */
        }
        (hi_void)fwrite(planar_buffer, 1, chroma_width, file);
    }

    free(planar_buffer);
    planar_buffer = HI_NULL;
    return HI_SUCCESS;
}

static hi_s32 sample_vgs_save_sp42_x_to_planar(FILE *file, hi_video_frame *frame)
{
    hi_u32 luma_width = frame->width;
    hi_u32 chroma_width = luma_width >> 1;
    hi_u32 luma_height = frame->height;
    hi_u32 chroma_height = luma_height;
    hi_u32 luma_stride = frame->stride[0];
    hi_u8 *luma = (hi_u8*)(hi_uintptr_t)frame->virt_addr[0];
    hi_u8 *chroma = (hi_u8*)(hi_uintptr_t)frame->virt_addr[1];

    hi_u8 *dst = HI_NULL;
    hi_u32 row;

    /* Y */
    dst = luma;
    for (row = 0; row < luma_height; ++row) {
        (hi_void)fwrite(dst, 1, luma_width, file);
        dst += luma_stride;
    }

    if (HI_PIXEL_FORMAT_YUV_400 == frame->pixel_format) {
        return HI_SUCCESS;
    } else if (HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420 == frame->pixel_format) {
        chroma_height = chroma_height >> 1;
    }

    if (sample_vgs_convert_chroma_sp42x_to_planar(
        file, chroma, luma_stride, chroma_width, chroma_height) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_s32 sample_vgs_add_case2_task(const sample_vgs_func_param *param, hi_vgs_handle h_handle,
    hi_vgs_task_attr *vgs_task_attr)
{
    hi_s32 ret;
    if (param->cover) {
        ret = hi_mpi_vgs_add_cover_task(h_handle, vgs_task_attr, param->vgs_add_cover, 1);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_cover_task failed, ret:0x%x", ret);
            return ret;
        }
    }

    if (param->corner_rect_en) {
        ret = hi_mpi_vgs_add_corner_rect_task(h_handle, vgs_task_attr,
            param->corner_rect, param->corner_rect_attr, param->corner_rect_num);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_mosaic_task failed, ret:0x%x", ret);
            return ret;
        }
    }
    return HI_SUCCESS;
}

static hi_s32 sample_vgs_add_task(const sample_vgs_func_param *param, hi_vgs_handle h_handle,
    hi_vgs_task_attr *vgs_task_attr)
{
    hi_s32 ret;
    if (param->scale) {
        ret = hi_mpi_vgs_add_scale_task(h_handle, vgs_task_attr, *param->vgs_scl_coef_mode);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_scale_task failed, ret:0x%x", ret);
            return ret;
        }
    }
    ret = sample_vgs_add_case2_task(param, h_handle, vgs_task_attr);
    if (ret != HI_SUCCESS) {
        sample_print("sample_vgs_add_case2_task failed, ret:0x%x", ret);
        return ret;
    }

    if (param->draw_line) {
        ret = hi_mpi_vgs_add_draw_line_task(h_handle, vgs_task_attr, param->vgs_draw_line, 1);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_draw_line_task failed, ret:0x%x", ret);
            return ret;
        }
    }

    if (param->rotate) {
        ret = hi_mpi_vgs_add_rotation_task(h_handle, vgs_task_attr, *param->rotation_angle);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_rotation_task failed, ret:0x%x", ret);
            return ret;
        }
    }

    if (param->luma) {
        ret = hi_mpi_vgs_add_luma_task(h_handle, vgs_task_attr, param->luma_rect, param->luma_num, param->luma_data);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_draw_line_task failed, ret:0x%x", ret);
            return ret;
        }
    }

    if (param->online_en) {
        ret = hi_mpi_vgs_add_online_task(h_handle, vgs_task_attr, param->online_opt);
        if (ret != HI_SUCCESS) {
            sample_print("mpi_vgs_add_online_task failed, ret:0x%x", ret);
            return ret;
        }
    }
    return HI_SUCCESS;
}

static hi_s32 sample_vgs_get_in_out_frame(sample_vgs_func_param *param,
    hi_vgs_task_attr *vgs_task_attr, hi_bool same_vb)
{
    hi_vb_calc_cfg in_img_vb_cal_config = {0};
    hi_vb_calc_cfg out_img_vb_cal_config = {0};
    hi_s32 ret;

    sample_vgs_get_yuv_buffer_cfg(param->in_img_vb_info, &in_img_vb_cal_config);
    ret = sample_vgs_get_frame_from_file(param, &in_img_vb_cal_config, &vgs_task_attr->img_in);
    if (ret != HI_SUCCESS) {
        sample_print("sample_vgs_get_frame_from_file failed, ret:0x%x\n", ret);
        return HI_FAILURE;
    }

    if (same_vb) {
        if (memcpy_s(&vgs_task_attr->img_out, sizeof(hi_video_frame_info),
            &vgs_task_attr->img_in, sizeof(hi_video_frame_info)) != EOK) {
            sample_print("memcpy_s failed\n");
            return HI_FAILURE;
        }
    } else {
        sample_vgs_get_yuv_buffer_cfg(param->out_img_vb_info, &out_img_vb_cal_config);
        ret = sample_vgs_get_frame_vb(param->out_img_vb_info, &out_img_vb_cal_config,
            &vgs_task_attr->img_out, &g_out_img_vb_info);
        if (ret != HI_SUCCESS) {
            return ret;
        }
    }
    return HI_SUCCESS;
}

static hi_s32 sample_vgs_init_sys_and_vb(const sample_vgs_func_param *param,
    hi_bool same_vb, FILE **file_write)
{
    hi_vb_calc_cfg in_img_vb_cal_config = {0};
    hi_vb_calc_cfg out_img_vb_cal_config = {0};
    hi_char sz_out_file_name[FILE_NAME_LENGTH];
    hi_vb_cfg vb_conf = {0};
    hi_s32 ret;

    vb_conf.max_pool_cnt = same_vb ? 1 : 2; /* there are 2 pools in 1 vb */
    sample_vgs_get_yuv_buffer_cfg(param->in_img_vb_info, &in_img_vb_cal_config);
    vb_conf.common_pool[0].blk_size = in_img_vb_cal_config.vb_size;
    vb_conf.common_pool[0].blk_cnt = 2; /* there are 2 blks in 1 pool */

    if (!same_vb) {
        sample_vgs_get_yuv_buffer_cfg(param->out_img_vb_info, &out_img_vb_cal_config);
        vb_conf.common_pool[1].blk_size = out_img_vb_cal_config.vb_size;
        vb_conf.common_pool[1].blk_cnt = 2; /* there are 2 blks in 1 pool */
    }

    ret = sample_comm_sys_vb_init(&vb_conf);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_sys_vb_init failed, ret:0x%x\n", ret);
        return ret;
    }

    if (same_vb) {
        if (snprintf_s(sz_out_file_name, FILE_NAME_LENGTH, FILE_NAME_LENGTH - 1,
            "%s/vgs_sample%d_%ux%u_p420.yuv", SAMPLE_VGS_SAVE_PATH,
            param->sample_num, param->in_img_vb_info->width, param->in_img_vb_info->height) == -1) {
            sample_print("set output file name fail!\n");
            return HI_FAILURE;
        }
    } else {
        if (snprintf_s(sz_out_file_name, FILE_NAME_LENGTH, FILE_NAME_LENGTH - 1,
            "%s/vgs_sample%d_%ux%u_p420.yuv", SAMPLE_VGS_SAVE_PATH,
            param->sample_num, param->out_img_vb_info->width, param->out_img_vb_info->height) == -1) {
            sample_print("set output file name fail!\n");
            return HI_FAILURE;
        }
    }

    *file_write = fopen(sz_out_file_name, "w+");
    if (*file_write == HI_NULL) {
        sample_print("can't open file %s\n", sz_out_file_name);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_void sample_vgs_common_function_exit_release(const sample_vgs_func_param *param,
    hi_vgs_task_attr *vgs_task_attr, hi_bool same_vb, FILE **file_write)
{
    hi_vb_calc_cfg in_img_vb_cal_config = {0};
    hi_vb_calc_cfg out_img_vb_cal_config = {0};
    if (!same_vb && g_out_img_vb_info.vb_used == HI_TRUE) {
        sample_vgs_get_yuv_buffer_cfg(param->out_img_vb_info, &out_img_vb_cal_config);
        hi_mpi_sys_munmap(vgs_task_attr->img_out.video_frame.header_virt_addr[0], out_img_vb_cal_config.vb_size);
        hi_mpi_vb_release_blk(g_out_img_vb_info.vb_handle);
        g_out_img_vb_info.vb_used = HI_FALSE;
    }
    if (*file_write != HI_NULL) {
        (hi_void)fclose(*file_write);
        *file_write = HI_NULL;
    }
    if (g_in_img_vb_info.vb_used == HI_TRUE) {
        sample_vgs_get_yuv_buffer_cfg(param->in_img_vb_info, &in_img_vb_cal_config);
        hi_mpi_sys_munmap(vgs_task_attr->img_in.video_frame.header_virt_addr[0], in_img_vb_cal_config.vb_size);
        hi_mpi_vb_release_blk(g_in_img_vb_info.vb_handle);
        g_in_img_vb_info.vb_used = HI_FALSE;
    }
}

static hi_s32 sample_vgs_common_function(sample_vgs_func_param *param)
{
    hi_s32 ret;
    hi_bool same_vb = HI_FALSE;
    hi_vgs_handle h_handle = -1;
    hi_vgs_task_attr vgs_task_attr = {0};
    FILE *file_write = HI_NULL;

    if (param == HI_NULL || param->in_img_vb_info == HI_NULL) {
        return HI_FAILURE;
    }

    if (param->in_img_vb_info != HI_NULL && param->out_img_vb_info == HI_NULL) {
        same_vb = HI_TRUE;
    }

    /* step1: init SYS and common VB */
    ret = sample_vgs_init_sys_and_vb(param, same_vb, &file_write);
    if (ret != HI_SUCCESS) {
        goto exit_and_release;
    }

    /* step2: get frame */
    ret = sample_vgs_get_in_out_frame(param, &vgs_task_attr, same_vb);
    if (ret != HI_SUCCESS) {
        goto exit_and_release;
    }

    /* step3: create VGS job */
    ret = hi_mpi_vgs_begin_job(&h_handle);
    if (ret != HI_SUCCESS) {
        sample_print("mpi_vgs_begin_job failed, ret:0x%x", ret);
        goto exit_and_release;
    }

    /* step4: add VGS task */
    ret = sample_vgs_add_task(param, h_handle, &vgs_task_attr);
    if (ret != HI_SUCCESS) {
        hi_mpi_vgs_cancel_job(h_handle);
        goto exit_and_release;
    }

    /* step5: start VGS work */
    ret = hi_mpi_vgs_end_job(h_handle);
    if (ret != HI_SUCCESS) {
        hi_mpi_vgs_cancel_job(h_handle);
        sample_print("mpi_vgs_end_job failed, ret:0x%x", ret);
        goto exit_and_release;
    }

    /* step6: save the frame to file */
    ret = sample_vgs_save_sp42_x_to_planar(file_write, &vgs_task_attr.img_out.video_frame);
    if (ret != HI_SUCCESS) {
        goto exit_and_release;
    }
    (hi_void)fflush(file_write);

    /* step7: exit */
exit_and_release:
    sample_vgs_common_function_exit_release(param, &vgs_task_attr, same_vb, &file_write);
    sample_comm_sys_exit();
    return ret;
}

static hi_s32 sample_vgs_scale(hi_void)
{
    hi_s32 ret;
    sample_vgs_func_param vgs_func_param = {0};
    sample_vb_base_info in_img_vb_info;
    sample_vb_base_info out_img_vb_info;
    hi_vgs_scale_coef_mode vgs_scl_coef_mode = HI_VGS_SCALE_COEF_NORM;

    in_img_vb_info.video_format = HI_VIDEO_FORMAT_LINEAR;
    in_img_vb_info.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    in_img_vb_info.width = 1920; /* 1920:1080P width */
    in_img_vb_info.height = 1080; /* 1080:1080P height */
    in_img_vb_info.align = 8; /* 8: 8 align */
    in_img_vb_info.compress_mode = HI_COMPRESS_MODE_NONE;

    if (memcpy_s(&out_img_vb_info, sizeof(sample_vb_base_info), &in_img_vb_info, sizeof(sample_vb_base_info)) != EOK) {
        sample_print("memcpy_s failed\n");
        return HI_FAILURE;
    }
    out_img_vb_info.video_format = HI_VIDEO_FORMAT_LINEAR;
    out_img_vb_info.width = 1280; /* 1280:D1 width */
    out_img_vb_info.height = 720; /* 720:D1 height */

    vgs_func_param.scale = HI_TRUE;
    vgs_func_param.vgs_scl_coef_mode = &vgs_scl_coef_mode;
    vgs_func_param.in_img_vb_info = &in_img_vb_info;
    vgs_func_param.out_img_vb_info = &out_img_vb_info;
    vgs_func_param.sample_num = 0;

    ret = sample_vgs_common_function(&vgs_func_param);
    if (ret != HI_SUCCESS) {
        sample_print("VGS sample %d failed, ret:0x%x", vgs_func_param.sample_num, ret);
    }

    return ret;
}

static hi_void sample_vgs_case2_set_cover_and_corner_rect(hi_cover *vgs_add_cover,
    hi_corner_rect *corner_rect, hi_corner_rect_attr *corner_rect_attr, hi_u32 corner_rect_num)
{
    hi_u32 i;

    vgs_add_cover->type = HI_COVER_RECT;
    vgs_add_cover->color = 0x00FF00;
    vgs_add_cover->rect_attr.rect.x = 200; /* cover x-component is 200 */
    vgs_add_cover->rect_attr.rect.y = 300; /* cover y-component is 300 */
    vgs_add_cover->rect_attr.rect.width = 400; /* cover's size is 400x300 */
    vgs_add_cover->rect_attr.rect.height = 300; /* cover's size is 400x300 */
    vgs_add_cover->rect_attr.is_solid = HI_TRUE;

    for (i = 0; i < corner_rect_num; i++) {
        corner_rect[i].thick = 8; /* 8 corner rect thick */
        corner_rect[i].hor_len = 20; /* 20 corner rect horizontal length */
        corner_rect[i].ver_len = 20; /* 20 corner rect vertical length */
    }
    corner_rect[0].rect.x = -150; /* -150 corner_rect0 x position */
    corner_rect[0].rect.y = -100; /* 100 corner_rect0 y position */
    corner_rect[0].rect.width = 300; /* 300 corner_rect0 width */
    corner_rect[0].rect.height = 300; /* 300 corner_rect0 height */

    corner_rect[1].rect.x = 1800; /* 1800 corner_rect1 x position */
    corner_rect[1].rect.y = -100; /* -100 corner_rect1 y position */
    corner_rect[1].rect.width = 300; /* 300 corner_rect1 width */
    corner_rect[1].rect.height = 300; /* 300 corner_rect1 height */

    corner_rect[2].rect.x = -150; /* -150 corner_rect2 x position */
    corner_rect[2].rect.y = 900; /* 900 corner_rect2 y position */
    corner_rect[2].rect.width = 300; /* 300 corner_rect2 width */
    corner_rect[2].rect.height = 300; /* 300 corner_rect2 height */

    corner_rect[3].rect.x = 1800; /* 1800 corner_rect3 x position */
    corner_rect[3].rect.y = 900; /* 900 corner_rect3 y position */
    corner_rect[3].rect.width = 300; /* 300 corner_rect3 width */
    corner_rect[3].rect.height = 300; /* 300 corner_rect3 height */

    corner_rect[4].rect.x = 1200; /* 1200 corner_rect4 x position */
    corner_rect[4].rect.y = 100; /* 100 corner_rect4 y position */
    corner_rect[4].rect.width = 300; /* 300 corner_rect4 width */
    corner_rect[4].rect.height = 200; /* 200 corner_rect4 height */

    corner_rect_attr->color = 0xFF66FF;
    corner_rect_attr->corner_rect_type = HI_CORNER_RECT_TYPE_CORNER;
}

static hi_s32 sample_vgs_cover_and_coner_rect(hi_void)
{
    hi_s32 ret;
    sample_vgs_func_param vgs_func_param = {0};
    sample_vb_base_info in_img_vb_info;
    hi_cover vgs_add_cover;
    hi_corner_rect corner_rect[CORNER_RECT_NUM];
    hi_corner_rect_attr corner_rect_attr;

    in_img_vb_info.video_format = HI_VIDEO_FORMAT_LINEAR;
    in_img_vb_info.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    in_img_vb_info.width = 1920; /* 1920:1080P width */
    in_img_vb_info.height = 1080; /* 1080:1080P height */
    in_img_vb_info.align = 0;
    in_img_vb_info.compress_mode = HI_COMPRESS_MODE_NONE;

    vgs_func_param.corner_rect_num = CORNER_RECT_NUM;
    sample_vgs_case2_set_cover_and_corner_rect(&vgs_add_cover,
        corner_rect, &corner_rect_attr, vgs_func_param.corner_rect_num);

    vgs_func_param.cover = HI_TRUE;
    vgs_func_param.vgs_add_cover = &vgs_add_cover;

    vgs_func_param.corner_rect_en = HI_TRUE;
    vgs_func_param.corner_rect = corner_rect;
    vgs_func_param.corner_rect_attr = &corner_rect_attr;

    vgs_func_param.in_img_vb_info = &in_img_vb_info;
    vgs_func_param.out_img_vb_info = HI_NULL;
    vgs_func_param.sample_num = 1;

    ret = sample_vgs_common_function(&vgs_func_param);
    if (ret != HI_SUCCESS) {
        sample_print("VGS sample %d failed, ret:0x%x", vgs_func_param.sample_num, ret);
    }

    return ret;
}

static hi_s32 sample_vgs_draw_line(hi_void)
{
    hi_s32 ret;
    sample_vgs_func_param vgs_func_param = {0};
    sample_vb_base_info in_img_vb_info;
    hi_vgs_line vgs_draw_line;

    in_img_vb_info.video_format = HI_VIDEO_FORMAT_LINEAR;
    in_img_vb_info.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    in_img_vb_info.width = 1920; /* 1920:1080P width */
    in_img_vb_info.height = 1080; /* 1080:1080P height */
    in_img_vb_info.align = 0;
    in_img_vb_info.compress_mode = HI_COMPRESS_MODE_NONE;

    vgs_draw_line.start_point.x = 50; /* line starts from 50,50 */
    vgs_draw_line.start_point.y = 50; /* line starts from 50,50 */
    vgs_draw_line.end_point.x = 1600; /* line ends at 1600,900 */
    vgs_draw_line.end_point.y = 900; /* line ends at 1600,900 */
    vgs_draw_line.thick = 2; /* line width 2 */
    vgs_draw_line.color = 0x0000FF;

    vgs_func_param.draw_line = HI_TRUE;
    vgs_func_param.vgs_draw_line = &vgs_draw_line;
    vgs_func_param.in_img_vb_info = &in_img_vb_info;
    vgs_func_param.out_img_vb_info = HI_NULL;
    vgs_func_param.sample_num = 2; /* line width 2 */

    ret = sample_vgs_common_function(&vgs_func_param);
    if (ret != HI_SUCCESS) {
        sample_print("VGS sample %d failed, ret:0x%x", vgs_func_param.sample_num, ret);
    }

    return ret;
}

static hi_s32 sample_vgs_luma(hi_void)
{
    hi_s32 ret;
    sample_vgs_func_param vgs_func_param = {0};
    sample_vb_base_info in_img_vb_info;
    hi_s32 i;
    hi_rect luma_rect[MAX_LUMA_NUM];
    hi_u64 luma_data[MAX_LUMA_NUM];
    in_img_vb_info.video_format = HI_VIDEO_FORMAT_LINEAR;
    in_img_vb_info.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    in_img_vb_info.width = 1920; /* 1920:1080P width */
    in_img_vb_info.height = 1080; /* 1080:1080P height */
    in_img_vb_info.align = 0;
    in_img_vb_info.compress_mode = HI_COMPRESS_MODE_NONE;

    for (i = 0; i < MAX_LUMA_NUM; i++) {
        luma_rect[i].x = 100 + 50 * i; /* luma x-component starts from 100 at a step of 50 */
        luma_rect[i].y = 100 + 50 * i; /* luma y-component starts from 100 at a step of 50 */
        luma_rect[i].width = 40; /* 40x40 luma region */
        luma_rect[i].height = 40; /* 40x40 luma region */
    }
    if (memset_s(luma_data, sizeof(hi_u64) * MAX_LUMA_NUM, 0, sizeof(hi_u64) * MAX_LUMA_NUM) != EOK) {
        sample_print("vgs memset_s failed!\n");
        return HI_FAILURE;
    };
    vgs_func_param.luma = HI_TRUE;
    vgs_func_param.luma_rect = luma_rect;
    vgs_func_param.luma_data = luma_data;
    vgs_func_param.luma_num = MAX_LUMA_NUM;
    vgs_func_param.in_img_vb_info = &in_img_vb_info;
    vgs_func_param.out_img_vb_info = HI_NULL;
    vgs_func_param.sample_num = 4; /* 4 is parameter */

    ret = sample_vgs_common_function(&vgs_func_param);
    if (ret != HI_SUCCESS) {
        sample_print("VGS sample %d failed, ret:0x%x", vgs_func_param.sample_num, ret);
    }
    for (i = 0; i < MAX_LUMA_NUM; i++) {
        sample_print("VGS sample luma_rect: %d,luma_data:%llu\n", i, luma_data[i]);
    }

    return ret;
}

static hi_s32 sample_vgs_rotate(hi_void)
{
    hi_s32 ret;
    sample_vgs_func_param vgs_func_param = {0};
    hi_rotation rotation_angle = HI_ROTATION_90;
    sample_vb_base_info in_img_vb_info;
    sample_vb_base_info out_img_vb_info;

    in_img_vb_info.video_format = HI_VIDEO_FORMAT_LINEAR;
    in_img_vb_info.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    in_img_vb_info.width = 1920; /* 1920:1080P width */
    in_img_vb_info.height = 1080; /* 1080:1080P height */
    in_img_vb_info.align = 0;
    in_img_vb_info.compress_mode = HI_COMPRESS_MODE_NONE;

    if (memcpy_s(&out_img_vb_info, sizeof(sample_vb_base_info), &in_img_vb_info, sizeof(sample_vb_base_info)) != EOK) {
        sample_print("memcpy_s failed\n");
        return HI_FAILURE;
    };
    out_img_vb_info.width = 1080; /* 1080:1080P height */
    out_img_vb_info.height = 1920; /* 1920:1080P width */

    vgs_func_param.rotate = HI_TRUE;
    vgs_func_param.rotation_angle = &rotation_angle;
    vgs_func_param.in_img_vb_info = &in_img_vb_info;
    vgs_func_param.out_img_vb_info = &out_img_vb_info;
    vgs_func_param.sample_num = 3; /* 3 rotation task */

    ret = sample_vgs_common_function(&vgs_func_param);
    if (ret != HI_SUCCESS) {
        sample_print("VGS sample %d failed, ret:0x%x", vgs_func_param.sample_num, ret);
    }

    return ret;
}

static hi_s32 sample_vgs_proc_task(hi_s32 task_index)
{
    hi_s32 ret;
    switch (task_index) {
        case 0: /* 0 scale sample */
            ret = sample_vgs_scale();
            break;
        case 1: /* 1 cover and coner_rect sample */
            ret = sample_vgs_cover_and_coner_rect();
            break;
        case 2: /* 2 draw line sample */
            ret = sample_vgs_draw_line();
            break;
        case 3: /* 3 rotate sample */
            ret = sample_vgs_rotate();
            break;
        case 4: /* 4 luma sample */
            ret = sample_vgs_luma();
            break;
        default:
            ret = HI_FAILURE;
            break;
    }
    return ret;
}

/*
 * function : main()
 * description : video vgs sample
 */
#ifdef __LITEOS__
hi_s32 app_main(hi_s32 argc, hi_char *argv[])
#else
hi_s32 main(hi_s32 argc, hi_char *argv[])
#endif
{
    hi_s32 ret = HI_FAILURE;
    hi_u32 index;
    hi_char *end_ptr = HI_NULL;

    if (argc != 2) { /* 2:arv num */
        sample_vgs_usage(argv[0]);
        return ret;
    }

    if (!strncmp(argv[1], "-h", 2)) { /* 2:arv len */
        sample_vgs_usage(argv[0]);
        return HI_SUCCESS;
    }
    if ((strlen(argv[1]) > 1) ||
        (strncmp(argv[1], "0", 1) && strncmp(argv[1], "1", 1) && strncmp(argv[1], "2", 1) &&
        strncmp(argv[1], "3", 1) && strncmp(argv[1], "4", 1))) {
        sample_print("the index is invalid!\n");
        sample_vgs_usage(argv[0]);
        return HI_FAILURE;
    }

    g_vgs_sample_signal_flag = 0;

#ifndef __LITEOS__
    sample_sys_signal(&sample_vgs_handle_sig);
#endif

    index = (hi_u32)strtol(argv[1], &end_ptr, 10); /* base 10 */
    if ((end_ptr == argv[1]) || (*end_ptr) != '\0' || errno == ERANGE) {
        printf("input index error, use default 0!\n");
        index = 0;
    }
    ret = sample_vgs_proc_task(index);

    if (g_vgs_sample_signal_flag == 1) {
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
        exit(-1);
    }

    if (ret == HI_SUCCESS) {
        sample_print("vgs sample %u exit normally!\n", index);
    } else {
        sample_print("vgs sample %u exit abnormally!\n", index);
    }
    return ret;
}

/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "sample_comm.h"
#include "sample_ipc.h"
#include "securec.h"
#include "hi_mpi_ae.h"

#define X_ALIGN 16
#define Y_ALIGN 2

#define VENC_MAX_CHN_NUM 8

static volatile sig_atomic_t g_sig_flag = 0;

/* this configuration is used to adjust the size and number of buffer(VB).  */
static sample_vb_param g_vb_param = {
    /* raw, yuv, vpss chn1 */
    .vb_size = {{2688, 1520}, {2688, 1520}, {720, 480}},
    .pixel_format = {HI_PIXEL_FORMAT_RGB_BAYER_12BPP, HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420,
        HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420},
    .compress_mode = {HI_COMPRESS_MODE_LINE, HI_COMPRESS_MODE_NONE,
        HI_COMPRESS_MODE_NONE},
    .video_format = {HI_VIDEO_FORMAT_LINEAR, HI_VIDEO_FORMAT_LINEAR,
        HI_VIDEO_FORMAT_LINEAR},
    .blk_num = {4, 7, 3}
};

static sampe_sys_cfg g_vie_sys_cfg = {
    .route_num = 1,
    .mode_type = HI_VI_OFFLINE_VPSS_OFFLINE,
    .vpss_wrap_en = HI_TRUE,
    .vpss_wrap_size = 3200 * 128 *1.5,
};

static sample_comm_venc_chn_param g_venc_chn_param = {
    .frame_rate           = 30, /* 30 is a number */
    .stats_time           = 2,  /* 2 is a number */
    .gop                  = 60, /* 60 is a number */
    .venc_size            = {1920, 1080},
    .size                 = -1,
    .profile              = 0,
    .is_rcn_ref_share_buf = HI_FALSE,
    .gop_attr             = {
        .gop_mode = HI_VENC_GOP_MODE_NORMAL_P,
        .normal_p = {2},
    },
    .type                 = HI_PT_H265,
    .rc_mode              = SAMPLE_RC_CBR,
};

static sample_vi_fpn_calibration_cfg g_calibration_cfg = {
    .threshold     = 4095, /* 4095 is a number */
    .frame_num     = 16,   /* 16 is a number */
    .fpn_type      = HI_ISP_FPN_TYPE_FRAME,
    .pixel_format  = HI_PIXEL_FORMAT_RGB_BAYER_16BPP,
    .compress_mode = HI_COMPRESS_MODE_NONE,
};

static sample_vi_fpn_correction_cfg g_correction_cfg = {
    .op_mode       = HI_OP_MODE_AUTO,
    .fpn_type      = HI_ISP_FPN_TYPE_FRAME,
    .strength      = 0,
    .pixel_format  = HI_PIXEL_FORMAT_RGB_BAYER_16BPP,
    .compress_mode = HI_COMPRESS_MODE_NONE,
};

static hi_void sample_get_char(hi_void)
{
    if (g_sig_flag == 1) {
        return;
    }

    sample_pause();
}

/* define SAMPLE_MEM_SHARE_ENABLE, when use tools to dump YUV/RAW. */
#ifdef SAMPLE_MEM_SHARE_ENABLE
hi_void sample_vie_init_mem_share(hi_void)
{
    hi_u32 i;
    hi_vb_common_pools_id pools_id = {0};

    if (hi_mpi_vb_get_common_pool_id(&pools_id) != HI_SUCCESS) {
        sample_print("get common pool_id failed!\n");
        return;
    }
    for (i = 0; i < pools_id.pool_cnt; ++i) {
        hi_mpi_vb_pool_share_all(pools_id.pool[i]);
    }
}
#endif

/* default sc4336p x 2 */
static hi_vi_wrap_in_param g_vi_wrap_param = {
    .is_slave = HI_FALSE,
    .pipe_num = 2,
    .wrap_param[0] = {
        .pipe_size = {2560, 1440},
        .frame_rate_100x = 3000,
        .full_lines_std = 1500,
        .slave_param = {
            .vi_vpss_online = HI_TRUE,
            .vpss_chn0_size = {2560, 1440},
            .vpss_venc_wrap = HI_TRUE,
            .large_stream_size = {2560, 1440},
            .small_stream_size = {720, 480},
        }
    },
    .wrap_param[1] = {
        .pipe_size = {2560, 1440},
        .frame_rate_100x = 3000,
        .full_lines_std = 1500,
        .slave_param.vi_vpss_online = HI_TRUE,
        .slave_param.vpss_chn0_size = {2560, 1440},
        .slave_param.vpss_venc_wrap = HI_TRUE,
        .slave_param.large_stream_size = {2560, 1440},
        .slave_param.small_stream_size = {720, 480},
    }
};

static td_void sample_vie_get_pipe_wrap_line(sample_vi_cfg vi_cfg[], td_s32 route_num)
{
    hi_vi_wrap_out_param out_param;
    hi_vi_wrap_in_param in_param;
    hi_s32 i;

    if (g_vie_sys_cfg.mode_type == HI_VI_ONLINE_VPSS_ONLINE || g_vie_sys_cfg.mode_type == HI_VI_ONLINE_VPSS_OFFLINE) {
        return;
    }

    (hi_void)memcpy_s(&in_param, sizeof(hi_vi_wrap_in_param), &g_vi_wrap_param, sizeof(hi_vi_wrap_in_param));
    if (g_vie_sys_cfg.mode_type == HI_VI_OFFLINE_VPSS_OFFLINE) {
        in_param.wrap_param[0].slave_param.vi_vpss_online = HI_FALSE;
        in_param.wrap_param[1].slave_param.vi_vpss_online = HI_FALSE;
    }
    in_param.pipe_num = route_num;
#ifdef OT_FPGA
    in_param.wrap_param[0].frame_rate_100x = 1500; /* 1500: 15fps*100 */
    in_param.wrap_param[1].frame_rate_100x = 1500; /* 1500: 15fps*100 */
    in_param.wrap_param[0].slave_param.vpss_venc_wrap = HI_FALSE;
    in_param.wrap_param[1].slave_param.vpss_venc_wrap = HI_FALSE;
#endif
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_param.wrap_param[0].pipe_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_param.wrap_param[0].slave_param.vpss_chn0_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &in_param.wrap_param[0].slave_param.large_stream_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR1_TYPE, &in_param.wrap_param[1].pipe_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR1_TYPE, &in_param.wrap_param[1].slave_param.vpss_chn0_size);
    sample_comm_vi_get_size_by_sns_type(SENSOR1_TYPE, &in_param.wrap_param[1].slave_param.large_stream_size);
    in_param.wrap_param[0].full_lines_std = in_param.wrap_param[0].pipe_size.height;
    in_param.wrap_param[1].full_lines_std = in_param.wrap_param[1].pipe_size.height;
    if (hi_mpi_sys_get_vi_wrap_buffer_line(&in_param, &out_param) != HI_SUCCESS) {
        sample_print("sample_vie get wrap line fail, pipe_wrap not enable!\n");
        return;
    }

    for (i = 0; i < route_num; i++) {
        vi_cfg[i].pipe_info[0].wrap_attr.buf_line = out_param.buf_line[i];
        vi_cfg[i].pipe_info[0].wrap_attr.enable = HI_TRUE;
    }
}

static td_void sample_vie_vpss_get_default_wrap_buf_size(sample_sns_type sns_type, sampe_sys_cfg *g_vie_sys_cfg)
{
    hi_size size;
    hi_pic_buf_attr buf_attr = { 0 };
    sample_comm_vi_get_size_by_sns_type(sns_type, &size);

    if (g_vie_sys_cfg->vpss_wrap_en == HI_FALSE) {
        return;
    }

    buf_attr.width = size.width;
    buf_attr.height = size.height;
    buf_attr.bit_width = HI_DATA_BIT_WIDTH_8;
    buf_attr.pixel_format = HI_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    buf_attr.compress_mode = HI_COMPRESS_MODE_SEG_COMPACT;
    buf_attr.align = HI_DEFAULT_ALIGN;
    buf_attr.video_format = HI_VIDEO_FORMAT_LINEAR;
    g_vie_sys_cfg->vpss_wrap_size = hi_comm_get_vpss_venc_wrap_buf_size(&buf_attr, 128); // 128: default buf_line
}

static hi_s32 sample_vie_sys_init(hi_void)
{
    hi_vb_cfg vb_cfg;
    hi_u32 supplement_config = HI_VB_SUPPLEMENT_BNR_MOT_MASK | HI_VB_SUPPLEMENT_MOTION_DATA_MASK;

    sample_comm_sys_get_default_vb_cfg(&g_vb_param, &vb_cfg);
    /* prepare vpss wrap vb */
    if (g_vie_sys_cfg.vpss_wrap_en) {
        vb_cfg.common_pool[SAMPLE_VIE_POOL_NUM].blk_cnt = 1;
        vb_cfg.common_pool[SAMPLE_VIE_POOL_NUM].blk_size = g_vie_sys_cfg.vpss_wrap_size;
    }
    if (sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config) != HI_SUCCESS) {
        return HI_FAILURE;
    }

#ifdef SAMPLE_MEM_SHARE_ENABLE
    sample_vie_init_mem_share();
#endif

    if (sample_comm_vi_set_vi_vpss_mode(g_vie_sys_cfg.mode_type, HI_VI_AIISP_MODE_DEFAULT) != HI_SUCCESS) {
        goto sys_exit;
    }

    return HI_SUCCESS;
sys_exit:
    sample_comm_sys_exit();
    return HI_FAILURE;
}

static hi_s32 sample_vie_vpss_set_wrap_grp_int_attr(hi_vi_vpss_mode_type mode_type, hi_bool wrap_en, hi_u32 max_height)
{
    hi_s32 ret;
    if (mode_type == HI_VI_ONLINE_VPSS_ONLINE && wrap_en) {
        hi_frame_interrupt_attr frame_interrupt_attr;
        frame_interrupt_attr.interrupt_type = HI_FRAME_INTERRUPT_EARLY_END;
        frame_interrupt_attr.early_line = max_height / 2; /* 2 half */
        ret = hi_mpi_vpss_set_grp_frame_interrupt_attr(0, &frame_interrupt_attr);
        if (ret != HI_SUCCESS) {
            printf("hi_mpi_vpss_set_grp_frame_interrupt_attr failed!\n");
            return ret;
        }
    }
    return HI_SUCCESS;
}

static hi_s32 sample_vie_start_vpss(hi_vpss_grp grp, sample_vpss_cfg *vpss_cfg)
{
    hi_s32 ret;
    sample_vpss_chn_attr vpss_chn_attr = {0};

    if (grp == 0) {
        ret = sample_vie_vpss_set_wrap_grp_int_attr(g_vie_sys_cfg.mode_type, vpss_cfg->wrap_attr[0].enable,
            vpss_cfg->grp_attr.max_height);
        if (ret != HI_SUCCESS) {
            return ret;
        }
    }

    (hi_void)memcpy_s(&vpss_chn_attr.chn_attr[0], sizeof(hi_vpss_chn_attr) * HI_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->chn_attr, sizeof(hi_vpss_chn_attr) * HI_VPSS_MAX_PHYS_CHN_NUM);
    (hi_void)memcpy_s(vpss_chn_attr.chn_enable, sizeof(vpss_chn_attr.chn_enable),
        vpss_cfg->chn_en, sizeof(vpss_chn_attr.chn_enable));
    (hi_void)memcpy_s(&vpss_chn_attr.wrap_attr[0], sizeof(hi_vpss_chn_buf_wrap_attr) * HI_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->wrap_attr, sizeof(hi_vpss_chn_buf_wrap_attr) * HI_VPSS_MAX_PHYS_CHN_NUM);
    vpss_chn_attr.chn_array_size = HI_VPSS_MAX_PHYS_CHN_NUM;
    ret = sample_common_vpss_start(grp, &vpss_cfg->grp_attr, &vpss_chn_attr);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    if (vpss_cfg->chn_en[1]) {
        const hi_low_delay_info low_delay_info = { HI_TRUE, 200, HI_FALSE }; /* 200: lowdelay line */
        if (hi_mpi_vpss_set_chn_low_delay(grp, 1, &low_delay_info) != HI_SUCCESS) {
            goto stop_vpss;
        }
    }

    return HI_SUCCESS;
stop_vpss:
    sample_common_vpss_stop(grp, vpss_cfg->chn_en, HI_VPSS_MAX_PHYS_CHN_NUM);
    return HI_FAILURE;
}

static hi_void sample_vie_stop_vpss(hi_vpss_grp grp)
{
    hi_bool chn_enable[HI_VPSS_MAX_PHYS_CHN_NUM] = {HI_TRUE, HI_TRUE, HI_FALSE};

    sample_common_vpss_stop(grp, chn_enable, HI_VPSS_MAX_PHYS_CHN_NUM);
}

static hi_s32 sample_vie_start_venc(hi_venc_chn venc_chn[], hi_size venc_size[], size_t size, hi_u32 chn_num)
{
    hi_s32 i;
    hi_s32 ret;

    if (chn_num > size) {
        return HI_FAILURE;
    }

    for (i = 0; i < (hi_s32)chn_num; i++) {
        g_venc_chn_param.venc_size.width = venc_size[i].width;
        g_venc_chn_param.venc_size.height = venc_size[i].height;
        g_venc_chn_param.is_rcn_ref_share_buf = 1;
        ret = sample_comm_venc_start(venc_chn[i], &g_venc_chn_param);
        if (ret != HI_SUCCESS) {
            goto exit;
        }
    }

    ret = sample_comm_venc_start_get_stream(venc_chn, chn_num);
    if (ret != HI_SUCCESS) {
        goto exit;
    }

    return HI_SUCCESS;

exit:
    for (i = i - 1; i >= 0; i--) {
        sample_comm_venc_stop(venc_chn[i]);
    }
    return HI_FAILURE;
}

static hi_void sample_vie_stop_venc(hi_venc_chn venc_chn[], size_t size, hi_u32 chn_num)
{
    hi_u32 i;

    if (chn_num > size) {
        return;
    }

    sample_comm_venc_stop_get_stream(chn_num);

    for (i = 0; i < chn_num; i++) {
        sample_comm_venc_stop(venc_chn[i]);
    }
}

static hi_s32 sample_vie_start_and_bind_venc(hi_vpss_grp vpss_grp[], sample_vpss_cfg *vpss_cfg, size_t size,
    hi_u32 grp_num)
{
    hi_u32 i;
    hi_s32 ret;
    hi_venc_chn venc_chn[VENC_MAX_CHN_NUM];
    hi_size venc_size[VENC_MAX_CHN_NUM];
    hi_u32 chn_num;

    if (grp_num > size) {
        return HI_FAILURE;
    }

    for (i = 0; i < VENC_MAX_CHN_NUM; i++) {
        venc_chn[i] = i;
        if (i % 2 == 0) { /* 2: divisor */
            venc_size[i].width = vpss_cfg->chn_attr[0].width;
            venc_size[i].height = vpss_cfg->chn_attr[0].height;
        } else {
            venc_size[i].width = vpss_cfg->chn_attr[1].width;
            venc_size[i].height = vpss_cfg->chn_attr[1].height;
        }
    }

    chn_num = grp_num;
    if (g_vie_sys_cfg.vpss_wrap_en) {
        chn_num *= 2; /* 2 chns */
    }
    ret = sample_vie_start_venc(venc_chn, venc_size, sizeof(venc_chn) / sizeof(venc_chn[0]), chn_num);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }
    for (i = 0; i < grp_num; i++) {
        sample_comm_vpss_bind_venc(vpss_grp[i], HI_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        if (g_vie_sys_cfg.vpss_wrap_en) {
            sample_comm_vpss_bind_venc(vpss_grp[i], HI_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
        }
    }

    return HI_SUCCESS;
}

static hi_void sample_vie_stop_and_unbind_venc(hi_vpss_grp vpss_grp[], size_t size, hi_u32 grp_num)
{
    hi_u32 i;
    hi_venc_chn venc_chn[VENC_MAX_CHN_NUM];
    hi_u32 chn_num;

    if (grp_num > size) {
        return;
    }

    for (i = 0; i < VENC_MAX_CHN_NUM; i++) {
        venc_chn[i] = i;
    }

    for (i = 0; i < grp_num; i++) {
        sample_comm_vpss_un_bind_venc(vpss_grp[i], HI_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        if (g_vie_sys_cfg.vpss_wrap_en) {
            sample_comm_vpss_un_bind_venc(vpss_grp[i], HI_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
        }
    }

    chn_num = grp_num;
    if (g_vie_sys_cfg.vpss_wrap_en) {
        chn_num *= 2; /* 2 chns */
    }
    sample_vie_stop_venc(venc_chn, sizeof(venc_chn) / sizeof(venc_chn[0]), chn_num);
}

static hi_s32 sample_vie_start_route(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg, hi_s32 route_num)
{
    hi_s32 i, j, ret;
    hi_vpss_grp vpss_grp[SAMPLE_VIE_MAX_ROUTE_NUM] = {0, 1, 2, 3};

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[1]);
    if (sample_vie_sys_init() != HI_SUCCESS) {
        return HI_FAILURE;
    }

    for (i = 0; i < route_num; i++) {
        ret = sample_comm_vi_start_vi(&vi_cfg[i]);
        if (ret != HI_SUCCESS) {
            goto start_vi_failed;
        }
    }

    for (i = 0; i < route_num; i++) {
        sample_comm_vi_bind_vpss(i, 0, vpss_grp[i], 0);
    }

    for (i = 0; i < route_num; i++) {
        ret = sample_vie_start_vpss(vpss_grp[i], vpss_cfg);
        if (ret != HI_SUCCESS) {
            goto start_vpss_failed;
        }
    }

    ret = sample_vie_start_and_bind_venc(vpss_grp, vpss_cfg, SAMPLE_VIE_MAX_ROUTE_NUM, route_num);
    if (ret != HI_SUCCESS) {
        goto start_vpss_failed;
    }

    return HI_SUCCESS;

start_vpss_failed:
    for (j = i - 1; j >= 0; j--) {
        sample_vie_stop_vpss(vpss_grp[j]);
    }
    for (i = 0; i < route_num; i++) {
        sample_comm_vi_un_bind_vpss(i, 0, vpss_grp[i], 0);
    }
start_vi_failed:
    for (j = i - 1; j >= 0; j--) {
        sample_comm_vi_stop_vi(&vi_cfg[j]);
    }
    sample_comm_sys_exit();
    return HI_FAILURE;
}

static hi_void sample_vie_stop_route(sample_vi_cfg *vi_cfg, hi_s32 route_num)
{
    hi_s32 i;
    hi_vpss_grp vpss_grp[SAMPLE_VIE_MAX_ROUTE_NUM] = {0, 1, 2, 3};

    sample_vie_stop_and_unbind_venc(vpss_grp, SAMPLE_VIE_MAX_ROUTE_NUM, route_num);
    for (i = route_num - 1; i >= 0; i--) {
        sample_vie_stop_vpss(vpss_grp[i]);
        sample_comm_vi_un_bind_vpss(i, 0, vpss_grp[i], 0);
        sample_comm_vi_stop_vi(&vi_cfg[i]);
    }
    sample_comm_sys_exit();
}

static hi_void sample_vie_print_vi_mode_list(hi_bool is_wdr_mode)
{
    printf("vi vpss mode list: \n");
    if (is_wdr_mode == HI_TRUE) {
        printf("    (0) VI_ONLINE_VPSS_ONLINE\n");
        printf("    (1) VI_ONLINE_VPSS_OFFLINE\n");
        printf("    (2) VI_OFFLINE_VPSS_ONLINE\n");
        printf("    (3) VI_OFFLINE_VPSS_OFFLINE\n");
        printf("please select mode:\n");
        return;
    }
    printf("    (0) VI_ONLINE_VPSS_ONLINE\n");
    printf("    (1) VI_ONLINE_VPSS_OFFLINE\n");
    printf("    (2) VI_OFFLINE_VPSS_ONLINE\n");
    printf("    (3) VI_OFFLINE_VPSS_OFFLINE\n");
    printf("please select mode:\n");
}

static hi_void sample_vie_get_vi_vpss_mode_by_char(hi_char ch, td_bool is_wdr)
{
    switch (ch) {
        case '0':
            g_vie_sys_cfg.mode_type = HI_VI_ONLINE_VPSS_ONLINE;
            g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
            g_vb_param.blk_num[1] = 0; /* yuv_vb num 0 */
            break;
        case '1':
            g_vie_sys_cfg.mode_type = HI_VI_ONLINE_VPSS_OFFLINE;
            g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
            g_vb_param.blk_num[1] = 8; /* yuv_vb num 8 */
            break;
        case '2':
            g_vie_sys_cfg.mode_type = HI_VI_OFFLINE_VPSS_ONLINE;
            g_vb_param.blk_num[0] = is_wdr ? 6 : 0; /* raw_vb num 6 or 0 */
            g_vb_param.blk_num[1] = 0; /* yuv_vb num 0 */
            break;
        case '3':
            g_vie_sys_cfg.mode_type = HI_VI_OFFLINE_VPSS_OFFLINE;
            g_vb_param.blk_num[0] = is_wdr ? 4 : 0; /* raw_vb num 4 or 0 */
            break;
        default:
            g_vie_sys_cfg.mode_type = HI_VI_ONLINE_VPSS_ONLINE;
            g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
            g_vb_param.blk_num[1] = 0; /* yuv_vb num 0 */
            break;
    }
}

static hi_void sample_vie_get_vi_vpss_mode(hi_bool is_wdr_mode)
{
    hi_char ch = '0';
    hi_char end_ch = '3';
    hi_char input[3] = {0}; /* max_len: 3 */
    hi_s32 max_len = 3; /* max_len: 3 */

    sample_vie_print_vi_mode_list(is_wdr_mode);

    while (g_sig_flag == 0) {
        if (gets_s(input, max_len) != HI_NULL && strlen(input) == 1 && input[0] >= ch && input[0] <= end_ch) {
            break;
        } else {
            printf("\nInvalid param, please enter again!\n\n");
            sample_vie_print_vi_mode_list(is_wdr_mode);
        }
        (hi_void)fflush(stdin);
    }

    sample_vie_get_vi_vpss_mode_by_char(input[0], is_wdr_mode);
}

static hi_void sample_vie_vpss_get_wrap_cfg(sampe_sys_cfg *g_vie_sys_cfg, sample_vpss_cfg *vpss_cfg)
{
    hi_u32 full_lines_std;
    if (g_vie_sys_cfg->vpss_wrap_en) {
        if (SENSOR0_TYPE == SC4336P_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == OS04D10_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == GC4023_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1 || SENSOR0_TYPE == SC4336P_MIPI_3M_30FPS_10BIT) {
            full_lines_std = 1500; /* full_lines_std: 1500 */
        } else if (SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1585; /* full_lines_std: 1585 */
        } else if (SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT || SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1700; /* full_lines_std: 1700 */
        } else {
            g_vie_sys_cfg->vpss_wrap_en = HI_FALSE;
            vpss_cfg->wrap_attr[0].enable = HI_FALSE;
            return;
        }
        (hi_void)sample_comm_vpss_get_wrap_cfg(vpss_cfg->chn_attr, g_vie_sys_cfg->mode_type, full_lines_std,
            &vpss_cfg->wrap_attr[0]);
        g_vie_sys_cfg->vpss_wrap_size = vpss_cfg->wrap_attr[0].buf_size;
    }
}

static hi_s32 sample_vie_all_mode(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg;
    sample_sns_type sns_type = SENSOR0_TYPE;

    sample_vie_get_vi_vpss_mode(HI_FALSE);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    sample_vie_get_pipe_wrap_line(vi_cfg, 1);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);

    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_get_char();

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_s32 sample_vie_wdr(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg;
    sample_sns_type sns_type = SENSOR0_TYPE;

    sample_vie_get_vi_vpss_mode(HI_TRUE);

    if (sns_type == SC431HAI_MIPI_4M_30FPS_10BIT) {
        sns_type = SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1;
    } else if (sns_type == SC450AI_MIPI_4M_30FPS_10BIT) {
        sns_type = SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1;
    } else if (sns_type == SC500AI_MIPI_5M_30FPS_10BIT) {
        sns_type = SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1;
    } else {
        sample_print("Not supported by current sensor%d\n", sns_type);
        return HI_FAILURE;
    }

    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    if (g_vie_sys_cfg.mode_type == HI_VI_OFFLINE_VPSS_ONLINE || g_vie_sys_cfg.mode_type == HI_VI_OFFLINE_VPSS_OFFLINE) {
        vi_cfg[0].pipe_info[0].pipe_attr.compress_mode = HI_COMPRESS_MODE_FRAME;
        vi_cfg[0].pipe_info[1].pipe_attr.compress_mode = HI_COMPRESS_MODE_FRAME;
        g_vb_param.compress_mode[0] = HI_COMPRESS_MODE_FRAME;
    }
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);

    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_get_char();

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_s32 sample_vie_switch_first_route(sample_sns_type sns_type, sample_vi_cfg *vi_cfg, hi_bool is_run_be)
{
    const hi_vi_pipe vi_pipe = 0;
    const hi_vi_chn vi_chn = 0;
    hi_vpss_grp vpss_grp[1] = {0};
    const hi_u32 grp_num = 1;
    const hi_vpss_chn vpss_chn = 0;
    sample_vpss_cfg vpss_cfg;
    hi_size in_size;
    sample_run_be_bind_pipe bind_pipe = {
        .wdr_mode = OT_WDR_MODE_NONE, .pipe_id = {0}, .pipe_num = 1};
    hi_s32 ret;

    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);
    vi_cfg->pipe_info[0].isp_be_end_trigger = is_run_be;
    ret = sample_comm_vi_start_vi(vi_cfg);
    if (ret != HI_SUCCESS) {
        goto start_vi_failed;
    }
    sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], vpss_chn);
    ret = sample_vie_start_vpss(vpss_grp[0], &vpss_cfg);
    if (ret != HI_SUCCESS) {
        goto start_vpss_failed;
    }
    ret = sample_vie_start_and_bind_venc(vpss_grp, &vpss_cfg, sizeof(vpss_grp) / sizeof(vpss_grp[0]), grp_num);
    if (ret != HI_SUCCESS) {
        goto start_venc_failed;
    }

    if (is_run_be) {
        sample_comm_vi_send_run_be_frame(&bind_pipe);
    } else {
        sample_print_pause("press enter key to switch!\n");
    }

    sample_vie_stop_and_unbind_venc(vpss_grp, sizeof(vpss_grp) / sizeof(vpss_grp[0]), grp_num);
start_venc_failed:
    sample_vie_stop_vpss(vpss_grp[0]);
start_vpss_failed:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], vpss_chn);
    if (ret == HI_SUCCESS) {
        sample_comm_vi_mode_switch_stop_vi(vi_cfg);
    } else {
        sample_comm_vi_stop_vi(vi_cfg);
    }
start_vi_failed:
    return ret;
}

static hi_s32 sample_vie_switch_second_route(sample_sns_type sns_type, hi_bool is_run_be)
{
    const hi_vi_pipe vi_pipe = 0;
    const hi_vi_chn vi_chn = 0;
    hi_vpss_grp vpss_grp[1] = {0};
    const hi_u32 grp_num = 1;
    const hi_vpss_chn vpss_chn = 0;
    hi_size in_size;
    sample_vi_cfg vi_cfg;
    sample_vpss_cfg vpss_cfg;
    sample_run_be_bind_pipe bind_pipe = {
        .wdr_mode = OT_WDR_MODE_2To1_LINE,
        .pipe_id = {0, 1},
        .pipe_num = 2
    };
    hi_s32 ret;

    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);
    ret = sample_comm_vi_mode_switch_start_vi(&vi_cfg, HI_FALSE, &in_size);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], vpss_chn);

    ret = sample_vie_start_vpss(vpss_grp[0], &vpss_cfg);
    if (ret != HI_SUCCESS) {
        goto start_vpss_failed;
    }

    ret = sample_vie_start_and_bind_venc(vpss_grp, &vpss_cfg, sizeof(vpss_grp) / sizeof(vpss_grp[0]), grp_num);
    if (ret != HI_SUCCESS) {
        goto start_venc_failed;
    }

    if (is_run_be) {
        sample_comm_vi_send_run_be_frame(&bind_pipe);
    } else {
        sample_get_char();
    }

    sample_vie_stop_and_unbind_venc(vpss_grp, sizeof(vpss_grp) / sizeof(vpss_grp[0]), grp_num);
start_venc_failed:
    sample_vie_stop_vpss(vpss_grp[0]);
start_vpss_failed:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], vpss_chn);
    sample_comm_vi_stop_vi(&vi_cfg);

    return ret;
}

static hi_void sample_vie_vpss_get_cfg_by_size(sample_vpss_cfg *vpss_cfg, hi_size *in_size)
{
    sample_comm_vpss_get_default_vpss_cfg(vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    vpss_cfg->grp_attr.max_width = in_size->width;
    vpss_cfg->grp_attr.max_height = in_size->height;
    vpss_cfg->chn_attr[0].width = in_size->width;
    vpss_cfg->chn_attr[0].height = in_size->height;
}

static hi_s32 sample_vie_switch_resolution_route(sample_sns_type sns_type)
{
    const hi_vi_pipe vi_pipe = 0;
    const hi_vi_chn vi_chn = 0;
    hi_vpss_grp vpss_grp[1] = {0};
    const hi_u32 grp_num = 1;
    const hi_vpss_chn vpss_chn = 0;
    hi_size in_size;
    sample_vi_cfg vi_cfg;
    sample_vpss_cfg vpss_cfg;
    hi_s32 ret;

    in_size.width = 1920;  //  switch to 1920
    in_size.height = 1080; //  switch to 1080

    sample_comm_vi_init_vi_cfg(sns_type, &in_size, &vi_cfg);
    sample_vie_get_pipe_wrap_line(&vi_cfg, 1);
    sample_vie_vpss_get_cfg_by_size(&vpss_cfg, &in_size);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);

    ret = sample_comm_vi_mode_switch_start_vi(&vi_cfg, HI_TRUE, &in_size);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_comm_vi_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], vpss_chn);
    ret = sample_vie_start_vpss(vpss_grp[0], &vpss_cfg);
    if (ret != HI_SUCCESS) {
        goto start_vpss_failed;
    }

    ret = sample_vie_start_and_bind_venc(vpss_grp, &vpss_cfg, sizeof(vpss_grp) / sizeof(vpss_grp[0]), grp_num);
    if (ret != HI_SUCCESS) {
        goto start_venc_failed;
    }

    sample_get_char();

    sample_vie_stop_and_unbind_venc(vpss_grp, sizeof(vpss_grp) / sizeof(vpss_grp[0]), grp_num);
start_venc_failed:
    sample_vie_stop_vpss(vpss_grp[0]);
start_vpss_failed:
    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp[0], vpss_chn);
    sample_comm_vi_stop_vi(&vi_cfg);

    return ret;
}

static hi_s32 sample_vie_switch_mode(hi_void)
{
    hi_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    hi_bool is_run_be = HI_FALSE;
    sample_vi_cfg vi_cfg;

    sample_vie_get_vi_vpss_mode(HI_TRUE);
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[1]);
    sample_vie_vpss_get_default_wrap_buf_size(sns_type, &g_vie_sys_cfg);
    ret = sample_vie_sys_init();
    if (ret != HI_SUCCESS) {
        return ret;
    }

    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    ret = sample_vie_switch_first_route(sns_type, &vi_cfg, is_run_be);
    if (ret != HI_SUCCESS) {
        sample_comm_sys_exit();
        return ret;
    }

    if (sns_type == SC431HAI_MIPI_4M_30FPS_10BIT) {
        sns_type = SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1;
    } else if (sns_type == SC450AI_MIPI_4M_30FPS_10BIT) {
        sns_type = SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1;
    } else if (sns_type == SC500AI_MIPI_5M_30FPS_10BIT) {
        sns_type = SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1;
    } else {
        printf("Not supported by current sensor%d\n", sns_type);
        sample_comm_vi_stop_isp(&vi_cfg);
        sample_comm_sys_exit();
        return HI_FAILURE;
    }

    ret = sample_vie_switch_second_route(sns_type, is_run_be);
    sample_comm_sys_exit();
    return ret;
}

static hi_s32 sample_vie_switch_resolution(hi_void)
{
    hi_s32 ret;

    sample_sns_type sns_type = SENSOR0_TYPE;
    hi_bool is_run_be = HI_FALSE;
    sample_vi_cfg vi_cfg;

    sample_vie_get_vi_vpss_mode(HI_FALSE);
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[1]);
    sample_vie_vpss_get_default_wrap_buf_size(sns_type, &g_vie_sys_cfg);
    ret = sample_vie_sys_init();
    if (ret != HI_SUCCESS) {
        return ret;
    }

    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    sample_vie_get_pipe_wrap_line(&vi_cfg, 1);
    ret = sample_vie_switch_first_route(sns_type, &vi_cfg, is_run_be);
    if (ret != HI_SUCCESS) {
        sample_comm_sys_exit();
        return ret;
    }

    ret = sample_vie_switch_resolution_route(sns_type);
    sample_comm_sys_exit();
    return ret;
}

static hi_s32 sample_vie_run_be_switch_mode(hi_void)
{
    hi_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;
    hi_bool is_run_be = HI_TRUE;
    sample_vi_cfg vi_cfg;

    sample_vie_get_vi_vpss_mode_by_char('2', HI_TRUE);  // '2': VI_OFFLINE_VPSS_ONLINE
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[1]);
    sample_vie_vpss_get_default_wrap_buf_size(sns_type, &g_vie_sys_cfg);
    g_vb_param.blk_num[0] = 4; /* raw_vb num 4 */
    g_vb_param.blk_num[1] = 4; /* yuv_vb num 4 */
    ret = sample_vie_sys_init();
    if (ret != HI_SUCCESS) {
        return ret;
    }

    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    ret = sample_vie_switch_first_route(sns_type, &vi_cfg, is_run_be);
    if (ret != HI_SUCCESS) {
        sample_comm_sys_exit();
        return ret;
    }

    if (sns_type == SC431HAI_MIPI_4M_30FPS_10BIT) {
        sns_type = SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1;
    } else if (sns_type == SC450AI_MIPI_4M_30FPS_10BIT) {
        sns_type = SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1;
    } else if (sns_type == SC500AI_MIPI_5M_30FPS_10BIT) {
        sns_type = SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1;
    } else {
        printf("Not supported by current sensor%d\n", sns_type);
        sample_comm_vi_stop_isp(&vi_cfg);
        sample_comm_sys_exit();
        return HI_FAILURE;
    }

    ret = sample_vie_switch_second_route(sns_type, is_run_be);
    sample_comm_sys_exit();
    return ret;
}

static hi_void sample_vie_do_fpn_calibrate_and_correction(hi_vi_pipe vi_pipe)
{
    sample_comm_vi_fpn_calibrate(vi_pipe, &g_calibration_cfg);

    printf("press enter key to enable fpn correction!\n");
    sample_get_char();

    sample_comm_vi_enable_fpn_correction(vi_pipe, &g_correction_cfg);
}

static hi_s32 sample_vie_fpn(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg;
    const hi_vi_pipe vi_pipe = 0;
    g_vie_sys_cfg.mode_type = OT_VI_ONLINE_VPSS_ONLINE;
    g_vb_param.blk_num[0] = 0; /* online unuse raw vb */
    g_vb_param.blk_num[1] = 0; /* online unuse yuv vb  */
    sample_comm_vi_get_default_vi_cfg(SENSOR0_TYPE, &vi_cfg[0]);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);
    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_vie_do_fpn_calibrate_and_correction(vi_pipe);

    sample_get_char();

    sample_comm_vi_disable_fpn_correction(vi_pipe, &g_correction_cfg);

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_void sample_vie_set_ldc_en(hi_vi_pipe vi_pipe, hi_bool enable)
{
    hi_s32 ret;
    hi_ldc_attr ldc_attr;

    ldc_attr.enable                       = enable;
    ldc_attr.ldc_version                  = HI_LDC_V3;
    ldc_attr.ldc_v3_attr.view_type        = OT_LDC_VIEW_TYPE_ALL;
    ldc_attr.ldc_v3_attr.center_x_offset  = 100; /* 100: center x offset */
    ldc_attr.ldc_v3_attr.center_y_offset  = 100; /* 100: center y offset */
    ldc_attr.ldc_v3_attr.distortion_ratio = 100; /* 100: distortion_ratio */
    ldc_attr.ldc_v3_attr.min_ratio  = 0;

    ret = hi_mpi_vi_set_chn_ldc_attr(vi_pipe, 0, &ldc_attr);
    if (ret != HI_SUCCESS) {
        sample_print("set ldc attr failed.ret:0x%x !\n", ret);
    }
}

static hi_void sample_vie_set_rotation_en(hi_vi_pipe vi_pipe, hi_bool enable)
{
    hi_s32 ret;
    hi_rotation_attr rotation_attr;

    rotation_attr.enable = enable;
    rotation_attr.rotation_type = OT_ROTATION_ANG_FIXED;
    rotation_attr.rotation_fixed = OT_ROTATION_90;

    ret = hi_mpi_vi_set_chn_rotation(vi_pipe, 0, &rotation_attr);
    if (ret != HI_SUCCESS) {
        sample_print("set rotation attr failed.ret:0x%x !\n", ret);
    }
}

static hi_void sample_vie_switch_ldc_rotation_en(hi_vi_pipe vi_pipe)
{
    sample_print_pause("press enter key to enable ldc and rotation!\n");

    sample_vie_set_ldc_en(vi_pipe, HI_TRUE);
    sample_vie_set_rotation_en(vi_pipe, HI_TRUE);

    sample_print_pause("press enter key to disable ldc and rotation!\n");

    sample_vie_set_ldc_en(vi_pipe, HI_FALSE);
    sample_vie_set_rotation_en(vi_pipe, HI_FALSE);
}

static hi_s32 sample_vie_ldc_rotation(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg;
    const hi_vi_pipe vi_pipe = 0;

    g_vb_param.blk_num[0] = 4; /* raw_vb num 4 */
    sample_comm_vi_get_default_vi_cfg(SENSOR0_TYPE, &vi_cfg[0]);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);
    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_vie_switch_ldc_rotation_en(vi_pipe);
    sample_get_char();

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_void sample_vie_switch_low_delay(hi_vi_pipe vi_pipe, hi_vi_chn vi_chn, hi_vpss_grp vpss_grp,
    hi_vpss_chn vpss_chn)
{
    hi_s32 ret;
    hi_low_delay_info low_delay_info;

    low_delay_info.enable = HI_TRUE;
    low_delay_info.line_cnt = 300; /* 300: low delay line cnt */
    low_delay_info.one_buf_en = HI_FALSE;

    sample_print_pause("press enter key to enable pipe low delay!\n");

    ret = hi_mpi_vi_set_pipe_low_delay(vi_pipe, &low_delay_info);
    if (ret != HI_SUCCESS) {
        sample_print("enable pipe low delay failed!\n");
    }
    ret = hi_mpi_vpss_set_chn_low_delay(vpss_grp, vpss_chn, &low_delay_info);
    if (ret != HI_SUCCESS) {
        sample_print("enable vpss low delay failed!\n");
    }

    sample_print_pause("press enter key to disable pipe low delay!\n");

    low_delay_info.enable = HI_FALSE;
    ret = hi_mpi_vi_set_pipe_low_delay(vi_pipe, &low_delay_info);
    if (ret != HI_SUCCESS) {
        sample_print("disable pipe low delay failed!\n");
    }
    ret = hi_mpi_vpss_set_chn_low_delay(vpss_grp, vpss_chn, &low_delay_info);
    if (ret != HI_SUCCESS) {
        sample_print("enable vpss low delay failed!\n");
    }
}

static hi_s32 sample_vie_lowdelay(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg = {0};
    sample_sns_type sns_type = SENSOR0_TYPE;
    const hi_vi_pipe vi_pipe = 0;
    const hi_vi_chn vi_chn = 0;
    const hi_vpss_grp vpss_grp = 0;
    const hi_vpss_chn vpss_chn = 0;

    g_vb_param.blk_num[0] = 4; /* raw_vb num 4 */
    g_vie_sys_cfg.vpss_wrap_en = 0;
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, TD_FALSE);

    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_vie_switch_low_delay(vi_pipe, vi_chn, vpss_grp, vpss_chn);
    sample_get_char();

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_void sample_switch_user_pic(hi_vi_pipe vi_pipe)
{
    hi_s32 ret;
    hi_3dnr_attr nr_attr;
    sample_vi_user_pic_type user_pic_type;
    sample_vi_user_frame_info user_frame_info = {0};

    for (user_pic_type = VI_USER_PIC_FRAME; user_pic_type <= VI_USER_PIC_BGCOLOR; user_pic_type++) {
        ret = sample_common_vi_load_user_pic(vi_pipe, user_pic_type, &user_frame_info);
        if (ret != HI_SUCCESS) {
            sample_print("load user pic failed!\n");
            return;
        }

        ret = hi_mpi_vi_set_pipe_user_pic(vi_pipe, &user_frame_info.frame_info);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_vi_set_pipe_user_pic failed!\n");
        }

        ret = hi_mpi_vi_get_pipe_3dnr_attr(vi_pipe, &nr_attr);
        if (ret != HI_SUCCESS) {
            sample_print("vi pipe(%d) get 3dnr_attr failed!\n", vi_pipe);
        }
        nr_attr.enable = HI_FALSE;
        ret = hi_mpi_vi_set_pipe_3dnr_attr(vi_pipe, &nr_attr);
        if (ret != HI_SUCCESS) {
            sample_print("vi pipe(%d) set 3dnr_attr failed!\n", vi_pipe);
        }

        sample_print_pause("press enter key to enable user pic!\n");
        ret = hi_mpi_vi_enable_pipe_user_pic(vi_pipe);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_vi_enable_pipe_user_pic failed!\n");
        }

        sample_print_pause("press enter key to disable user pic!\n");
        ret = hi_mpi_vi_disable_pipe_user_pic(vi_pipe);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_vi_disable_pipe_user_pic failed!\n");
        }

        nr_attr.enable = HI_TRUE;
        ret = hi_mpi_vi_set_pipe_3dnr_attr(vi_pipe, &nr_attr);
        if (ret != HI_SUCCESS) {
            sample_print("vi pipe(%d) set 3dnr_attr failed!\n", vi_pipe);
        }

        sleep(1);
        sample_common_vi_unload_user_pic(&user_frame_info);
    }
}

static hi_s32 sample_vie_user_pic(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg = {0};
    sample_sns_type sns_type = SENSOR0_TYPE;
    const hi_vi_pipe vi_pipe = 0;

    g_vb_param.blk_num[0] = 4; /* raw_vb num 4 */
    g_vb_param.blk_num[1] = 4;
    g_vie_sys_cfg.vpss_wrap_en = 0;
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, TD_FALSE);

    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_switch_user_pic(vi_pipe);
    sample_get_char();

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_void sample_vi_get_two_sensor_vi_cfg(sample_sns_type sns_type, sample_vi_cfg vi_cfg[], size_t size)
{
    const hi_vi_pipe vi_pipe = 1; /* dev1 bind pipe1 */
    if (size < 2) { /* need 2 sensor cfg */
        return;
    }
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[1]);
    vi_cfg[0].mipi_info.divide_mode = LANE_DIVIDE_MODE_1;
#ifdef OT_FPGA
    vi_cfg[1].sns_info.bus_id = 0; /* i2c0 */
#else
    vi_cfg[1].sns_info.bus_id = 1; /* i2c1 */
#endif
    vi_cfg[1].sns_info.sns_clk_src = 1;
    vi_cfg[1].sns_info.sns_rst_src = 1;
    sample_comm_vi_get_mipi_info_by_dev_id(sns_type, 0, &vi_cfg[0].mipi_info); /* dev 0 */
    sample_comm_vi_get_mipi_info_by_dev_id(sns_type, 1, &vi_cfg[1].mipi_info); /* dev 1 */
    vi_cfg[1].dev_info.vi_dev = 1;
    vi_cfg[1].bind_pipe.pipe_id[0] = vi_pipe;
    vi_cfg[1].grp_info.grp_num = 1;
    vi_cfg[1].grp_info.fusion_grp[0] = 1;
    vi_cfg[1].grp_info.fusion_grp_attr[0].pipe_id[0] = vi_pipe;

    /* total performance does not support 6M30. */
    /* total performance does not support 6M30. */
    if ((sns_type == SC4336P_MIPI_4M_30FPS_10BIT) || (sns_type == SC4336P_MIPI_4M_30FPS_10BIT) ||
        (sns_type == SC450AI_MIPI_4M_30FPS_10BIT) || (sns_type == SC500AI_MIPI_5M_30FPS_10BIT) ||
        (sns_type == OS04D10_MIPI_4M_30FPS_10BIT)) {
        vi_cfg[0].pipe_info[0].pipe_attr.frame_rate_ctrl.src_frame_rate = 30; /* 30: src_rate */
        vi_cfg[0].pipe_info[0].pipe_attr.frame_rate_ctrl.dst_frame_rate = 20; /* 20: dst_rate */
        vi_cfg[1].pipe_info[0].pipe_attr.frame_rate_ctrl.src_frame_rate = 30; /* 30: src_rate */
        vi_cfg[1].pipe_info[0].pipe_attr.frame_rate_ctrl.dst_frame_rate = 20; /* 20: dst_rate */
    }
}

/* Set pin_mux i2c0 & i2c1 & sensor0 & sensor1 & MIPI0 & MIPI1 before using this sample! */
static hi_s32 sample_vie_two_sensor(hi_void)
{
    sample_vi_cfg vi_cfg[2];
    sample_vpss_cfg vpss_cfg;
    sample_sns_type sns_type = SENSOR0_TYPE;

    g_vie_sys_cfg.route_num = 2; /* 2: route_num */

    g_vb_param.blk_num[0] = 0; /* raw_vb num 5 */
    g_vb_param.blk_num[1] = 6; /* yuv_vb num 6 */
    g_vb_param.blk_num[2] = 6; /* vpss chn 2, yuv_vb num 6 */

    sample_vi_get_two_sensor_vi_cfg(sns_type, vi_cfg, sizeof(vi_cfg) / sizeof(vi_cfg[0]));
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_vie_sys_cfg.vpss_wrap_en);
    sample_vie_get_pipe_wrap_line(vi_cfg, g_vie_sys_cfg.route_num);
    sample_vie_vpss_get_wrap_cfg(&g_vie_sys_cfg, &vpss_cfg);
    if (sample_vie_start_route(vi_cfg, &vpss_cfg, g_vie_sys_cfg.route_num) != HI_SUCCESS) {
        return HI_FAILURE;
    }

    sample_get_char();

    sample_vie_stop_route(vi_cfg, g_vie_sys_cfg.route_num);
    return HI_SUCCESS;
}

static hi_void sample_vie_usage(const char *prg_name)
{
    printf("usage : %s <index> \n", prg_name);
    printf("index:\n");
    printf("    (0) all mode route          :vi linear(online/offline) -> vpss(online/offline) -> venc.\n");
    printf("    (1) wdr route               :vi wdr(online/offline) -> vpss(online/offline) -> venc.\n");
    printf("    (2) fpn calibrate & correct :vi fpn calibrate & correct(offline) -> vpss(offline) -> venc.\n");
    printf("    (3) ldc rotation            :vi ldc/rotation(offline) -> vpss(offline) -> venc.\n");
    printf("    (4) low delay               :vi(pipe lowdelay) -> vpss(lowdelay) -> venc.\n");
    printf("    (5) user pic                :vi user pic(offline) -> vpss(offline) -> venc.\n");
    printf("    (6) two sensor              :vi two sensor(offline) -> vpss(offline) -> venc.\n");
    printf("    (7) isp run switch mode     :vi linear switch to wdr(online/offline) -> vpss(online/offline)"
                                            "-> venc.\n");
    printf("    (8) switch resolution       :vi 4M switch to 1080P(online/offline) -> vpss(online/offline) -> venc.\n");
    printf("    (9) isp run be switch mode  :vi linear switch to wdr(offline) -> vpss(offline) -> venc.\n");
}

static hi_void sample_vie_handle_sig(hi_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_sig_flag = 1;
    }
}

static hi_void sample_register_sig_handler(hi_void (*sig_handle)(hi_s32))
{
    struct sigaction sa;

    (hi_void)memset_s(&sa, sizeof(struct sigaction), 0, sizeof(struct sigaction));
    sa.sa_handler = sig_handle;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, HI_NULL);
    sigaction(SIGTERM, &sa, HI_NULL);
}

static hi_s32 sample_vie_execute_case(hi_u32 case_index)
{
    hi_s32 ret;

    switch (case_index) {
        case 0: /* 0 all mode route */
            ret = sample_vie_all_mode();
            break;
        case 1: /* 1 wdr route */
            ret = sample_vie_wdr();
            break;
        case 2: /* 2 fpn calibrate and correct */
            ret = sample_vie_fpn();
            break;
        case 3: /* 3 ldc and rotation */
            ret = sample_vie_ldc_rotation();
            break;
        case 4: /* 4 low delay */
            ret = sample_vie_lowdelay();
            break;
        case 5: /* 5 user pic */
            ret = sample_vie_user_pic();
            break;
        case 6: /* 6 two sensor */
            ret = sample_vie_two_sensor();
            break;
        case 7: /* 7 switch mode */
            ret = sample_vie_switch_mode();
            break;
        case 8: /* 8 switch resolution */
            ret = sample_vie_switch_resolution();
            break;
        case 9: /* 9 run be switch mode */
            ret = sample_vie_run_be_switch_mode();
            break;
        default:
            ret = HI_FAILURE;
            break;
    }

    return ret;
}

static hi_s32 sample_vie_msg_proc_vb_pool_share(hi_s32 pid)
{
    hi_s32 ret;
    hi_u32 i;
    hi_bool isp_states[HI_VI_MAX_PIPE_NUM];
#ifndef SAMPLE_MEM_SHARE_ENABLE
    hi_vb_common_pools_id pools_id = {0};

    if (hi_mpi_vb_get_common_pool_id(&pools_id) != HI_SUCCESS) {
        sample_print("get common pool_id failed!\n");
        return HI_FAILURE;
    }

    for (i = 0; i < pools_id.pool_cnt; ++i) {
        if (hi_mpi_vb_pool_share(pools_id.pool[i], pid) != HI_SUCCESS) {
            sample_print("vb pool share failed!\n");
            return HI_FAILURE;
        }
    }
#endif
    ret = sample_comm_vi_get_isp_run_state(isp_states, HI_VI_MAX_PIPE_NUM);
    if (ret != HI_SUCCESS) {
        sample_print("get isp states fail\n");
        return HI_FAILURE;
    }

    for (i = 0; i < HI_VI_MAX_PIPE_NUM; i++) {
        if (!isp_states[i]) {
            continue;
        }
        ret = hi_mpi_isp_mem_share(i, pid);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_isp_mem_share vi_pipe %u, pid %d fail\n", i, pid);
        }
    }

    return HI_SUCCESS;
}

static hi_void sample_vie_msg_proc_vb_pool_unshare(hi_s32 pid)
{
    hi_s32 ret;
    hi_u32 i;
    hi_bool isp_states[HI_VI_MAX_PIPE_NUM];
#ifndef SAMPLE_MEM_SHARE_ENABLE
    hi_vb_common_pools_id pools_id = {0};
    if (hi_mpi_vb_get_common_pool_id(&pools_id) == HI_SUCCESS) {
        for (i = 0; i < pools_id.pool_cnt; ++i) {
            ret = hi_mpi_vb_pool_unshare(pools_id.pool[i], pid);
            if (ret != HI_SUCCESS) {
                sample_print("hi_mpi_vb_pool_unshare vi_pipe %u, pid %d fail\n", pools_id.pool[i], pid);
            }
        }
    }
#endif
    ret = sample_comm_vi_get_isp_run_state(isp_states, HI_VI_MAX_PIPE_NUM);
    if (ret != HI_SUCCESS) {
        sample_print("get isp states fail\n");
        return;
    }

    for (i = 0; i < HI_VI_MAX_PIPE_NUM; i++) {
        if (!isp_states[i]) {
            continue;
        }
        ret = hi_mpi_isp_mem_unshare(i, pid);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_isp_mem_unshare vi_pipe %u, pid %d fail\n", i, pid);
        }
    }
}

static hi_s32 sample_vie_ipc_msg_proc(const sample_ipc_msg_req_buf *msg_req_buf,
    hi_bool *is_need_fb, sample_ipc_msg_res_buf *msg_res_buf)
{
    hi_s32 ret;

    if (msg_req_buf == HI_NULL || is_need_fb == HI_NULL) {
        return HI_FAILURE;
    }

    /* need feedback default */
    *is_need_fb = HI_TRUE;

    switch ((sample_msg_type)msg_req_buf->msg_type) {
        case SAMPLE_MSG_TYPE_VB_POOL_SHARE_REQ: {
            if (msg_res_buf == HI_NULL) {
                return HI_FAILURE;
            }
            ret = sample_vie_msg_proc_vb_pool_share(msg_req_buf->msg_data.pid);
            msg_res_buf->msg_type = SAMPLE_MSG_TYPE_VB_POOL_SHARE_RES;
            msg_res_buf->msg_data.is_req_success = (ret == HI_SUCCESS) ? HI_TRUE : HI_FALSE;
            break;
        }
        case SAMPLE_MSG_TYPE_VB_POOL_UNSHARE_REQ: {
            if (msg_res_buf == HI_NULL) {
                return HI_FAILURE;
            }
            sample_vie_msg_proc_vb_pool_unshare(msg_req_buf->msg_data.pid);
            msg_res_buf->msg_type = SAMPLE_MSG_TYPE_VB_POOL_UNSHARE_RES;
            msg_res_buf->msg_data.is_req_success = HI_TRUE;
            break;
        }
        default: {
            printf("unsupported msg type(%ld)!\n", msg_req_buf->msg_type);
            return HI_FAILURE;
        }
    }
    return HI_SUCCESS;
}

#ifdef __LITEOS__
hi_s32 app_main(hi_s32 argc, hi_char *argv[])
#else
hi_s32 main(hi_s32 argc, hi_char *argv[])
#endif
{
    hi_s32 ret;
    hi_u32 index;
    hi_char *end_ptr = HI_NULL;

    if (argc != 2) { /* 2:arg num */
        sample_vie_usage(argv[0]);
        return HI_FAILURE;
    }

    if (!strncmp(argv[1], "-h", 2)) { /* 2:arg num */
        sample_vie_usage(argv[0]);
        return HI_FAILURE;
    }

    if (strlen(argv[1]) >= 2 || strlen(argv[1]) == 0 || !check_digit(argv[1][0])) { /* 2:arg len */
        sample_vie_usage(argv[0]);
        return HI_FAILURE;
    }

    index = (hi_u32)strtol(argv[1], &end_ptr, 10); /* base 10, argv[1] has been check between [0, 10] */
    if ((end_ptr == argv[1]) || (*end_ptr) != '\0') {
        sample_vie_usage(argv[0]);
        return HI_FAILURE;
    }

#ifndef __LITEOS__
    sample_register_sig_handler(sample_vie_handle_sig);
#endif

    if (sample_ipc_server_init(sample_vie_ipc_msg_proc) != HI_SUCCESS) {
        printf("sample_ipc_server_init failed!!!\n");
    }

    ret = sample_vie_execute_case(index);
    if ((ret == HI_SUCCESS) && (g_sig_flag == 0)) {
        printf("\033[0;32mprogram exit normally!\033[0;39m\n");
    } else {
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    sample_ipc_server_deinit();
    return ret;
}

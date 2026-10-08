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
#include "hi_mpi_awb.h"
#include "ot_sns_ctrl.h"

#define VENC_MAX_CHN_NUM 8
#define BUFFER_SIZE 128
#define CHN_DEPTH 2

static volatile sig_atomic_t g_sig_flag = 0;
#define ARGV_AE_EXP 1
#define ARGV_AE_AGAIN 2
#define ARGV_AE_DGAIN 3
#define ARGV_AE_ISP_DGAIN 4
#define ARGV_AE_EXPOSURE 5
#define ARGV_AE_LINE_PER500MS 6
#define ARGV_AWB_RGAIN 7
#define ARGV_AWB_GGAIN 8
#define ARGV_AWB_BGAIN 9
static hi_u32 g_sns_exp, g_sns_again, g_sns_dgain, g_isp_dgain, g_exposure, g_lines_per500ms;
static hi_u32 g_awb_rgain, g_awb_ggain, g_awb_bgain;

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
    .blk_num = {5, 7, 3}
};

static sampe_sys_cfg g_fast_ae_sys_cfg = {
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

void sensor_sc4336p_init(td_u32 sns_exp, td_u32 sns_again, td_u32 sns_dgain);
void sensor_sc4336p_exit();
void sensor_sc4336p_read_exp(td_u32 *sns_exp);
void sensor_sc4336p_read_gain(td_u32 *sns_again, td_u32 *sns_dgain);

static hi_void sample_get_char(hi_void)
{
    if (g_sig_flag == 1) {
        return;
    }

    sample_pause();
}

/* define SAMPLE_MEM_SHARE_ENABLE, when use tools to dump YUV/RAW. */
#ifdef SAMPLE_MEM_SHARE_ENABLE
hi_void sample_fast_ae_init_mem_share(hi_void)
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

static hi_void sample_fast_ae_get_pipe_wrap_line(sample_vi_cfg vi_cfg[], hi_u32 route_num)
{
    hi_vi_wrap_out_param out_param;
    hi_vi_wrap_in_param in_param;
    hi_u32 i;

    if (g_fast_ae_sys_cfg.mode_type == HI_VI_ONLINE_VPSS_ONLINE ||
        g_fast_ae_sys_cfg.mode_type == HI_VI_ONLINE_VPSS_OFFLINE) {
        return;
    }

    (hi_void)memcpy_s(&in_param, sizeof(hi_vi_wrap_in_param), &g_vi_wrap_param, sizeof(hi_vi_wrap_in_param));
    if (g_fast_ae_sys_cfg.mode_type == HI_VI_OFFLINE_VPSS_OFFLINE) {
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
    if (hi_mpi_sys_get_vi_wrap_buffer_line(&in_param, &out_param) != HI_SUCCESS) {
        sample_print("sample_vie get wrap line fail, pipe_wrap not enable!\n");
        return;
    }

    for (i = 0; i < route_num; i++) {
        vi_cfg[i].pipe_info[0].wrap_attr.buf_line = out_param.buf_line[i];
        vi_cfg[i].pipe_info[0].wrap_attr.enable = HI_TRUE;
    }
}

static hi_s32 sample_fast_ae_sys_init(hi_void)
{
    hi_vb_cfg vb_cfg;
    hi_u32 supplement_config = HI_VB_SUPPLEMENT_BNR_MOT_MASK | HI_VB_SUPPLEMENT_MOTION_DATA_MASK;

    sample_comm_sys_get_default_vb_cfg(&g_vb_param, &vb_cfg);
    /* prepare vpss wrap vb */
    if (g_fast_ae_sys_cfg.vpss_wrap_en) {
        vb_cfg.common_pool[SAMPLE_VIE_POOL_NUM].blk_cnt = 1;
        vb_cfg.common_pool[SAMPLE_VIE_POOL_NUM].blk_size = g_fast_ae_sys_cfg.vpss_wrap_size;
    }
    if (sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config) != HI_SUCCESS) {
        return HI_FAILURE;
    }

#ifdef SAMPLE_MEM_SHARE_ENABLE
    sample_fast_ae_init_mem_share();
#endif

    if (sample_comm_vi_set_vi_vpss_mode(g_fast_ae_sys_cfg.mode_type, HI_VI_AIISP_MODE_DEFAULT) != HI_SUCCESS) {
        goto sys_exit;
    }

    return HI_SUCCESS;
sys_exit:
    sample_comm_sys_exit();
    return HI_FAILURE;
}

static hi_s32 sample_vpss_set_wrap_grp_int_attr(hi_vi_vpss_mode_type mode_type, hi_bool wrap_en, hi_u32 max_height)
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

static hi_s32 sample_fast_ae_start_vpss(hi_vpss_grp grp, sample_vpss_cfg *vpss_cfg)
{
    hi_s32 ret;
    sample_vpss_chn_attr vpss_chn_attr = {0};

    if (grp == 0) {
        ret = sample_vpss_set_wrap_grp_int_attr(g_fast_ae_sys_cfg.mode_type,
            vpss_cfg->wrap_attr[0].enable, vpss_cfg->grp_attr.max_height);
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

static hi_void sample_fast_ae_stop_vpss(hi_vpss_grp grp)
{
    hi_bool chn_enable[HI_VPSS_MAX_PHYS_CHN_NUM] = {HI_TRUE, HI_FALSE, HI_FALSE};

    sample_common_vpss_stop(grp, chn_enable, HI_VPSS_MAX_PHYS_CHN_NUM);
}

static hi_s32 sample_fast_ae_start_venc(hi_venc_chn venc_chn[], hi_size venc_size[], size_t size, hi_u32 chn_num)
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

static hi_s32 sample_fast_ae_start_and_bind_venc(hi_vpss_grp vpss_grp[], sample_vpss_cfg *vpss_cfg,
    hi_venc_chn venc_chn[], size_t size, hi_u32 grp_num)
{
    hi_u32 i;
    hi_s32 ret;
    hi_size venc_size[VENC_MAX_CHN_NUM];

    if (grp_num > size) {
        return HI_FAILURE;
    }

    for (i = 0; i < VENC_MAX_CHN_NUM; i++) {
        venc_chn[i] = (hi_s32)i;
        if (i % 2 == 0) { /* 2: divisor */
            venc_size[i].width = vpss_cfg->chn_attr[0].width;
            venc_size[i].height = vpss_cfg->chn_attr[0].height;
        } else {
            venc_size[i].width = vpss_cfg->chn_attr[1].width;
            venc_size[i].height = vpss_cfg->chn_attr[1].height;
        }
    }

    ret = sample_fast_ae_start_venc(venc_chn, venc_size, size, grp_num * 2); /* 2 chns */
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }
    for (i = 0; i < grp_num; i++) {
        sample_comm_vpss_bind_venc(vpss_grp[i], HI_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        sample_comm_vpss_bind_venc(vpss_grp[i], HI_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
    }

    return HI_SUCCESS;
}

static void sample_fast_ae_get_sns_type(void)
{
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &g_vb_param.vb_size[1]);
}

static hi_s32 sample_fast_ae_start_route(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg, hi_s32 route_num)
{
    hi_s32 i, j;
    hi_vpss_grp vpss_grp[SAMPLE_VIE_MAX_ROUTE_NUM] = {0, 1, 2, 3};
    hi_venc_chn venc_chn[VENC_MAX_CHN_NUM];

    sample_fast_ae_get_sns_type();
    if (sample_fast_ae_sys_init() != HI_SUCCESS) {
        return HI_FAILURE;
    }

    for (i = 0; i < route_num; i++) {
        sample_comm_vi_bind_vpss(i, 0, vpss_grp[i], 0);
    }

    for (i = 0; i < route_num; i++) {
        if (sample_fast_ae_start_vpss(vpss_grp[i], vpss_cfg) != HI_SUCCESS) {
            goto start_vpss_failed;
        }
    }

    if (sample_fast_ae_start_and_bind_venc(vpss_grp, vpss_cfg, venc_chn, SAMPLE_VIE_MAX_ROUTE_NUM, route_num) !=
         HI_SUCCESS) {
        goto start_venc_failed;
    }

    for (i = 0; i < route_num; i++) {
        if (sample_comm_vi_start_vi(&vi_cfg[i]) != HI_SUCCESS) {
            goto start_vi_failed;
        }
    }

    return HI_SUCCESS;

start_vi_failed:
    for (j = i - 1; j >= 0; j--) {
        sample_comm_vi_stop_vi(&vi_cfg[j]);
    }
    for (i = 0; i < route_num; i++) {
        sample_comm_venc_stop(venc_chn[i]);
        sample_comm_vpss_un_bind_venc(vpss_grp[i], HI_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        sample_comm_vpss_un_bind_venc(vpss_grp[i], HI_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
    }
start_venc_failed:
start_vpss_failed:
    for (i = i - 1; i >= 0; i--) {
        sample_fast_ae_stop_vpss(vpss_grp[i]);
    }
    for (i = 0; i < route_num; i++) {
        sample_comm_vi_un_bind_vpss(i, 0, vpss_grp[i], 0);
    }
    sample_comm_sys_exit();
    return HI_FAILURE;
}

static hi_void sample_fast_ae_stop_venc(hi_venc_chn venc_chn[], size_t size, hi_u32 chn_num)
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

static hi_void sample_fast_ae_stop_and_unbind_venc(hi_vpss_grp vpss_grp[], size_t size, hi_u32 grp_num)
{
    hi_u32 i;
    hi_venc_chn venc_chn[VENC_MAX_CHN_NUM];

    if (grp_num > size) {
        return;
    }

    for (i = 0; i < VENC_MAX_CHN_NUM; i++) {
        venc_chn[i] = (hi_s32)i;
    }

    for (i = 0; i < grp_num; i++) {
        sample_comm_vpss_un_bind_venc(vpss_grp[i], HI_VPSS_CHN0, venc_chn[i * 2]); /* 2 chns */
        sample_comm_vpss_un_bind_venc(vpss_grp[i], HI_VPSS_CHN1, venc_chn[i * 2 + 1]); /* 2 chns */
    }

    sample_fast_ae_stop_venc(venc_chn, sizeof(venc_chn) / sizeof(venc_chn[0]), grp_num * 2); /* 2 chns */
}

static hi_void sample_fast_ae_stop_route(sample_vi_cfg *vi_cfg, hi_s32 route_num)
{
    hi_s32 i;
    hi_vpss_grp vpss_grp[SAMPLE_VIE_MAX_ROUTE_NUM] = {0, 1, 2, 3};

    sample_fast_ae_stop_and_unbind_venc(vpss_grp, SAMPLE_VIE_MAX_ROUTE_NUM, route_num);

    for (i = route_num - 1; i >= 0; i--) {
        sample_fast_ae_stop_vpss(vpss_grp[i]);
        sample_comm_vi_un_bind_vpss(i, 0, vpss_grp[i], 0);
        sample_comm_vi_stop_vi(&vi_cfg[i]);
    }
    sample_comm_sys_exit();
}

static hi_void sample_fast_ae_get_vi_vpss_mode(hi_bool is_wdr_mode)
{
    g_fast_ae_sys_cfg.mode_type = HI_VI_OFFLINE_VPSS_ONLINE;
    g_vb_param.blk_num[0] = 0; /* raw_vb num 0 */
    g_vb_param.blk_num[1] = 0; /* raw_vb num 0 */
}

static hi_void sample_fast_ae_vpss_get_wrap_cfg(sampe_sys_cfg *g_fast_ae_sys_cfg, sample_vpss_cfg *vpss_cfg)
{
    hi_u32 full_lines_std;
    if (g_fast_ae_sys_cfg->vpss_wrap_en) {
        if (SENSOR0_TYPE == SC4336P_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == OS04D10_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == GC4023_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1 || SENSOR0_TYPE == SC4336P_MIPI_3M_30FPS_10BIT) {
            full_lines_std = 1500; /* full_lines_std: 1500 */
        } else if (SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1585; /* full_lines_std: 1585 */
        } else if (SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT || SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1700; /* full_lines_std: 1700 */
        } else {
            g_fast_ae_sys_cfg->vpss_wrap_en = HI_FALSE;
            vpss_cfg->wrap_attr[0].enable = HI_FALSE;
            return;
        }
        (hi_void)sample_comm_vpss_get_wrap_cfg(vpss_cfg->chn_attr, g_fast_ae_sys_cfg->mode_type, full_lines_std,
            &vpss_cfg->wrap_attr[0]);
        g_fast_ae_sys_cfg->vpss_wrap_size = vpss_cfg->wrap_attr[0].buf_size;
    }
}

static hi_void sample_fast_ae_read_exp(hi_u32 *exposure, hi_u32 *lines_per500ms, hi_u32 *isp_dgain)
{
    hi_s32 ret;
    ot_isp_exp_info exp_info;
    ret = hi_mpi_isp_query_exposure_info(0, &exp_info);
    if (ret == HI_SUCCESS) {
        *exposure = exp_info.exposure;
        *lines_per500ms = exp_info.lines_per500ms;
        *isp_dgain = exp_info.isp_d_gain;
    } else {
        *exposure = 0;
        *lines_per500ms = 0;
        *isp_dgain = 0;
    }
}

static hi_void sample_fast_ae_read_awb(hi_u32 *awb_r, hi_u32 *awb_g, hi_u32 *awb_b)
{
    hi_s32 ret;
    ot_isp_wb_info wb_info;
    ret = hi_mpi_isp_query_wb_info(0, &wb_info);
    if (ret == HI_SUCCESS) {
        *awb_r = wb_info.r_gain;
        *awb_g = wb_info.gr_gain;
        *awb_b = wb_info.b_gain;
    } else {
        *awb_r = 0;
        *awb_g = 0;
        *awb_b = 0;
    }
}

static hi_void sample_fast_ae_isp_init()
{
    sample_sns_type sns_type = SENSOR0_TYPE;
    ot_isp_sns_obj *sns_obj = HI_NULL;
    ot_isp_init_attr isp_init_attr = {0};
    if (sns_type == SC4336P_MIPI_4M_30FPS_10BIT || sns_type == SC4336P_MIPI_3M_30FPS_10BIT) {
        sns_obj = &g_sns_sc4336p_obj;
    }
    isp_init_attr.exp_time = g_sns_exp;
    isp_init_attr.a_gain = g_sns_again;
    isp_init_attr.d_gain = g_sns_dgain;
    isp_init_attr.ispd_gain = g_isp_dgain;
    isp_init_attr.wb_r_gain = g_awb_rgain;
    isp_init_attr.wb_g_gain = g_awb_ggain;
    isp_init_attr.wb_b_gain = g_awb_bgain;
    isp_init_attr.exposure = g_exposure;
    isp_init_attr.lines_per500ms = g_lines_per500ms;
    isp_init_attr.quick_start_en = HI_TRUE;
    if (sns_obj != HI_NULL && sns_obj->pfn_set_init != HI_NULL) {
        sns_obj->pfn_set_init(0, &isp_init_attr);
    }
}

static hi_s32 sample_fast_ae_all_mode(hi_void)
{
    sample_vi_cfg vi_cfg[1];
    sample_vpss_cfg vpss_cfg;
    sample_sns_type sns_type = SENSOR0_TYPE;

    sample_fast_ae_get_vi_vpss_mode(HI_FALSE);
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg[0]);
    sample_fast_ae_get_pipe_wrap_line(vi_cfg, 1);
    sample_comm_vpss_get_default_vpss_cfg(&vpss_cfg, g_fast_ae_sys_cfg.vpss_wrap_en);
    sample_fast_ae_vpss_get_wrap_cfg(&g_fast_ae_sys_cfg, &vpss_cfg);

    if (sns_type != SC4336P_MIPI_4M_30FPS_10BIT && sns_type != SC4336P_MIPI_3M_30FPS_10BIT) {
        sample_print("sensor not support!\n");
        return HI_FAILURE;
    }
    sample_comm_vi_start_sensor(&vi_cfg[0].sns_info, &vi_cfg[0].mipi_info);
    vi_cfg[0].pipe_info[0].isp_quick_start = HI_TRUE;
    vi_cfg[0].sns_info.sns_clk_rst_en = HI_FALSE;
    sensor_sc4336p_init(g_sns_exp, g_sns_again, g_sns_dgain);
    sample_fast_ae_isp_init();

    if (sample_fast_ae_start_route(vi_cfg, &vpss_cfg, g_fast_ae_sys_cfg.route_num) != HI_SUCCESS) {
        sample_comm_vi_stop_mipi_rx(&vi_cfg->sns_info, &vi_cfg->mipi_info);
        return HI_FAILURE;
    }

    sample_get_char();
    sensor_sc4336p_read_exp(&g_sns_exp);
    sensor_sc4336p_read_gain(&g_sns_again, &g_sns_dgain);
    sample_fast_ae_read_exp(&g_exposure, &g_lines_per500ms, &g_isp_dgain);
    sample_fast_ae_read_awb(&g_awb_rgain, &g_awb_ggain, &g_awb_bgain);
    sensor_sc4336p_exit();
    printf("sns_exp = %u again = %u dgain = %u isp_dgain = %u exposure = %u lines_per500ms = %u "
        "awb_rgain = %u, awb_ggain = %u, awb_bgain = %u\n",
        g_sns_exp, g_sns_again, g_sns_dgain, g_isp_dgain, g_exposure, g_lines_per500ms,
        g_awb_rgain, g_awb_ggain, g_awb_bgain);

    sample_fast_ae_stop_route(vi_cfg, g_fast_ae_sys_cfg.route_num);
    return HI_SUCCESS;
}


static hi_void sample_fast_ae_handle_sig(hi_s32 signo)
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

static hi_s32 sample_fast_ae_execute_case(hi_u32 case_index)
{
    hi_s32 ret;

    switch (case_index) {
        case 0: /* 0 all mode route */
            ret = sample_fast_ae_all_mode();
            break;
        default:
            ret = HI_FAILURE;
            break;
    }

    return ret;
}

static hi_void sample_fast_ae_usage(const char *prg_name)
{
    printf("usage : %s <sns_exp> <sns_again> <sns_dgain> <isp_dgain> <exposure> "
        "<line_per500ms> <r_gain> <g_gain> <b_gain>\n", prg_name);
}

static td_s32 sample_fast_ae_param_check(hi_char *argv[])
{
    hi_u32 i, j;
    size_t len;

    for (i = 1; i < 10; i++) { /* 10 param */
        len = strlen(argv[i]);
        if (len > 10 || len == 0) { /* max len of u32 is 10 */
            return HI_FAILURE;
        }
        for (j = 0; j < len; j++) {
            if (!check_digit(argv[i][j])) {
                return HI_FAILURE;
            }
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

#ifndef __LITEOS__
    sample_register_sig_handler(sample_fast_ae_handle_sig);
#endif

    hi_char *para_stop;
    if (argc == 10) { /* 10 parameter */
        ret = sample_fast_ae_param_check(argv);
        if (ret != HI_SUCCESS) {
            sample_fast_ae_usage(argv[0]);
            return HI_FAILURE;
        }
        g_sns_exp = (hi_u32)strtol(argv[ARGV_AE_EXP], &para_stop, 10); /* 10 dec */
        g_sns_again = (hi_u32)strtol(argv[ARGV_AE_AGAIN], &para_stop, 10); /* 10 dec */
        g_sns_dgain = (hi_u32)strtol(argv[ARGV_AE_DGAIN], &para_stop, 10); /* 10 dec */
        g_isp_dgain = (hi_u32)strtol(argv[ARGV_AE_ISP_DGAIN], &para_stop, 10); /* 10 dec */
        g_exposure = (hi_u32)strtol(argv[ARGV_AE_EXPOSURE], &para_stop, 10); /* 10 dec */
        g_lines_per500ms = (hi_u32)strtol(argv[ARGV_AE_LINE_PER500MS], &para_stop, 10); /* 10 dec */
        g_awb_rgain = (hi_u32)strtol(argv[ARGV_AWB_RGAIN], &para_stop, 10); /* 10 dec */
        g_awb_ggain = (hi_u32)strtol(argv[ARGV_AWB_GGAIN], &para_stop, 10); /* 10 dec */
        g_awb_bgain = (hi_u32)strtol(argv[ARGV_AWB_BGAIN], &para_stop, 10); /* 10 dec */
    } else {
        sample_fast_ae_usage(argv[0]);
        return HI_FAILURE;
    }

    ret = sample_fast_ae_execute_case(0);
    if ((ret == HI_SUCCESS) && (g_sig_flag == 0)) {
        printf("\033[0;32mprogram exit normally!\033[0;39m\n");
    } else {
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    return ret;
}

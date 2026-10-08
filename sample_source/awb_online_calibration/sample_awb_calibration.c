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
#include <fcntl.h>
#include <sys/time.h>
#include <math.h>

#include "hi_mpi_isp.h"
#include "hi_common_isp.h"
#include "hi_mpi_ae.h"

#include "sample_comm.h"
static volatile sig_atomic_t g_sig_flag = 0;
#define VB_BLK_CNT 2
#define AWB_GAIN_MAX 4095

typedef struct {
    sample_vi_cfg vi_config;

    hi_vi_pipe    vi_pipe;
    hi_vi_chn     vi_chn;
    hi_venc_chn venc_chn;
}awb_cali_prev;

hi_s32 sample_vio_start_vi(sample_vi_cfg *vi_config)
{
    hi_s32  ret;

    ret = sample_comm_vi_start_vi(vi_config);
    if (HI_SUCCESS != ret) {
        sample_print("start vi failed!\n");
        return ret;
    }
    return ret;
}

static hi_s32 sample_vio_stop_vi(sample_vi_cfg *vi_config)
{
    sample_comm_vi_stop_vi(vi_config);

    return HI_SUCCESS;
}

static hi_void sample_vi_get_default_vb_config(hi_size *size, hi_vb_cfg *vb_cfg)
{
    hi_vb_calc_cfg calc_cfg;
    hi_pic_buf_attr buf_attr;

    (hi_void)memset_s(vb_cfg, sizeof(hi_vb_cfg), 0, sizeof(hi_vb_cfg));
    vb_cfg->max_pool_cnt = 128; /* 128 pool limit */

    buf_attr.width         = size->width;
    buf_attr.height        = size->height;
    buf_attr.align         = HI_DEFAULT_ALIGN;
    buf_attr.bit_width     = HI_DATA_BIT_WIDTH_8;
    buf_attr.pixel_format  = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422;
    buf_attr.compress_mode = HI_COMPRESS_MODE_NONE;
    buf_attr.video_format = HI_VIDEO_FORMAT_LINEAR;
    hi_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);

    vb_cfg->common_pool[0].blk_size = calc_cfg.vb_size;
    vb_cfg->common_pool[0].blk_cnt  = VB_BLK_CNT;
}

static hi_s32 sample_vio_sys_init(hi_vi_vpss_mode_type mode_type, hi_vi_aiisp_mode aiisp_mode)
{
    hi_s32 ret;
    hi_size size;
    hi_vb_cfg vb_cfg;
    hi_u32 supplement_config;

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &size);
    sample_vi_get_default_vb_config(&size, &vb_cfg);

    supplement_config = HI_VB_SUPPLEMENT_BNR_MOT_MASK;
    ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }

    ret = sample_comm_vi_set_vi_vpss_mode(mode_type, aiisp_mode);
    if (ret != HI_SUCCESS) {
        sample_comm_sys_exit();

        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

hi_s32 sample_awb_cali_start_prev(awb_cali_prev *awb_cali_prev)
{
    hi_s32             ret;

    sample_sns_type  sns_type;
    hi_size             in_size;
    hi_vi_vpss_mode_type mast_pipe_mode = HI_VI_ONLINE_VPSS_OFFLINE;
    hi_vi_aiisp_mode aiisp_mode = HI_VI_AIISP_MODE_DEFAULT;

    awb_cali_prev->vi_pipe              = 0;
    awb_cali_prev->vi_chn               = 0;
    awb_cali_prev->venc_chn             = 0;

    /************************************************
    get all sensors information
    *************************************************/
    ret = sample_vio_sys_init(mast_pipe_mode, aiisp_mode);
    if (ret != HI_SUCCESS) {
        goto sys_init_failed;
    }
    sns_type = SENSOR0_TYPE;
    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vi_get_default_vi_cfg(sns_type, &awb_cali_prev->vi_config);

    /************************************************
    init VI and VO
    *************************************************/
    ret = sample_vio_start_vi(&awb_cali_prev->vi_config);
    if (HI_SUCCESS != ret) {
        sample_print("sample_vio_start_vi failed witfh %d\n", ret);
        goto EXIT;
    }

    /************************************************
    bind VI and VO
    *************************************************/
    ret = sample_comm_vi_bind_venc(awb_cali_prev->vi_pipe, awb_cali_prev->vi_chn,
        awb_cali_prev->venc_chn);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_vi_bind_vo failed with %#x!\n", ret);
        goto EXIT1;
    }

    return ret;

EXIT1:
    sample_vio_stop_vi(&awb_cali_prev->vi_config);
EXIT:
    sample_comm_sys_exit();
sys_init_failed:

    return ret;
}

hi_s32 sample_awb_cali_stop_prev(awb_cali_prev *awb_cali_prev)
{
    hi_s32 ret;

    if (awb_cali_prev == HI_NULL) {
        sample_print("err: awb_cali_prev is NULL \n");
        return HI_FAILURE;
    }

    ret = sample_comm_vi_un_bind_venc(awb_cali_prev->vi_pipe, awb_cali_prev->vi_chn,
        awb_cali_prev->venc_chn);
    ret += sample_vio_stop_vi(&awb_cali_prev->vi_config);
    sample_comm_sys_exit();

    return ret;
}

void sample_awb_correction_usage(const char *s_prg_nm)
{
    printf("usage : %s <mode> <intf1> <intf2> <intf3>\n", s_prg_nm);
    printf("mode:\n");
    printf("\t 0) calculate sample gain.\n");
    printf("\t 1) adjust sample gain according to golden sample.\n");

    printf("intf1:\n");
    printf("\t the value of rgain of golden sample, valid in mode 1. range [0, 4095]\n");

    printf("intf2:\n");
    printf("\t the value of bgain of golden sample, valid in mode 1. range [0, 4095]\n");

    printf("intf3:\n");
    printf("\t the value of alpha ranging from 0 to 1024, 0 means no blending. valid in mode 1.\n");

    return;
}

static hi_s32 para_check_digit(hi_char *para, hi_u32 len)
{
    hi_u32 i;

    for (i = 0; i < len; i++) {
        if (!check_digit(para[i])) {
            return HI_FAILURE;
        }
    }
    return HI_SUCCESS;
}

hi_s32 awb_calib_check_param(hi_s32 argc, hi_char *argv[], hi_u32 *mode,
    hi_isp_awb_calibration_gain *awb_golden_gain, hi_u16 *alpha)
{
    hi_u32 i;
    hi_char *para_stop;

    if ((argc < 2 || argc > 5) || (strlen(argv[1]) != 1)) { /* 2 get sample 5 cal sample gain */
        return HI_FAILURE;
    }
    if ((argc < 5) && (*argv[1] == '1')) {      /* 5 cal sample gain */
        return HI_FAILURE;
    }

    switch (*argv[1]) {
    case '0':
        *mode = 0;
        break;

    case '1':
        *mode = 1;

        for (i = 2; i <= 4; i++) { /* check para 2, 3, 4 */
            if (strlen(argv[i]) > 4) { /* max value 4095, width 4 */
                return HI_FAILURE;
            }

            if (para_check_digit(argv[i], strlen(argv[i])) != HI_SUCCESS) {
                return HI_FAILURE;
            }
        }

        awb_golden_gain->avg_r_gain = (hi_u16)strtol(argv[2], &para_stop, 10); /* para2 golden rgain, 10 dec */
        awb_golden_gain->avg_b_gain = (hi_u16)strtol(argv[3], &para_stop, 10); /* para3 golden bgain, 10 dec */
        *alpha = (hi_u16)strtol(argv[4], &para_stop, 10); /* para4 blend coef of golden, 10 dec */

        if ((awb_golden_gain->avg_r_gain > AWB_GAIN_MAX) || (awb_golden_gain->avg_b_gain > AWB_GAIN_MAX)) {
            return HI_FAILURE;
        }

        if (*alpha > 1024) { /* blend ratio range [0, 1024] */
            return HI_FAILURE;
        }
        break;

    default:
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}


hi_bool awb_calib_check_ae_status(hi_vi_pipe vi_pipe, hi_u8 ac_freq)
{
    hi_u16 total_count = 0;
    hi_u16 stable_count = 0;
    hi_isp_exp_info exp_info;
    hi_isp_exposure_attr exp_attr;
    const hi_u32 stable_upper_limit = 20;
    const hi_u32 stable_low_limit = 5;

    hi_mpi_isp_get_exposure_attr(vi_pipe, &exp_attr);

    printf("set antiflicker enable and the value of frequency to 50_hz\n");
    exp_attr.auto_attr.antiflicker.enable = HI_TRUE;
    exp_attr.auto_attr.antiflicker.frequency = ac_freq;
    hi_mpi_isp_set_exposure_attr(vi_pipe, &exp_attr);

    do {
        hi_mpi_isp_query_exposure_info(vi_pipe, &exp_info);
        usleep(100000000 / div_0_to_1(exp_info.fps)); /* 100000000 is 1s * 100, fps normalize 100 */

        /* judge whether AE is stable */
        if (exp_info.hist_error > exp_attr.auto_attr.tolerance) {
            stable_count = 0;
        } else {
            stable_count++;
        }
        total_count++;
    }while ((stable_count < stable_low_limit) && (total_count < stable_upper_limit));

    if (stable_count >= stable_low_limit) {
        return HI_TRUE;
    } else {
        return HI_FALSE;
    }
}

hi_s32 awb_calib_get_corr_gain(hi_vi_pipe vi_pipe, hi_isp_awb_calibration_gain *awb_calib_gain,
    hi_isp_awb_calibration_gain *awb_golden_gain, hi_u32 mode, hi_u16 alpha)
{
    const hi_u32 blend_shift = 10; /* 2^10 = 1024 */
    hi_u16 beta = (1 << blend_shift) - alpha;

    hi_mpi_isp_get_lightbox_gain(vi_pipe, awb_calib_gain);

    /* adjust the value of rgain and bgain of sample according to golden sample */
    if (mode == 1) {
        awb_calib_gain->avg_r_gain = (hi_u16)(((hi_u32)awb_calib_gain->avg_r_gain * beta +
            (hi_u32)awb_golden_gain->avg_r_gain * alpha) >> blend_shift);
        awb_calib_gain->avg_b_gain = (hi_u16)(((hi_u32)awb_calib_gain->avg_b_gain * beta +
            (hi_u32)awb_golden_gain->avg_b_gain * alpha) >> blend_shift);
    }

    if (mode == 0) {
        printf("calculate sample gain:\n");
    } else if (mode == 1) {
        printf("adjust sample gain:\n");
    }
    printf("\tavg_rgain = %u, avg_bgain = %u\n", awb_calib_gain->avg_r_gain, awb_calib_gain->avg_b_gain);

    return HI_SUCCESS;
}

static hi_void sample_awb_handle_sig(hi_s32 signo)
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

static hi_void awb_getchar()
{
    if (g_sig_flag == 1) {
        return;
    }

    (hi_void)getchar();

    if (g_sig_flag == 1) {
        return;
    }
}

#ifdef __LITEOS__
hi_s32 app_main(hi_s32 argc, hi_char *argv[])
#else
hi_s32 main(hi_s32 argc, hi_char *argv[])
#endif
{
    const hi_vi_pipe vi_pipe = 0;
    hi_isp_awb_calibration_gain awb_golden_gain = {0};
    hi_u16 alpha = 0;
    hi_u32 mode = 0;
    hi_s32 ret;

    ret = awb_calib_check_param(argc, argv, &mode, &awb_golden_gain, &alpha);
    if (ret != HI_SUCCESS) {
        sample_print("the mode is invalid!\n");
        sample_awb_correction_usage(argv[0]);
        return HI_FAILURE;
    }
#ifndef __LITEOS__
    sample_register_sig_handler(sample_awb_handle_sig);
#endif

    awb_cali_prev awb_cali_prev;
    ret = sample_awb_cali_start_prev(&awb_cali_prev);
    if (ret == HI_SUCCESS) {
        sample_print("ISP is now running normally\n");
    } else {
        sample_print("ISP is not running normally!please check it\n");
        return HI_FAILURE;
    }
    printf("input anything to continue....\n");
    awb_getchar();

    if (awb_calib_check_ae_status(vi_pipe, 50) == HI_TRUE) { /* AC frequency is 50 */
        hi_isp_awb_calibration_gain awb_calib_gain;
        ret = awb_calib_get_corr_gain(vi_pipe, &awb_calib_gain, &awb_golden_gain, mode, alpha);
        goto EXIT;
    } else {
        printf("AE IS NOT STABLE,PLEASE WAIT");
        ret = HI_FAILURE;
        goto EXIT;
    }

EXIT:
    printf("input anything to continue....\n");
    awb_getchar();
    return sample_awb_cali_stop_prev(&awb_cali_prev);
}

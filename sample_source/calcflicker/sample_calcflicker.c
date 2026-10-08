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
#include <sys/prctl.h>
#include <sys/ioctl.h>

#include "hi_common_isp.h"
#include "hi_mpi_ae.h"
#include "hi_isp_debug.h"
#include "sample_comm.h"
#include "hi_mpi_isp.h"

static volatile sig_atomic_t g_sig_flag = 0;
#define VB_BLK_CNT 10
#define USE_FRAME_NUM 3
#define DET50_THRESHOLD 3
#define DET60_THRESHOLD 3
#define DET_TIME_OUT 5
#define DET_UNKNOW_THRESHOLD 4
#define DET_NONE_THRESHOLD 3
#define SLEEP_TIME 500000
#define FREQ50 50
#define FREQ60 60
#define FPS30 30.0
#define FPS25 25.0
#define ISO220 220
#define EXP4400 4400
#define EXP5500 5500
#define VPSS_CHN 2
#define DET_IMG_WIDTH 1920
#define DET_IMG_HEIGHT 1080

static volatile sig_atomic_t g_flicker_sig_flag = 0;

hi_bool g_vpss_chn_enable[HI_VPSS_MAX_PHYS_CHN_NUM] = {HI_TRUE, HI_TRUE, HI_FALSE};

typedef struct {
    sample_vi_cfg vi_config;
    hi_venc_chn venc_chn;
    sample_comm_venc_chn_param venc_param;
    hi_vi_pipe vi_pipe;
    hi_vi_chn vi_chn;
    hi_vpss_grp vpss_grp;
    hi_vpss_chn vpss_chn[VPSS_CHN];
    hi_vpss_grp_attr vpss_grp_attr;
    sample_vpss_chn_attr vpss_chn_attr;
}calcflicker_prev;

typedef struct flicker_cnt {
    hi_u8 det_50;
    hi_u8 det_60;
    hi_u8 det_unknow;
    hi_u8 det_none;
    hi_u8 try_50;
    hi_u8 try_60;
} flicker_cnt;

static hi_void flicker_getchar()
{
    printf("--------------- press enter key to get start! ---------------\n");
    if (g_flicker_sig_flag == 1) {
        return;
    }

    (hi_void)getchar();

    if (g_flicker_sig_flag == 1) {
        return;
    }
}

hi_s32 sample_calcflicker_start_vi(sample_vi_cfg *vi_cfg)
{
    hi_s32  ret;

    ret = sample_comm_vi_start_vi(vi_cfg);
    if (HI_SUCCESS != ret) {
        sample_print("SAMPLE_Calcflicker start vi failed!\n");
        return ret;
    }

    return ret;
}

static hi_s32 sample_calcflicker_stop_vi(sample_vi_cfg *vi_cfg)
{
    sample_comm_vi_stop_vi(vi_cfg);
    return HI_SUCCESS;
}

static hi_void sample_vi_get_default_vb_config(hi_size *size, hi_vb_cfg *vb_cfg)
{
    hi_vb_calc_cfg calc_cfg;
    hi_pic_buf_attr samlple_calflicker_buf_attr;

    (hi_void)memset_s(vb_cfg, sizeof(hi_vb_cfg), 0, sizeof(hi_vb_cfg));
    vb_cfg->max_pool_cnt = 128; /* 128 pool limit */
    samlple_calflicker_buf_attr.align         = HI_DEFAULT_ALIGN;
    samlple_calflicker_buf_attr.bit_width     = HI_DATA_BIT_WIDTH_8;
    samlple_calflicker_buf_attr.width         = size->width;
    samlple_calflicker_buf_attr.height        = size->height;
    samlple_calflicker_buf_attr.compress_mode = HI_COMPRESS_MODE_NONE;
    samlple_calflicker_buf_attr.video_format = HI_VIDEO_FORMAT_LINEAR;
    samlple_calflicker_buf_attr.pixel_format  = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    hi_common_get_pic_buf_cfg(&samlple_calflicker_buf_attr, &calc_cfg);

    vb_cfg->common_pool[0].blk_size = calc_cfg.vb_size;
    vb_cfg->common_pool[0].blk_cnt  = VB_BLK_CNT;
}

static hi_s32 sample_calcflicker_sys_init(hi_vi_vpss_mode_type mode_type, hi_vi_aiisp_mode aiisp_mode)
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

static hi_s32 sample_calcflicker_start_vpss(calcflicker_prev *calcflicker_prev, hi_size *in_size)
{
    hi_vpss_grp_attr *grp_attr = &calcflicker_prev->vpss_grp_attr;
    sample_vpss_chn_attr *vpss_chn_attr = &calcflicker_prev->vpss_chn_attr;
    hi_vpss_chn_attr *attr = HI_NULL;

    sample_comm_vpss_get_default_grp_attr(grp_attr);
    grp_attr->max_width  = in_size->width;
    grp_attr->max_height = in_size->height;
    attr = &vpss_chn_attr->chn_attr[0];
    sample_comm_vpss_get_default_chn_attr(attr);
    vpss_chn_attr->chn_attr[0].width  = in_size->width;
    vpss_chn_attr->chn_attr[0].height = in_size->height;
    attr = &vpss_chn_attr->chn_attr[1];
    sample_comm_vpss_get_default_chn_attr(attr);
    vpss_chn_attr->chn_attr[1].width  = DET_IMG_WIDTH;
    vpss_chn_attr->chn_attr[1].height = DET_IMG_HEIGHT;
    vpss_chn_attr->chn_attr[1].compress_mode = HI_COMPRESS_MODE_NONE;
    vpss_chn_attr->chn_attr[1].depth = 2; /* channel copy 2 */
    memcpy_s(vpss_chn_attr->chn_enable, sizeof(vpss_chn_attr->chn_enable),
             g_vpss_chn_enable, sizeof(vpss_chn_attr->chn_enable));
    vpss_chn_attr->chn_array_size = HI_VPSS_MAX_PHYS_CHN_NUM;
    return sample_common_vpss_start(calcflicker_prev->vpss_grp,
                                    &calcflicker_prev->vpss_grp_attr,
                                    &calcflicker_prev->vpss_chn_attr);
}
static hi_void sample_calcflicker_get_default_venc_param(sample_sns_type sns_type,
    sample_comm_venc_chn_param *venc_param)
{
    hi_size size;

    sample_comm_vi_get_size_by_sns_type(sns_type, &size);

    venc_param->frame_rate = 30;    // 30: frame rate
    venc_param->stats_time = 2;     // 2: default stats_time
    venc_param->gop = 60;           // 60: default gop
    venc_param->venc_size.width = size.width;
    venc_param->venc_size.height = size.height;
    venc_param->size = PIC_1080P;
    venc_param->profile = 0;
    venc_param->is_rcn_ref_share_buf = HI_FALSE;
    venc_param->gop_attr.gop_mode = HI_VENC_GOP_MODE_NORMAL_P;
    venc_param->gop_attr.normal_p.ip_qp_delta = 2;  // 2: default delta
    venc_param->type = HI_PT_H265;
    venc_param->rc_mode = SAMPLE_RC_CBR;
}

static hi_void sample_calcflicker_flicker_init(calcflicker_prev *flicker)
{
    flicker->vi_pipe              = 0;
    flicker->vi_chn               = 0;
    flicker->vpss_grp             = 0;
    flicker->vpss_chn[0]          = 0;
    flicker->vpss_chn[1]          = 1;
    flicker->venc_chn             = 0;
}

hi_s32 sample_calcflicker_start_prev(calcflicker_prev *flicker)
{
    sample_sns_type  sns_type = SENSOR0_TYPE;
    hi_size             in_size;
    sample_calcflicker_flicker_init(flicker);
    if (sample_calcflicker_sys_init(HI_VI_OFFLINE_VPSS_OFFLINE, HI_VI_AIISP_MODE_DEFAULT) != HI_SUCCESS) {
        goto sys_init_failed;
    }
    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vi_get_default_vi_cfg(sns_type, &flicker->vi_config);
    sample_calcflicker_get_default_venc_param(sns_type, &flicker->venc_param);
    if (sample_calcflicker_start_vi(&flicker->vi_config) != HI_SUCCESS) {
        sample_print("sample_calcflicker_start_vi failed\n");
        goto EXIT;
    }
    if (sample_calcflicker_start_vpss(flicker, &in_size) != HI_SUCCESS) {
        sample_print("sample_calcflicker_start_vpss failed\n");
        goto EXIT1;
    }
    if (sample_comm_venc_start(flicker->venc_chn, &flicker->venc_param) != HI_SUCCESS) {
        sample_print("sample_comm_venc_start failed\n");
        goto EXIT2;
    }
    if (sample_comm_vi_bind_vpss(flicker->vi_pipe, flicker->vi_chn,
                                 flicker->vpss_grp, flicker->vpss_chn[0]) != HI_SUCCESS) {
        sample_print("sample_comm_vi_bind_vpss failed\n");
        goto EXIT3;
    }
    if (sample_comm_vi_bind_venc(flicker->vi_pipe, flicker->vi_chn, flicker->venc_chn) != HI_SUCCESS) {
        sample_print("sample_comm_vpss_bind_venc failed!\n");
        goto EXIT4;
    }
    if ((sample_comm_venc_start_get_stream(&flicker->venc_chn, 1)) != HI_SUCCESS) {    // 1: chn num
        sample_print("sample_comm_venc_start_get_stream failed!\n");
        goto EXIT5;
    }
    return HI_SUCCESS;

EXIT5:
    sample_comm_vi_un_bind_venc(flicker->vi_pipe, flicker->vi_chn, flicker->venc_chn);

EXIT4:
    sample_comm_vi_un_bind_vpss(flicker->vi_pipe, flicker->vi_chn, flicker->vpss_grp, flicker->vpss_chn[0]);

EXIT3:
    sample_comm_venc_stop(flicker->venc_chn);

EXIT2:
    sample_common_vpss_stop(flicker->vpss_grp, flicker->vpss_chn_attr.chn_enable, HI_VPSS_MAX_PHYS_CHN_NUM);
EXIT1:
    sample_calcflicker_stop_vi(&flicker->vi_config);
EXIT:
    sample_comm_sys_exit();
sys_init_failed:
    return HI_FAILURE;
}

hi_s32 sample_calcflicker_stop_prev(calcflicker_prev *calcflicker_prev)
{
    hi_s32 ret;
    if (calcflicker_prev == HI_NULL) {
        sample_print("err: calcflicker_prev is NULL \n");
        return HI_FAILURE;
    }
    ret = sample_comm_venc_stop_get_stream(1);    // 1: chn num
    if (ret != HI_SUCCESS) {
        return ret;
    }

    ret = sample_comm_venc_stop(calcflicker_prev->venc_chn);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    ret = sample_comm_vi_un_bind_venc(calcflicker_prev->vi_pipe, calcflicker_prev->vi_chn, calcflicker_prev->venc_chn);
    if (ret != HI_SUCCESS) {
        return ret;
    }

    ret += sample_comm_vi_un_bind_vpss(calcflicker_prev->vi_pipe,
                                       calcflicker_prev->vi_chn,
                                       calcflicker_prev->vpss_grp,
                                       calcflicker_prev->vpss_chn[0]);
    ret += sample_common_vpss_stop(calcflicker_prev->vpss_grp,
                                   calcflicker_prev->vpss_chn_attr.chn_enable,
                                   HI_VPSS_MAX_PHYS_CHN_NUM);
    ret += sample_calcflicker_stop_vi(&calcflicker_prev->vi_config);

    sample_comm_sys_exit();
    return ret;
}

/******************************************************************************
* function : show usage
******************************************************************************/
void sample_calc_flicker_usage(const char *s_prg_nm)
{
    printf("usage : %s <index>\n", s_prg_nm);
    printf("index:\n");

    printf("\t 0)auto calculate flicker and anti_flicker.\n");
    printf("\t 1)only calculate flicker type for 1 times.\n");
    printf("\t 2)only calculate flicker type for 3 times.\n");

    printf("\t calc flicker normal -mode under wall mount.\n");
    printf("\t if you have any questions, please look at readme.txt!\n");
    return;
}

hi_s32 sample_calc_flicker_type(const calcflicker_prev *calcflicker_prev,
                                hi_isp_calc_flicker_output *output_param)
{
    hi_u16 i = 0;
    hi_u16 frame_num = USE_FRAME_NUM;
    hi_vi_pipe_attr vi_chn_attr = {0};
    hi_vpss_chn vpss_chn = 1;
    hi_vpss_chn_attr chn_attr;
    hi_vpss_grp_attr grp_attr;

    hi_s32 milli_sec = -1;
    hi_s32 ret = HI_SUCCESS;
    hi_video_frame_info frame[USE_FRAME_NUM] = {0};
    hi_isp_calc_flicker_input input_line;
    hi_vpss_grp grp = calcflicker_prev->vpss_grp;
    hi_isp_pub_attr pub_attr;
    ret = hi_mpi_vpss_get_chn_attr(grp, calcflicker_prev->vpss_chn[1], &chn_attr);
    ret += hi_mpi_vpss_get_grp_attr(grp, &grp_attr);
    ret += hi_mpi_vi_get_pipe_attr(calcflicker_prev->vi_pipe,  &vi_chn_attr);
    ret += hi_mpi_isp_get_pub_attr(calcflicker_prev->vi_pipe, &pub_attr);
    if (ret != HI_SUCCESS) {
        printf("get attr failed !!! in %s!\n", __FUNCTION__);
        return ret;
    }

    /* get frame buffer */
    for (i = 0; i < frame_num; i++) {
        hi_mpi_vpss_get_chn_frame(grp, vpss_chn, &frame[i], milli_sec);
    }
    input_line.lines_per_second = chn_attr.height * (td_u32)pub_attr.frame_rate;
    ret = hi_mpi_isp_calc_flicker_type(0, &input_line, output_param, frame, frame_num);
    if (ret != HI_SUCCESS) {
        printf("the state of calc type function error !!! in %s!\n", __FUNCTION__);
    }
    for (i = 0; i < frame_num; i++) {
        hi_mpi_vpss_release_chn_frame(grp, vpss_chn, &frame[i]);
    }
    return ret;
}

hi_void sample_calc_flicker_mpi_init(hi_vi_pipe vi_pipe,
                                     hi_isp_exposure_attr *exp_attr,
                                     hi_isp_exposure_attr *exp_attr_dft,
                                     hi_isp_exp_info *exp_info)
{
    hi_mpi_isp_get_exposure_attr(vi_pipe, exp_attr);
    hi_mpi_isp_get_exposure_attr(vi_pipe, exp_attr_dft);
    hi_mpi_isp_query_exposure_info(vi_pipe, exp_info);
}

hi_s32 sample_flicker_50hz_process(hi_vi_pipe vi_pipe, hi_isp_exp_info *exp_info, hi_isp_pub_attr *pub_attr,
                                   hi_isp_exposure_attr *exp_attr, flicker_cnt *cnt)
{
    hi_s32 ret = 0;
    cnt->det_50++;
    cnt->det_60 = 0;
    cnt->det_none = 0;
    cnt->det_unknow = 0;
    if (cnt->det_50 >= DET50_THRESHOLD) {
        if ((exp_info->exp_time < EXP5500) && (exp_info->iso < ISO220)) {
            pub_attr->frame_rate = FPS25;
            ret = hi_mpi_isp_set_pub_attr(vi_pipe, pub_attr);
            printf("----- set pub frame rate to 25 -----\n");
        } else {
            exp_attr->auto_attr.antiflicker.enable = HI_TRUE;
            exp_attr->auto_attr.antiflicker.frequency = FREQ50;
            ret = hi_mpi_isp_set_exposure_attr(vi_pipe, exp_attr);
            printf("----- set antiflicker frequency 50 hz -----\n");
        }
        usleep(SLEEP_TIME);
    }
    return ret;
}

hi_s32 sample_flicker_60hz_process(hi_vi_pipe vi_pipe, hi_isp_exp_info *exp_info, hi_isp_pub_attr *pub_attr,
                                   hi_isp_exposure_attr *exp_attr, flicker_cnt *cnt)
{
    hi_s32 ret = 0;
    cnt->det_50 = 0;
    cnt->det_60++;
    cnt->det_none = 0;
    cnt->det_unknow = 0;
    if (cnt->det_60 >= DET60_THRESHOLD) {
        if ((exp_info->exp_time < EXP4400) && (exp_info->iso < ISO220)) {
            pub_attr->frame_rate = FPS30;
            ret = hi_mpi_isp_set_pub_attr(vi_pipe, pub_attr);
            printf("----- set pub frame rate to 30 -----\n");
        } else {
            exp_attr->auto_attr.antiflicker.enable = HI_TRUE;
            exp_attr->auto_attr.antiflicker.frequency = FREQ60;
            ret = hi_mpi_isp_set_exposure_attr(vi_pipe, exp_attr);
            printf("----- set antiflicker frequency 60 hz -----\n");
        }
        usleep(SLEEP_TIME);
    }
    return ret;
}

hi_s32 sample_flicker_set_antiflicker(hi_vi_pipe vi_pipe, hi_isp_exposure_attr *exp_attr, hi_u8 freq)
{
    hi_s32 ret;
    printf("---- try to use %u_hz ----\n", freq);
    exp_attr->auto_attr.antiflicker.enable = HI_TRUE;
    exp_attr->auto_attr.antiflicker.frequency = freq;
    ret = hi_mpi_isp_set_exposure_attr(vi_pipe, exp_attr);
    usleep(SLEEP_TIME);
    return ret;
}

hi_s32 sample_flicker_unknow_process(hi_vi_pipe vi_pipe, hi_isp_exposure_attr *exp_attr, flicker_cnt *cnt)
{
    hi_s32 ret = 0;
    cnt->det_50 = 0;
    cnt->det_60 = 0;
    cnt->det_none = 0;
    cnt->det_unknow++;
    if (cnt->det_unknow >= DET_UNKNOW_THRESHOLD) {
        if (cnt->try_50 == 0) {
            ret = sample_flicker_set_antiflicker(vi_pipe, exp_attr, FREQ50);
            cnt->det_unknow = 0;
            cnt->try_50 = 1;
        } else if (cnt->try_60 == 0) {
            ret = sample_flicker_set_antiflicker(vi_pipe, exp_attr, FREQ60);
            cnt->det_unknow = 0;
            cnt->try_60 = 1;
        }
    }
    return ret;
}
hi_void sample_flicker_none_process(flicker_cnt *cnt)
{
    cnt->det_50 = 0;
    cnt->det_60 = 0;
    cnt->det_unknow = 0;
    cnt->det_none++;
}

hi_s32 sample_auto_calc_flicker_and_anti_flicker(const calcflicker_prev *calcflicker_prev)
{
    hi_u8 detect_time_out = FREQ50;
    flicker_cnt cnt = {0};
    hi_vi_pipe vi_pipe = 0;
    hi_s32 ret = HI_FAILURE;
    hi_isp_exposure_attr exp_attr, exp_attr_dft;
    hi_isp_pub_attr pub_attr;
    hi_isp_exp_info exp_info;
    hi_isp_calc_flicker_output output_param;
    sample_calc_flicker_mpi_init(vi_pipe, &exp_attr, &exp_attr_dft, &exp_info);
    while (detect_time_out--) {
        ret = sample_calc_flicker_type(calcflicker_prev, &output_param);
        hi_mpi_isp_query_exposure_info(vi_pipe, &exp_info);
        hi_mpi_isp_get_pub_attr(vi_pipe, &pub_attr);
        if (HI_ISP_FLICKER_TYPE_50HZ == output_param.flicker_type) {
            printf("##### FLICKER_TYPE_50HZ %u #####\n", detect_time_out);
            sample_flicker_50hz_process(vi_pipe, &exp_info, &pub_attr, &exp_attr, &cnt);
            if (cnt.det_50 > DET_TIME_OUT) {
                ret = hi_mpi_isp_set_exposure_attr(vi_pipe, &exp_attr_dft);
                break;
            }
        } else if (HI_ISP_FLICKER_TYPE_60HZ == output_param.flicker_type) {
            printf("##### FLICKER_TYPE_60HZ %u #####\n", detect_time_out);
            sample_flicker_60hz_process(vi_pipe, &exp_info, &pub_attr, &exp_attr, &cnt);
            if (cnt.det_60 > DET_TIME_OUT) {
                ret = hi_mpi_isp_set_exposure_attr(vi_pipe, &exp_attr_dft);
                break;
            }
        } else if (HI_ISP_FLICKER_TYPE_UNKNOW == output_param.flicker_type) {
            printf("##### FLICKER_TYPE_UNKNOW %u #####\n", detect_time_out);
            sample_flicker_unknow_process(vi_pipe, &exp_attr, &cnt);
            if (cnt.det_unknow >= DET_UNKNOW_THRESHOLD && cnt.try_50 == 1 && cnt.try_60 == 1) {
                ret = hi_mpi_isp_set_exposure_attr(vi_pipe, &exp_attr_dft);
                break;
            }
        } else if (HI_ISP_FLICKER_TYPE_NONE == output_param.flicker_type) {
            printf("##### FLICKER_TYPE_NONE %u #####\n", detect_time_out);
            sample_flicker_none_process(&cnt);
            if (cnt.det_none >= DET_UNKNOW_THRESHOLD) {
                printf("no flicker detect more than 5 time\n");
                break;
            }
        }
    }
    return ret;
}

hi_s32 sample_calc_flicker_normal(const calcflicker_prev *calcflicker_prev)
{
    hi_s32 ret;
    ret = sample_auto_calc_flicker_and_anti_flicker(calcflicker_prev);
    return ret;
}

hi_s32 sample_calc_flicker_3_times(const calcflicker_prev *calcflicker_prev)
{
    hi_s32 ret;
    hi_vi_pipe vi_pipe = 0;
    hi_isp_exp_info exp_info;
    hi_u16 i = 0;
    hi_isp_calc_flicker_output output_param;
    hi_mpi_isp_query_exposure_info(vi_pipe, &exp_info);

    for (i = 0; i < 3; i++) { /* test 3 time */
        ret = sample_calc_flicker_type(calcflicker_prev, &output_param);
        if (HI_ISP_FLICKER_TYPE_50HZ == output_param.flicker_type) {
            printf("time: %u, ##### FLICKER_TYPE_50HZ #####\n", i + 1);
        } else if (HI_ISP_FLICKER_TYPE_60HZ == output_param.flicker_type) {
            printf("time: %u, ##### FLICKER_TYPE_60HZ #####\n", i + 1);
        } else if (HI_ISP_FLICKER_TYPE_NONE == output_param.flicker_type) {
            printf("time: %u, ##### FLICKER_TYPE_NONE #####\n", i + 1);
        } else if (HI_ISP_FLICKER_TYPE_UNKNOW == output_param.flicker_type) {
            printf("time: %u, ##### FLICKER_TYPE_UNKNOW #####\n", i + 1);
        }
    }
    return ret;
}

hi_s32 sample_calc_flicker_1_times(calcflicker_prev *calcflicker_prev)
{
    hi_s32 ret;
    hi_vi_pipe vi_pipe = 0;
    hi_isp_exp_info exp_info;
    hi_isp_calc_flicker_output output_param;
    hi_mpi_isp_query_exposure_info(vi_pipe, &exp_info);
    ret = sample_calc_flicker_type(calcflicker_prev, &output_param);
    if (HI_ISP_FLICKER_TYPE_50HZ == output_param.flicker_type) {
        printf("\n##### FLICKER_TYPE_50HZ #####\n");
    } else if (HI_ISP_FLICKER_TYPE_60HZ == output_param.flicker_type) {
        printf("\n##### FLICKER_TYPE_60HZ #####\n");
    } else if (HI_ISP_FLICKER_TYPE_NONE == output_param.flicker_type) {
        printf("\n##### FLICKER_TYPE_NONE #####\n");
    } else if (HI_ISP_FLICKER_TYPE_UNKNOW == output_param.flicker_type) {
        printf("\n##### FLICKER_TYPE_UNKNOW #####\n");
    }
    return ret;
}

static hi_void sample_calcflicker_handle_sig(hi_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_flicker_sig_flag = 1;
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

/******************************************************************************
* function    : main()
* description : video fisheye preview sample
******************************************************************************/
#ifdef __LITEOS__
hi_s32 app_main(hi_s32 argc, hi_char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    hi_s32             ret        = HI_FAILURE;
    if (argc < 2 || argc > 2) {  /* 2 get sample 2 */
        sample_calc_flicker_usage(argv[0]);
        return HI_FAILURE;
    }
    if (!strncmp(argv[1], "-h", 2)) {  /* 2 get sample */
        sample_calc_flicker_usage(argv[0]);
        return HI_SUCCESS;
    }
    if (strlen(argv[1]) != 1  || !check_digit(argv[1][0]) || argv[1][0] > '2') { // 2 max idx
        sample_calc_flicker_usage(argv[0]);
        return HI_FAILURE;
    }

#ifndef __LITEOS__
    sample_register_sig_handler(sample_calcflicker_handle_sig);
#endif
    calcflicker_prev calcflicker_prev;
    ret = sample_calcflicker_start_prev(&calcflicker_prev);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }
    (hi_void)flicker_getchar();
    if (g_flicker_sig_flag == 0) {
        printf("\033[0;32mprogram cannot be terminated using Ctrl + C!\033[0;39m\n");
        switch (*argv[1]) {
                /* VI/VPSS - VO. embeded isp, phychn channel preview. */
            case '0':
                ret = sample_calc_flicker_normal(&calcflicker_prev);
                break;
            case '1':
                ret = sample_calc_flicker_1_times(&calcflicker_prev);
                break;
            case '2':
                ret = sample_calc_flicker_3_times(&calcflicker_prev);
                break;
            default:
                ret = HI_FAILURE;
                sample_print("the index is invalid!\n");
                sample_calc_flicker_usage(argv[0]);
                break;
        }
        if (ret == HI_FAILURE) {
            goto EXIT;
        }
        if (ret == HI_SUCCESS) {
            printf("\033[0;32mprogram exit normally!\033[0;39m\n");
        }
        sample_pause();
    }
EXIT:
    ret = sample_calcflicker_stop_prev(&calcflicker_prev);
    return ret;
}

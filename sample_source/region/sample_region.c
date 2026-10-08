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
#include <sys/ioctl.h>
#include <sys/prctl.h>

#include "sample_comm.h"

#define MM_BMP "./res/mm.bmp"

#define VB_NUM_PRE_CHN 8
#define RGN_VI_CHN   0
#define RGN_VI_PIPE  0
#define RGN_VPSS_GRP 0
#define RGN_VPSS_CHN 0
#define RGN_VENC_CHN 0

#define RGN_HANDLE_NUM_1 1
#define RGN_HANDLE_NUM_2 2
#define RGN_HANDLE_NUM_3 3
#define RGN_HANDLE_NUM_4 4
#define RGN_HANDLE_NUM_8 8

#define RGN_VI_CHN_NUM 1

#define RGN_VENC_LINE_MAX_NUM 20
#define RGN_VENC_LINE_THICK 8
#define RGN_VENC_LINE_INDEX0 0
#define RGN_VENC_LINE_INDEX1 1
#define RGN_VENC_LINE_INDEX2 2
#define RGN_VENC_LINE_INDEX3 3
#define RGN_VENC_LINE_INDEX4 4
#define RGN_VENC_LINE_INDEX5 5
#define RGN_VENC_LINE_INDEX6 6
#define RGN_VENC_LINE_INDEX7 7

#define RGN_VENC_LINE_DEFAULT_NUM 8
#define RGN_VENC_LINE_POINT_20  20
#define RGN_VENC_LINE_POINT_40  40
#define RGN_VENC_LINE_POINT_60  60
#define RGN_VENC_LINE_POINT_80  80
#define RGN_VENC_LINE_POINT_100 100
#define RGN_VENC_LINE_POINT_120 120
#define RGN_VENC_LINE_POINT_160 160

#define REGION_STOP_GET_VENC_STREAM (0x01L << 0)
#define REGION_UNBIND_VPSS_VENC     (0x01L << 1)
#define REGION_STOP_VENC            (0x01L << 2)
#define REGION_STOP_SEND_STREAM     (0x01L << 3)
#define REGION_STOP_VPSS            (0x01L << 4)
#define REGION_UNBIND_VI_VPSS       (0x01L << 5)
#define REGION_STOP_VI              (0x01L << 6)
#define REGION_UNBIND_VI_VENC       (0x01L << 7)
typedef hi_u32 region_stop_flag;

static hi_char *g_path_bmp;
static int g_rgn_sample_exit = 0;

static sampe_sys_cfg g_vio_sys_cfg = {
    .route_num = 1,
    .mode_type = HI_VI_ONLINE_VPSS_OFFLINE,
    .vi_fmu = {0},
    .vpss_fmu = {0},
    .vpss_wrap_en = HI_TRUE,
};

static sample_vi_cfg g_vi_cfg = { 0 };

static sample_vb_param g_vb_param = {
    .pixel_format = {HI_PIXEL_FORMAT_RGB_BAYER_12BPP, HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420},
    .compress_mode = {HI_COMPRESS_MODE_LINE, HI_COMPRESS_MODE_NONE},
    .video_format = {HI_VIDEO_FORMAT_LINEAR, HI_VIDEO_FORMAT_LINEAR},
    .blk_num = {0, 3}
};

/*
 * function: to process abnormal case
 */
static hi_void sample_region_handle_sig(hi_s32 signo)
{
    if ((signo == SIGINT) || (signo == SIGTERM)) {
        g_rgn_sample_exit = 1;
    }
}

static hi_void rgn_sample_pause(hi_void)
{
    if (g_rgn_sample_exit == 1) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        sample_comm_sys_exit();
        exit(-1);
    }

    sample_pause();

    if (g_rgn_sample_exit == 1) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        sample_comm_sys_exit();
        exit(-1);
    }
}

static hi_pic_size sample_region_get_pic_size_by_sns_type(sample_sns_type sns_type)
{
    switch (sns_type) {
        case SC4336P_MIPI_4M_30FPS_10BIT:
        case OS04D10_MIPI_4M_30FPS_10BIT:
        case GC4023_MIPI_4M_30FPS_10BIT:
        case SC431HAI_MIPI_4M_30FPS_10BIT:
        case SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1:
            return PIC_2560X1440;
        case SC450AI_MIPI_4M_30FPS_10BIT:
        case SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1:
            return PIC_2688X1520;
        case SC500AI_MIPI_5M_30FPS_10BIT:
        case SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1:
            return PIC_2880X1620;
        case SC4336P_MIPI_3M_30FPS_10BIT:
            return PIC_2304X1296;
        default:
            return PIC_1080P;
    }
}

static hi_s32 sample_region_start_venc(hi_void)
{
    hi_s32 ret;
    hi_venc_gop_mode gop_mode;
    hi_venc_gop_attr gop_attr;
    sample_comm_venc_chn_param venc_create_param;
    hi_venc_start_param start_param;

    gop_mode = HI_VENC_GOP_MODE_NORMAL_P;

    (hi_void)memset_s(&venc_create_param, sizeof(sample_comm_venc_chn_param), 0, sizeof(sample_comm_venc_chn_param));
    ret = sample_comm_venc_get_gop_attr(gop_mode, &gop_attr);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_venc_get_gop_attr failed!\n");
        return ret;
    }
    venc_create_param.type = HI_PT_H265;
    venc_create_param.gop = 60; /* 60:default gop val */
    venc_create_param.frame_rate = 30; /* 30:is a number */
    venc_create_param.stats_time = 2; /* 2:is a number */
    venc_create_param.rc_mode = SAMPLE_RC_CBR;
    venc_create_param.is_rcn_ref_share_buf = HI_TRUE;
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &venc_create_param.venc_size);
    venc_create_param.size = sample_region_get_pic_size_by_sns_type(SENSOR0_TYPE);

    (hi_void)memcpy_s(&venc_create_param.gop_attr, sizeof(hi_venc_gop_attr), &gop_attr, sizeof(hi_venc_gop_attr));

    /* step 1:  creat encode channel */
    ret = sample_comm_venc_create(RGN_VENC_CHN, &venc_create_param);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_venc_create failed with%#x! \n", ret);
        return HI_FAILURE;
    }
    /* step 2:  start recv venc pictures */
    start_param.recv_pic_num = -1;
    ret = hi_mpi_venc_start_chn(RGN_VENC_CHN, &start_param);
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_venc_start_recv_pic failed with%#x! \n", ret);
        ret = hi_mpi_venc_destroy_chn(RGN_VENC_CHN);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_venc_destroy_chn chn[%d] failed with %#x!\n", RGN_VENC_CHN, ret);
        }
    }
    return ret;
}

static hi_s32 sample_region_stop_venc(hi_void)
{
    return sample_comm_venc_stop(RGN_VENC_CHN);
}

static hi_s32 sample_region_start_get_venc_stream(hi_void)
{
    hi_s32 venc_chn[2] = {0, 1}; /* 2:VENC chn max num */

    return sample_comm_venc_start_get_stream(venc_chn, 1);
}

static hi_void sample_region_stop_get_venc_stream(hi_void)
{
    sample_comm_venc_stop_get_stream(1);
}

static hi_s32 sample_region_start_vi(hi_u32 chn_num)
{
    hi_s32 i;
    hi_s32 ret = HI_SUCCESS;
    hi_vb_cfg vb_cfg;
    sample_sns_type sns_type = SENSOR0_TYPE;
    hi_u32 supplement_config = HI_VB_SUPPLEMENT_BNR_MOT_MASK;

    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[0]);
    sample_comm_vi_get_size_by_sns_type(sns_type, &g_vb_param.vb_size[1]);
    sample_comm_sys_get_default_vb_cfg(&g_vb_param, &vb_cfg);
    if (sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_config) != HI_SUCCESS) {
        sample_print("sample_comm_sys_init_with_vb_supplement fail!\n");
        return HI_FAILURE;
    }

    if (sample_comm_vi_set_vi_vpss_mode(g_vio_sys_cfg.mode_type, HI_VI_AIISP_MODE_DEFAULT) != HI_SUCCESS) {
        sample_comm_sys_exit();
        sample_print("sample_comm_vi_set_vi_vpss_mode fail!\n");
        return HI_FAILURE;
    }

    sample_comm_vi_get_vi_cfg_by_fmu_mode(sns_type, g_vio_sys_cfg.vi_fmu[0], &g_vi_cfg);

    for (i = 0; i < g_vio_sys_cfg.route_num; i++) {
        ret = sample_comm_vi_start_vi(&g_vi_cfg);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_vi_start_vi fail, ret = %d!\n", ret);
            return ret;
        }
    }

    return ret;
}

static hi_void sample_region_vpss_get_wrap_cfg(sampe_sys_cfg *g_rgn_sys_cfg, sample_vpss_chn_attr *vpss_cfg)
{
    hi_u32 full_lines_std;
    if (g_rgn_sys_cfg->vpss_wrap_en) {
        if (SENSOR0_TYPE == SC4336P_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == OS04D10_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == GC4023_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT ||
            SENSOR0_TYPE == SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1 || SENSOR0_TYPE == SC4336P_MIPI_3M_30FPS_10BIT) {
            full_lines_std = 1500; /* full_lines_std: 1500 */
        } else if (SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT || SENSOR0_TYPE == SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1585; /* full_lines_std: 1585 */
        } else if (SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT || SENSOR0_TYPE == SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1) {
            full_lines_std = 1700; /* full_lines_std: 1700 */
        } else {
            g_rgn_sys_cfg->vpss_wrap_en = HI_FALSE;
            vpss_cfg->wrap_attr[0].enable = HI_FALSE;
            sample_print("vpss do not open wrap!\n");
            return;
        }
        (hi_void)sample_comm_vpss_get_wrap_cfg(vpss_cfg->chn_attr, g_rgn_sys_cfg->mode_type, full_lines_std,
            &vpss_cfg->wrap_attr[0]);
        g_rgn_sys_cfg->vpss_wrap_size = vpss_cfg->wrap_attr[0].buf_size;
    }
}

static hi_s32 sample_region_vpss_set_wrap_grp_int_attr(hi_vi_vpss_mode_type mode_type, hi_bool wrap_en,
    hi_u32 max_height)
{
    hi_s32 ret = HI_SUCCESS;
    hi_frame_interrupt_attr frame_int_attr;

    if (mode_type == HI_VI_ONLINE_VPSS_ONLINE && wrap_en) {
        frame_int_attr.interrupt_type = HI_FRAME_INTERRUPT_EARLY_END;
        frame_int_attr.early_line = max_height / 2; /* 2 half */
        ret = hi_mpi_vpss_set_grp_frame_interrupt_attr(0, &frame_int_attr);
        if (ret != HI_SUCCESS) {
            printf("hi_mpi_vpss_set_grp_frame_interrupt_attr is failed!\n");
        }
    }

    return ret;
}

static hi_s32 sample_region_common_start_vpss(hi_s32 vpss_grp, hi_s32 vpss_chn, const hi_size *size)
{
    hi_s32 ret;
    hi_vpss_grp_attr grp_attr;
    sample_vpss_chn_attr vpss_chn_attr = {0};

    sample_comm_vpss_get_default_grp_attr(&grp_attr);
    grp_attr.max_width = size->width;
    grp_attr.max_height = size->height;
    sample_comm_vpss_get_default_chn_attr(&vpss_chn_attr.chn_attr[vpss_chn]);
    vpss_chn_attr.chn_attr[vpss_chn].width = size->width;
    vpss_chn_attr.chn_attr[vpss_chn].height = size->height;
    vpss_chn_attr.chn_attr[vpss_chn].compress_mode = HI_COMPRESS_MODE_NONE;
    vpss_chn_attr.chn_enable[vpss_chn] = HI_TRUE;
    vpss_chn_attr.chn_array_size = HI_VPSS_MAX_PHYS_CHN_NUM;
    vpss_chn_attr.wrap_attr[vpss_chn].enable = g_vio_sys_cfg.vpss_wrap_en;
    sample_region_vpss_get_wrap_cfg(&g_vio_sys_cfg, &vpss_chn_attr);

    if (vpss_grp == 0) {
        ret = sample_region_vpss_set_wrap_grp_int_attr(g_vio_sys_cfg.mode_type, vpss_chn_attr.wrap_attr[0].enable,
            grp_attr.max_height);
        if (ret != HI_SUCCESS) {
            return ret;
        }
    }

    ret = sample_common_vpss_start(vpss_grp, &grp_attr, &vpss_chn_attr);
    if (ret != HI_SUCCESS) {
        sample_print("failed with %#x!\n", ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_s32 sample_region_stop_vpss()
{
    hi_s32 ret;
    hi_bool chn_enable[HI_VPSS_MAX_PHYS_CHN_NUM] = {0};

    ret = sample_common_vpss_stop(RGN_VPSS_GRP, chn_enable, HI_VPSS_MAX_PHYS_CHN_NUM);
    if (ret != HI_SUCCESS) {
        sample_print("failed with %#x!\n", ret);
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_s32 sample_region_start_vpss(hi_void)
{
    hi_s32 ret;
    hi_size size;

    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &size);
    ret = sample_region_common_start_vpss(RGN_VPSS_GRP, RGN_VPSS_CHN, &size);
    if (ret != HI_SUCCESS) {
        sample_print("start vpss failed with 0x%x!\n", ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_s32 sample_region_do_stop(region_stop_flag flag)
{
    hi_s32 ret = HI_SUCCESS;

    if (flag & REGION_STOP_GET_VENC_STREAM) {
        sample_region_stop_get_venc_stream();
    }

    if (flag & REGION_UNBIND_VPSS_VENC) {
        ret = sample_comm_vpss_un_bind_venc(RGN_VPSS_GRP, RGN_VPSS_CHN, RGN_VENC_CHN);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_vpss_un_bind_venc failed!\n");
        }
    }

    if (flag & REGION_UNBIND_VI_VENC) {
        ret = sample_comm_vi_un_bind_venc(RGN_VI_PIPE, RGN_VI_CHN, RGN_VENC_CHN);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_vi_un_bind_venc failed!\n");
        }
    }

    if (flag & REGION_STOP_VENC) {
        ret = sample_region_stop_venc();
        if (ret != HI_SUCCESS) {
            sample_print("stop venc failed!\n");
        }
    }

    if (flag & REGION_UNBIND_VI_VPSS) {
        ret = sample_comm_vi_un_bind_vpss(RGN_VI_PIPE, RGN_VI_CHN, RGN_VPSS_GRP, RGN_VPSS_CHN);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_vi_un_bind_vpss failed!\n");
        }
    }

    if (flag & REGION_STOP_VPSS) {
        ret = sample_region_stop_vpss();
        if (ret != HI_SUCCESS) {
            sample_print("stop vpss failed!\n");
        }
    }

    if (flag & REGION_STOP_VI) {
        sample_comm_vi_stop_vi(&g_vi_cfg);
    }

    return ret;
}

static hi_s32 sample_region_do_destroy(hi_s32 handle_num, hi_rgn_type type, hi_mpp_chn *chn, region_op_flag flag)
{
    hi_s32 ret = HI_SUCCESS;

    if (flag & REGION_OP_CHN) {
        ret = sample_comm_region_detach(handle_num, type, chn, flag);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_region_detach failed!\n");
        }
    } else if (flag & REGION_OP_DEV) {
        ret = sample_comm_region_detach(handle_num, type, chn, flag);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_region_detach failed!\n");
        }
    }

    if (flag & REGION_DESTROY) {
        ret = sample_comm_region_destroy(handle_num, type);
        if (ret != HI_SUCCESS) {
            sample_print("sample_comm_region_destroy failed!\n");
        }
    }

    return ret;
}

static hi_s32 sample_region_vi_venc_start(hi_void)
{
    hi_s32 ret;

    ret = sample_region_start_vi(RGN_VI_CHN_NUM);
    if (ret != HI_SUCCESS) {
        sample_print("start vi failed with 0x%x!\n", ret);
        return ret;
    }
    ret = sample_region_start_venc();
    if (ret != HI_SUCCESS) {
        sample_print("start venc failed!\n");
        return sample_region_do_stop(REGION_STOP_VI);
    }
    ret = sample_comm_vi_bind_venc(RGN_VI_PIPE, RGN_VI_CHN, RGN_VENC_CHN);
    if (ret != HI_SUCCESS) {
        sample_print("bind vi venc failed!\n");
        return sample_region_do_stop(REGION_STOP_VI | REGION_STOP_VENC);
    }
    ret = sample_region_start_get_venc_stream();
    if (ret != HI_SUCCESS) {
        sample_print("start get venc stream failed!\n");
        g_rgn_sample_exit = 1;
        return sample_region_do_stop(REGION_STOP_VI | REGION_STOP_VENC | REGION_UNBIND_VI_VENC);
    }

    return ret;
}

static hi_s32 sample_region_vi_vpss_venc_start(hi_void)
{
    hi_s32 ret;

    ret = sample_region_start_vi(RGN_VI_CHN_NUM);
    if (ret != HI_SUCCESS) {
        sample_print("start vi failed with 0x%x!\n", ret);
        return ret;
    }
    ret = sample_region_start_vpss();
    if (ret != HI_SUCCESS) {
        sample_print("start vpss failed with 0x%x!\n", ret);
        return sample_region_do_stop(REGION_STOP_VI);
    }
    ret = sample_comm_vi_bind_vpss(RGN_VI_PIPE, RGN_VI_CHN, RGN_VPSS_GRP, RGN_VPSS_CHN);
    if (ret != HI_SUCCESS) {
        sample_print("vi_bind_multi_vpss 0x%x!\n", ret);
        return sample_region_do_stop(REGION_STOP_VPSS | REGION_STOP_VI);
    }
    ret = sample_region_start_venc();
    if (ret != HI_SUCCESS) {
        sample_print("start venc failed!\n");
        return sample_region_do_stop(REGION_STOP_VI | REGION_STOP_VPSS | REGION_UNBIND_VI_VPSS);
    }
    ret = sample_comm_vpss_bind_venc(RGN_VPSS_GRP, RGN_VPSS_CHN, RGN_VENC_CHN);
    if (ret != HI_SUCCESS) {
        sample_print("bind vpss venc failed!\n");
        return sample_region_do_stop(REGION_STOP_VI | REGION_STOP_VPSS | REGION_UNBIND_VI_VPSS | REGION_STOP_VENC);
    }
    ret = sample_region_start_get_venc_stream();
    if (ret != HI_SUCCESS) {
        sample_print("start get venc stream failed!\n");
        g_rgn_sample_exit = 1;
        return sample_region_do_stop(REGION_STOP_VI | REGION_STOP_VPSS | REGION_UNBIND_VI_VPSS
            | REGION_STOP_VENC | REGION_UNBIND_VPSS_VENC);
    }

    return ret;
}

static hi_s32 sample_region_vi_vpss_venc_end(hi_void)
{
    hi_s32 ret;

    sample_region_stop_get_venc_stream();
    ret = sample_comm_vpss_un_bind_venc(RGN_VPSS_GRP, RGN_VPSS_CHN, RGN_VENC_CHN);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_vpss_un_bind_venc failed!\n");
        return ret;
    }
    ret = sample_region_stop_venc();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_stop_venc failed!\n");
        return ret;
    }
    ret = sample_comm_vi_un_bind_vpss(RGN_VI_PIPE, RGN_VI_CHN, RGN_VPSS_GRP, RGN_VPSS_CHN);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_vi_un_bind_vpss failed with 0x%x!\n", ret);
        return ret;
    }
    ret = sample_region_stop_vpss();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_stop_vpss failed with 0x%x!\n", ret);
        return ret;
    }
    ret = sample_region_do_stop(REGION_STOP_VI);
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_do_stop vi failed with 0x%x!\n", ret);
        return ret;
    }
    sample_comm_sys_exit();

    return HI_SUCCESS;
}

static hi_s32 sample_region_vi_venc_end(hi_void)
{
    hi_s32 ret;

    sample_region_stop_get_venc_stream();
    ret = sample_comm_vi_un_bind_venc(RGN_VI_PIPE, RGN_VI_CHN, RGN_VENC_CHN);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_vi_un_bind_venc failed!\n");
        return ret;
    }
    ret = sample_region_stop_venc();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_stop_venc failed!\n");
        return ret;
    }
    ret = sample_region_do_stop(REGION_STOP_VI);
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_do_stop vi failed with 0x%x!\n", ret);
        return ret;
    }
    sample_comm_sys_exit();

    return HI_SUCCESS;
}

static hi_s32 sample_region_do_destroy_vi_vpss_venc(hi_s32 handle_num, hi_rgn_type type, hi_mpp_chn *chn,
    region_op_flag flag)
{
    hi_s32 ret;

    (hi_void)sample_region_do_destroy(handle_num, type, chn, flag);
    ret = sample_region_vi_vpss_venc_end();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_mpp_vi_vpss_venc_end failed!\n");
    }

    return ret;
}

static hi_s32 sample_region_do_destroy_vi_venc(hi_s32 handle_num, hi_rgn_type type, hi_mpp_chn *chn,
    region_op_flag flag)
{
    hi_s32 ret;

    (hi_void)sample_region_do_destroy(handle_num, type, chn, flag);
    ret = sample_region_vi_venc_end();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_vi_venc_end failed!\n");
    }

    return ret;
}

static hi_s32 sample_region_vi_vpss_venc(hi_s32 handle_num, hi_rgn_type type, hi_mpp_chn *chn,
    region_op_flag op_flag)
{
    hi_s32 i, ret, min_handle;

    rgn_check_handle_num_return(handle_num);

    ret = sample_region_vi_vpss_venc_start();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_vi_vpss_venc_start failed!\n");
        return ret;
    }
    ret = sample_comm_region_create(handle_num, type);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_region_create failed!\n");
        return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_DESTROY);
    }

    ret = sample_comm_region_attach(handle_num, type, chn, op_flag);
    if (ret != HI_SUCCESS) {
        if (op_flag & REGION_OP_CHN) {
            sample_print("sample_comm_region_attach failed!\n");
            return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
        } else if (op_flag & REGION_OP_DEV) {
            sample_print("sample_comm_region_attach_to_dev failed!\n");
            return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_DEV | REGION_DESTROY);
        }
    }

    min_handle = sample_comm_region_get_min_handle(type);
    if (sample_comm_check_min(min_handle) != HI_SUCCESS) {
        sample_print("min_handle(%d) should be in [0, %d).\n", min_handle, HI_RGN_HANDLE_MAX);
        return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
    }
    if (type == HI_RGN_OVERLAY) {
        for (i = min_handle; i < min_handle + handle_num; i++) {
            ret = sample_comm_region_set_bit_map(i, g_path_bmp);
            if (ret != HI_SUCCESS) {
                sample_print("sample_comm_region_set_bit_map failed!\n");
                return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
            }
        }
    } else if (type == HI_RGN_OVERLAYEX) {
        for (i = min_handle; i < min_handle + handle_num; i++) {
            ret = sample_comm_region_get_up_canvas(i, g_path_bmp);
            if (ret != HI_SUCCESS) {
                sample_print("sample_comm_region_get_up_canvas failed!\n");
                return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
            }
        }
    }
    rgn_sample_pause();

    return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
}

static hi_void sample_region_set_line_idx0_to_idx3(sample_osd_line *line)
{
    line[RGN_VENC_LINE_INDEX0].point1.x = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX0].point1.y = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX0].point2.x = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX0].point2.y = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX0].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX0].color = 1; /* 1: clut color index */
    line[RGN_VENC_LINE_INDEX0].is_display = HI_TRUE;

    line[RGN_VENC_LINE_INDEX1].point1.x = RGN_VENC_LINE_POINT_40;
    line[RGN_VENC_LINE_INDEX1].point1.y = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX1].point2.x = RGN_VENC_LINE_POINT_100;
    line[RGN_VENC_LINE_INDEX1].point2.y = 0;
    line[RGN_VENC_LINE_INDEX1].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX1].color = 2; /* 2: clut color index */
    line[RGN_VENC_LINE_INDEX1].is_display = HI_TRUE;

    line[RGN_VENC_LINE_INDEX2].point1.x = RGN_VENC_LINE_POINT_100;
    line[RGN_VENC_LINE_INDEX2].point1.y = 0;
    line[RGN_VENC_LINE_INDEX2].point2.x = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX2].point2.y = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX2].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX2].color = 3; /* 3: clut color index */
    line[RGN_VENC_LINE_INDEX2].is_display = HI_TRUE;

    line[RGN_VENC_LINE_INDEX3].point1.x = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX3].point1.y = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX3].point2.x = RGN_VENC_LINE_POINT_160;
    line[RGN_VENC_LINE_INDEX3].point2.y = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX3].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX3].color = 1; /* 1: clut color index */
    line[RGN_VENC_LINE_INDEX3].is_display = HI_TRUE;
}

static hi_void sample_region_set_line_idx4_to_idx7(sample_osd_line *line)
{
    line[RGN_VENC_LINE_INDEX4].point1.x = RGN_VENC_LINE_POINT_160;
    line[RGN_VENC_LINE_INDEX4].point1.y = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX4].point2.x = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX4].point2.y = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX4].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX4].color = 2; /* 2: clut color index */
    line[RGN_VENC_LINE_INDEX4].is_display = HI_TRUE;

    line[RGN_VENC_LINE_INDEX5].point1.x = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX5].point1.y = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX5].point2.x = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX5].point2.y = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX5].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX5].color = 2; /* 2: clut color index */
    line[RGN_VENC_LINE_INDEX5].is_display = HI_TRUE;

    line[RGN_VENC_LINE_INDEX6].point1.x = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX6].point1.y = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX6].point2.x = 0;
    line[RGN_VENC_LINE_INDEX6].point2.y = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX6].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX6].color = 3; /* 3: clut color index */
    line[RGN_VENC_LINE_INDEX6].is_display = HI_TRUE;

    line[RGN_VENC_LINE_INDEX7].point1.x = RGN_VENC_LINE_POINT_60;
    line[RGN_VENC_LINE_INDEX7].point1.y = RGN_VENC_LINE_POINT_20;
    line[RGN_VENC_LINE_INDEX7].point2.x = RGN_VENC_LINE_POINT_80;
    line[RGN_VENC_LINE_INDEX7].point2.y = RGN_VENC_LINE_POINT_120;
    line[RGN_VENC_LINE_INDEX7].thick = RGN_VENC_LINE_THICK;
    line[RGN_VENC_LINE_INDEX7].color = 1; /* 1: clut color index */
    line[RGN_VENC_LINE_INDEX7].is_display = HI_TRUE;
}

static hi_void sample_region_set_line_config(sample_osd_line *line, hi_u32 line_num)
{
    if (line_num > RGN_VENC_LINE_MAX_NUM || line_num < RGN_VENC_LINE_DEFAULT_NUM) {
        sample_print("line_num(%u) should in [%d, %d]!\n",
            line_num, RGN_VENC_LINE_DEFAULT_NUM, RGN_VENC_LINE_MAX_NUM);
        return;
    }
    sample_region_set_line_idx0_to_idx3(line);
    sample_region_set_line_idx4_to_idx7(line);
}

static hi_s32 sample_region_do_osd_drawline(hi_s32 handle_num, hi_rgn_type type)
{
    hi_s32 i;
    hi_s32 ret;
    hi_s32 min_handle;
    sample_osd_line line[RGN_VENC_LINE_MAX_NUM] = {0};
    hi_bool is_set_bmp = HI_TRUE;

    min_handle = sample_comm_region_get_min_handle(type);
    if (sample_comm_check_min(min_handle) != HI_SUCCESS) {
        sample_print("min_handle(%d) should be in [0, %d).\n", min_handle, HI_RGN_HANDLE_MAX);
        return HI_FAILURE;
    }

    for (i = min_handle; i < min_handle + handle_num; i++) {
        is_set_bmp = !is_set_bmp;
        (hi_void)memset_s(line, sizeof(sample_osd_line) * RGN_VENC_LINE_MAX_NUM,
            0, sizeof(sample_osd_line) * RGN_VENC_LINE_MAX_NUM);
        sample_region_set_line_config(line, RGN_VENC_LINE_DEFAULT_NUM);
        if (is_set_bmp) {
            ret = sample_comm_region_drawline_set_bit_map(i, line, RGN_VENC_LINE_DEFAULT_NUM);
            if (ret != HI_SUCCESS) {
                sample_print("sample_comm_region_drawline_set_bit_map failed!\n");
                return ret;
            }
        } else {
            ret = sample_comm_region_drawline_get_canvas(i, line, RGN_VENC_LINE_DEFAULT_NUM);
            if (ret != HI_SUCCESS) {
                sample_print("sample_comm_region_drawline_get_canvas failed!\n");
                return ret;
            }
        }
    }

    return ret;
}

static hi_s32 sample_region_vi_vpss_venc_drawline(hi_s32 handle_num, hi_rgn_type type, hi_mpp_chn *chn,
    region_op_flag op_flag)
{
    hi_s32 ret;

    rgn_check_handle_num_return(handle_num);

    ret = sample_region_vi_vpss_venc_start();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_vi_vpss_venc_start failed!\n");
        return ret;
    }
    ret = sample_comm_region_create(handle_num, type);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_region_create failed!\n");
        return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_DESTROY);
    }

    ret = sample_comm_region_attach(handle_num, type, chn, op_flag);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_region_attach failed!\n");
        return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
    }

    if (type == HI_RGN_OVERLAY) {
        ret = sample_region_do_osd_drawline(handle_num, type);
        if (ret != HI_SUCCESS) {
            return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
        }
    }
    rgn_sample_pause();

    return sample_region_do_destroy_vi_vpss_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
}

static hi_s32 sample_region_vi_venc(hi_s32 handle_num, hi_rgn_type type, hi_mpp_chn *chn,
    region_op_flag op_flag)
{
    hi_s32 ret;

    rgn_check_handle_num_return(handle_num);

    ret = sample_region_vi_venc_start();
    if (ret != HI_SUCCESS) {
        sample_print("sample_region_vi_venc_start failed!\n");
        return ret;
    }
    ret = sample_comm_region_create(handle_num, type);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_region_create failed!\n");
        return sample_region_do_destroy_vi_venc(handle_num, type, chn, REGION_DESTROY);
    }

    ret = sample_comm_region_attach(handle_num, type, chn, op_flag);
    if (ret != HI_SUCCESS) {
        sample_print("sample_comm_region_attach failed!\n");
        return sample_region_do_destroy_vi_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
    }

    rgn_sample_pause();

    return sample_region_do_destroy_vi_venc(handle_num, type, chn, REGION_OP_CHN | REGION_DESTROY);
}

/* 0 vi coverex */
static hi_s32 sample_region_vi_coverex(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;

    handle_num = RGN_HANDLE_NUM_3;
    type = HI_RGN_COVEREX;
    chn.mod_id = HI_ID_VI;
    chn.dev_id = 0;
    chn.chn_id = 0;

    return sample_region_vi_venc(handle_num, type, &chn, REGION_OP_CHN);
}

/* 1 venc overlay */
static hi_s32 sample_region_venc_overlay(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;
    handle_num = RGN_HANDLE_NUM_8;
    type = HI_RGN_OVERLAY;
    chn.mod_id = HI_ID_VENC;
    chn.dev_id = 0;
    chn.chn_id = 0;
    g_path_bmp = MM_BMP;
    sample_comm_set_osd_drawline(HI_FALSE);
    return sample_region_vi_vpss_venc(handle_num, type, &chn, REGION_OP_CHN);
}

/* 2 vpss grp cover */
static hi_s32 sample_region_vpss_grp_cover(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;

    handle_num = RGN_HANDLE_NUM_1;
    type = HI_RGN_COVER;
    chn.mod_id = HI_ID_VPSS;
    chn.dev_id = 0;
    chn.chn_id = 0;

    return sample_region_vi_vpss_venc(handle_num, type, &chn, REGION_OP_DEV);
}

/* 3 vpss chn cover */
static hi_s32 sample_region_vpss_chn_cover(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;

    handle_num = RGN_HANDLE_NUM_2;
    type = HI_RGN_COVER;
    chn.mod_id = HI_ID_VPSS;
    chn.dev_id = 0;
    chn.chn_id = 0;

    return sample_region_vi_vpss_venc(handle_num, type, &chn, REGION_OP_CHN);
}

/* 4 vpss chn corner rect */
static hi_s32 sample_region_vpss_chn_corner_rect(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;

    handle_num = RGN_HANDLE_NUM_2;
    type = HI_RGN_CORNER_RECT;
    chn.mod_id = HI_ID_VPSS;
    chn.dev_id = 0;
    chn.chn_id = 0;

    return sample_region_vi_vpss_venc(handle_num, type, &chn, REGION_OP_CHN);
}

/* 5 vpss chn corner rectex */
static hi_s32 sample_region_vpss_chn_corner_rectex(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;

    handle_num = RGN_HANDLE_NUM_2;
    type = HI_RGN_CORNER_RECTEX;
    chn.mod_id = HI_ID_VPSS;
    chn.dev_id = 0;
    chn.chn_id = 0;

    return sample_region_vi_vpss_venc(handle_num, type, &chn, REGION_OP_CHN);
}

/* 6 vpss chn lineex */
static hi_s32 sample_region_vpss_chn_lineex(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;

    handle_num = RGN_HANDLE_NUM_4;
    type = HI_RGN_LINEEX;
    chn.mod_id = HI_ID_VPSS;
    chn.dev_id = 0;
    chn.chn_id = 0;

    return sample_region_vi_vpss_venc(handle_num, type, &chn, REGION_OP_CHN);
}

/* 7 venc overlay draw line */
static hi_s32 sample_region_venc_overlay_drawline(hi_void)
{
    hi_s32 handle_num;
    hi_rgn_type type;
    hi_mpp_chn chn;
    handle_num = RGN_HANDLE_NUM_8;
    type = HI_RGN_OVERLAY;
    chn.mod_id = HI_ID_VENC;
    chn.dev_id = 0;
    chn.chn_id = 0;
    g_path_bmp = MM_BMP;
    sample_comm_set_osd_drawline(HI_TRUE);
    return sample_region_vi_vpss_venc_drawline(handle_num, type, &chn, REGION_OP_CHN);
}

/*
 * function : show usage
 */
static hi_void sample_region_usage(const char *s_prg_nm)
{
    printf("usage : %s <index> \n", s_prg_nm);
    printf("index:\n");
    printf("\t  0)VI COVEREX.\n");
    printf("\t  1)VENC OVERLAY.\n");
    printf("\t  2)VPSS GRP COVER.\n");
    printf("\t  3)VPSS CHN COVER.\n");
    printf("\t  4)VPSS CHN CORNER_RECT.\n");
    printf("\t  5)VPSS CHN CORNER_RECTEX.\n");
    printf("\t  6)VPSS CHN LINEEX.\n");
    printf("\t  7)VENC OVERLAY DRAW LINE.\n");
}

static hi_s32 rgn_get_index_start(hi_s32 index)
{
    hi_s32 ret;

    if (index > 0 && index <= 7) { /* 7 online_online */
        g_vio_sys_cfg.mode_type = HI_VI_ONLINE_VPSS_ONLINE;
    }

    switch (index) {
        case 0: /* 0 vi coverex */
            ret = sample_region_vi_coverex();
            break;
        case 1: /* 1 venc overlay */
            ret = sample_region_venc_overlay();
            break;
        case 2: /* 2 vpss grp cover */
            ret = sample_region_vpss_grp_cover();
            break;
        case 3: /* 3 vpss chn cover */
            ret = sample_region_vpss_chn_cover();
            break;
        case 4: /* 4 vpss chn corner rect */
            ret = sample_region_vpss_chn_corner_rect();
            break;
        case 5: /* 5 vpss chn corner rectex */
            g_vio_sys_cfg.vpss_wrap_en = HI_FALSE;
            ret = sample_region_vpss_chn_corner_rectex();
            break;
        case 6: /* 6 vpss chn lineex */
            g_vio_sys_cfg.vpss_wrap_en = HI_FALSE;
            ret = sample_region_vpss_chn_lineex();
            break;
        case 7: /* 7 venc overlay draw line */
            ret = sample_region_venc_overlay_drawline();
            break;
        default:
            ret = HI_FAILURE;
            break;
    }

    return ret;
}

#ifdef __LITEOS__
int app_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    hi_s32 ret = HI_FAILURE;
    hi_slong index;
#ifndef __LITEOS__
    struct sigaction sa;
#endif

    if (argc != 2) { /* 2: arg num */
        sample_region_usage(argv[0]);
        return HI_FAILURE;
    }
    if (!strncmp(argv[1], "-h", 2)) { /* 2: arg num */
        sample_region_usage(argv[0]);
        return HI_SUCCESS;
    }
    if (strlen(argv[1]) > 2 || strlen(argv[1]) == 0 || !check_digit(argv[1][0]) || /* 2:arg len */
        (strlen(argv[1]) == 2 && (!check_digit(argv[1][1]) || argv[1][0] == '0'))) { /* 2: num width */
        sample_print("the index is invalid!\n");
        sample_region_usage(argv[0]);
        return HI_FAILURE;
    }

#ifndef __LITEOS__
    (hi_void)memset_s(&sa, sizeof(struct sigaction), 0, sizeof(struct sigaction));
    sa.sa_handler = sample_region_handle_sig;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif

    index = strtol(argv[1], HI_NULL, 10); /* 10: base */
    if (!index && strncmp(argv[1], "0", 1)) {
        index = -1;
    }
    if (index > 7 || index < 0) { /* 7:max index */
        sample_print("the index is invalid!\n");
        sample_region_usage(argv[0]);
    } else {
        ret = rgn_get_index_start(index);
    }
    if (ret == HI_SUCCESS) {
        sample_print("program exit normally!\n");
    } else {
        sample_print("program exit abnormally!\n");
    }

    return (ret);
}
/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include "uvc_media.h"
#include <unistd.h>
#include "log.h"
#include "hi_common_vb.h"
#include "hi_buffer.h"
#include "sample_comm.h"
#include "sample_venc.h"
#include "ot_camera.h"
#include "hi_mpi_isp.h"
#include "hi_mpi_ae.h"
#include "hi_mpi_awb.h"
#include "uvc.h"
#include "hi_common_uvc.h"
#include "hi_mpi_uvc.h"

static uvc_media_cfg g_media_cfg = {0};
static hi_bool g_is_media_start = HI_FALSE;
static encoder_property g_encoder_property = {0};
static hi_uvc_data_input_mode g_data_input_mode = HI_UVC_MPP_BIND_UVC;

static hi_s32 sample_comm_vpss_bind_uvc(hi_vpss_grp vpss_grp, hi_vpss_chn vpss_chn, hi_uvc_chn uvc_chn)
{
    hi_mpp_chn src_chn;
    hi_mpp_chn dest_chn;

    src_chn.mod_id = HI_ID_VPSS;
    src_chn.dev_id = vpss_grp;
    src_chn.chn_id = vpss_chn;

    dest_chn.mod_id = HI_ID_UVC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = uvc_chn;

    check_return(hi_mpi_sys_bind(&src_chn, &dest_chn), "hi_mpi_sys_bind(VPSS-UVC)");

    return HI_SUCCESS;
}

static hi_s32 sample_comm_vpss_un_bind_uvc(hi_vpss_grp vpss_grp, hi_vpss_chn vpss_chn, hi_uvc_chn uvc_chn)
{
    hi_mpp_chn src_chn;
    hi_mpp_chn dest_chn;

    src_chn.mod_id = HI_ID_VPSS;
    src_chn.dev_id = vpss_grp;
    src_chn.chn_id = vpss_chn;

    dest_chn.mod_id = HI_ID_UVC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = uvc_chn;

    check_return(hi_mpi_sys_unbind(&src_chn, &dest_chn), "hi_mpi_sys_unbind(VPSS-UVC)");

    return HI_SUCCESS;
}

static hi_s32 sample_comm_venc_bind_uvc(hi_venc_chn venc_chn, hi_uvc_chn uvc_chn)
{
    hi_mpp_chn src_chn;
    hi_mpp_chn dest_chn;

    src_chn.mod_id = HI_ID_VENC;
    src_chn.dev_id = 0;
    src_chn.chn_id = venc_chn;

    dest_chn.mod_id = HI_ID_UVC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = uvc_chn;

    check_return(hi_mpi_sys_bind(&src_chn, &dest_chn), "hi_mpi_sys_bind(VENC-UVC)");

    return HI_SUCCESS;
}

static hi_s32 sample_comm_venc_un_bind_uvc(hi_venc_chn venc_chn, hi_uvc_chn uvc_chn)
{
    hi_mpp_chn src_chn;
    hi_mpp_chn dest_chn;

    src_chn.mod_id = HI_ID_VENC;
    src_chn.dev_id = 0;
    src_chn.chn_id = venc_chn;

    dest_chn.mod_id = HI_ID_UVC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = uvc_chn;

    check_return(hi_mpi_sys_unbind(&src_chn, &dest_chn), "hi_mpi_sys_unbind(VENC-UVC)");

    return HI_SUCCESS;
}

static hi_void sample_uvc_get_default_pic_buf_attr(hi_size *pic_size, hi_pic_buf_attr *buf_attr)
{
    buf_attr->width         = pic_size->width;
    buf_attr->height        = pic_size->height;
    buf_attr->bit_width     = HI_DATA_BIT_WIDTH_8;
    buf_attr->pixel_format  = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422;
    buf_attr->compress_mode = HI_COMPRESS_MODE_SEG;
    buf_attr->align         = HI_DEFAULT_ALIGN;
    buf_attr->video_format  = HI_VIDEO_FORMAT_LINEAR;
}

static hi_void sample_uvc_get_sensor_size(sample_sns_type sns_type, hi_size *sensor_size)
{
    hi_isp_pub_attr pub_attr;
    (hi_void)sample_comm_isp_get_pub_attr_by_sns(sns_type, &pub_attr); /* it return success or default */
    sensor_size->width = pub_attr.sns_size.width;
    sensor_size->height = pub_attr.sns_size.height;
}

static hi_bool sample_uvc_is_need_venc(unsigned int format)
{
    if ((format != VIDEO_IMG_FORMAT_YUYV) && (format != VIDEO_IMG_FORMAT_YUV420) &&
        (format != VIDEO_IMG_FORMAT_NV21) && (format != VIDEO_IMG_FORMAT_NV12)) {
        return HI_TRUE;
    } else {
        return HI_FALSE;
    }
}

static hi_s32 sample_uvc_sys_init(sample_sns_type sns_type, hi_u32 supplement_cfg)
{
    hi_size              sensor_size;
    hi_vb_cfg            vb_cfg = {0};
    hi_u32               blk_size;
    hi_pic_buf_attr      buf_attr;
    hi_vb_supplement_cfg vb_supplement_cfg;
    hi_s32               ret;

    sample_uvc_get_sensor_size(sns_type, &sensor_size);
    sample_uvc_get_default_pic_buf_attr(&sensor_size, &buf_attr);

    vb_cfg.max_pool_cnt = 1;
    blk_size = hi_common_get_pic_buf_size(&buf_attr);
    vb_cfg.common_pool[0].blk_size = blk_size;
    vb_cfg.common_pool[0].blk_cnt = 4;  /* 6 VB blk cnt yanst  */
    vb_cfg.common_pool[0].remap_mode = HI_VB_REMAP_MODE_CACHED;

    vb_supplement_cfg.supplement_cfg = supplement_cfg;
    ret = hi_mpi_vb_set_supplement_cfg(&vb_supplement_cfg);
    if (ret != HI_SUCCESS) {
        sample_print("set vb cfg failed!\n");
        return HI_FAILURE;
    }

    ret = sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement_cfg);

    return ret;
}

static hi_void sample_uvc_get_vi_vpss_mode(hi_vi_vpss_mode_type vi_vpss_mode_type, hi_vi_vpss_mode *vi_vpss_mode)
{
    hi_u32 i;

    for (i = 0; i < HI_VI_MAX_PIPE_NUM; i++) {
        vi_vpss_mode->mode[i] = HI_VI_OFFLINE_VPSS_OFFLINE;
    }

    if (vi_vpss_mode_type == HI_VI_ONLINE_VPSS_ONLINE) {
        vi_vpss_mode->mode[0] = HI_VI_ONLINE_VPSS_ONLINE;
    }
}

static hi_s32 sample_uvc_vi_init(uvc_media_cfg *media_cfg)
{
    hi_s32 ret;
    hi_vi_vpss_mode vi_vpss_mode;

    sample_comm_vi_get_default_vi_cfg(media_cfg->sns_type, &media_cfg->vi_cfg);
    sample_uvc_get_vi_vpss_mode(media_cfg->vi_vpss_mode_type, &vi_vpss_mode);

    ret = hi_mpi_sys_set_vi_vpss_mode(&vi_vpss_mode);
    if (ret != HI_SUCCESS) {
        sample_print("set vi vpss mode failed!\n");
        return HI_FAILURE;
    }

    ret = hi_mpi_sys_set_vi_aiisp_mode(0, media_cfg->aiisp_mode);
    if (ret != HI_SUCCESS) {
        sample_print("set vi video mode failed!\n");
        return HI_FAILURE;
    }

    for (hi_s32 i = 0; i < HI_VI_MAX_PHYS_PIPE_NUM; i++) {
        if (media_cfg->vi_cfg.pipe_info[i].pipe_need_start == HI_TRUE) {
            media_cfg->vi_cfg.pipe_info[i].nr_attr.enable = HI_FALSE;  /* disable 3dnr */
            if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
                media_cfg->vi_cfg.pipe_info[i].chn_info[0].chn_attr.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422;
            } else {
                media_cfg->vi_cfg.pipe_info[i].chn_info[0].chn_attr.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
            }
        }
    }
    ret = sample_comm_vi_start_vi(&media_cfg->vi_cfg);

    return ret;
}

static hi_void sample_uvc_get_uvc_chn_attr(hi_uvc_chn uvc_chn, hi_uvc_chn_attr* uvc_chn_attr)
{
    if (g_encoder_property.format == VIDEO_IMG_FORMAT_MJPEG) {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_MJPEG;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_H264) {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_H264;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_H265) {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_H265;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_NV12) {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_NV12;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_NV21) {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_NV21;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_YUYV;
    } else {
        uvc_chn_attr->uvc_format = HI_UVC_FORMAT_BUTT;
    }
    uvc_chn_attr->data_mode = g_data_input_mode;
    if (g_data_input_mode == HI_UVC_SEND_USER_STREAM) {
        uvc_chn_attr->buffer_size = 1536; /* 1536 ring buffer size for copy user stream to uvc module */
    } else {
        uvc_chn_attr->buffer_size = 0;
    }
}

static hi_s32 sample_uvc_chn_init(hi_uvc_chn uvc_chn)
{
    hi_s32 ret;
    hi_uvc_chn_attr uvc_chn_attr = {0};

    sample_uvc_get_uvc_chn_attr(uvc_chn, &uvc_chn_attr);
    ret = hi_mpi_uvc_create_chn(uvc_chn, &uvc_chn_attr);
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_uvc_create_chn(chn:%d) failed with %#x!\n", uvc_chn, ret);
        return HI_FAILURE;
    }

    ret = hi_mpi_uvc_start_chn(uvc_chn);
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_uvc_start_chn failed with %#x\n", ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_s32 sample_uvc_chn_deinit(hi_uvc_chn uvc_chn)
{
    hi_s32 ret;

    ret = hi_mpi_uvc_stop_chn(uvc_chn);
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_uvc_stop_chn failed with %#x\n", ret);
        return HI_FAILURE;
    }

    ret = hi_mpi_uvc_destroy_chn(uvc_chn);
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_uvc_destroy_chn(chn:%d) failed with %#x!\n", uvc_chn, ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_void sample_uvc_get_default_vpss_ext_chn_attr(hi_vpss_ext_chn_attr *vpss_ext_chn_attr)
{
    vpss_ext_chn_attr->bind_chn = HI_VPSS_CHN0;
    vpss_ext_chn_attr->src_type = HI_EXT_CHN_SRC_TYPE_TAIL;
    vpss_ext_chn_attr->width = 1920;     /* 1920: default width */
    vpss_ext_chn_attr->height = 1080;    /* 1080: default height */
    vpss_ext_chn_attr->depth = 0;
    vpss_ext_chn_attr->video_format = HI_VIDEO_FORMAT_LINEAR;
    vpss_ext_chn_attr->dynamic_range = HI_DYNAMIC_RANGE_SDR8;
    vpss_ext_chn_attr->pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422;
    vpss_ext_chn_attr->compress_mode = HI_COMPRESS_MODE_NONE;
    vpss_ext_chn_attr->frame_rate.src_frame_rate = -1;
    vpss_ext_chn_attr->frame_rate.dst_frame_rate = -1;
    if (g_data_input_mode == HI_UVC_SEND_YUV_FRAME && g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
        vpss_ext_chn_attr->depth = 1;
    }
}

static hi_s32 sample_uvc_vpss_init(uvc_media_cfg *media_cfg)
{
    hi_s32 ret;
    hi_low_delay_info low_delay_info;
    hi_vpss_chn vpss_ext_chn = HI_VPSS_MAX_PHYS_CHN_NUM;
    hi_vpss_ext_chn_attr vpss_ext_chn_attr;
    hi_bool is_need_venc = sample_uvc_is_need_venc(g_encoder_property.format);
    sample_vpss_chn_attr vpss_chn_attr = {0};

    memcpy_s(&vpss_chn_attr.chn_attr[0], sizeof(hi_vpss_chn_attr) * HI_VPSS_MAX_PHYS_CHN_NUM,
        media_cfg->vpss_chn_attr, sizeof(hi_vpss_chn_attr) * HI_VPSS_MAX_PHYS_CHN_NUM);
    memcpy_s(vpss_chn_attr.chn_enable, sizeof(hi_bool) * HI_VPSS_MAX_PHYS_CHN_NUM,
        media_cfg->vpss_chn_enable, sizeof(hi_bool) * HI_VPSS_MAX_PHYS_CHN_NUM);
    vpss_chn_attr.chn_array_size = HI_VPSS_MAX_PHYS_CHN_NUM;
    ret = sample_common_vpss_start(media_cfg->vpss_grp, &media_cfg->vpss_grp_attr, &vpss_chn_attr);
    if (ret != HI_SUCCESS) {
        sample_print("start vpss failed!\n");
        return HI_FAILURE;
    }

    if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
        sample_uvc_get_default_vpss_ext_chn_attr(&vpss_ext_chn_attr);
        vpss_ext_chn_attr.width = media_cfg->vpss_chn_attr[0].width;
        vpss_ext_chn_attr.height = media_cfg->vpss_chn_attr[0].height;
        vpss_ext_chn_attr.pixel_format = HI_PIXEL_FORMAT_VY1UY0_PACKAGE_422;
        ret = hi_mpi_vpss_set_ext_chn_attr(media_cfg->vpss_grp, vpss_ext_chn, &vpss_ext_chn_attr);
        if (ret != HI_SUCCESS) {
            sample_print("set vpss ext_chn%d attr failed!\n", vpss_ext_chn);
            goto vpss_fail;
        }

        ret = hi_mpi_vpss_enable_chn(media_cfg->vpss_grp, vpss_ext_chn);
        if (ret != HI_SUCCESS) {
            sample_print("enable vpss ext chn%d failed!\n", vpss_ext_chn);
            goto vpss_fail;
        }
    }

    if (is_need_venc == HI_TRUE) {
        low_delay_info.enable = HI_TRUE;
        low_delay_info.line_cnt = 16;       /* 16: low delay line */
        low_delay_info.one_buf_en = HI_FALSE;
        ret = hi_mpi_vpss_set_chn_low_delay(media_cfg->vpss_grp, media_cfg->vpss_chn, &low_delay_info);
        if (ret != HI_SUCCESS) {
            sample_print("set vpss chn low delay failed!\n");
            goto vpss_fail;
        }
    }

    return HI_SUCCESS;

vpss_fail:
   sample_common_vpss_stop(media_cfg->vpss_grp, media_cfg->vpss_chn_enable, HI_VPSS_MAX_PHYS_CHN_NUM);
   return HI_FAILURE;
}

static hi_void sample_uvc_vpss_de_init(uvc_media_cfg *media_cfg)
{
    if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
        (hi_void)hi_mpi_vpss_disable_chn(media_cfg->vpss_grp, HI_VPSS_MAX_PHYS_CHN_NUM);
    }
    (hi_void)sample_common_vpss_stop(media_cfg->vpss_grp, media_cfg->vpss_chn_enable, HI_VPSS_MAX_PHYS_CHN_NUM);
}

static hi_void sample_venc_set_one_stream_buf(hi_void)
{
    hi_s32 ret;
    hi_venc_mod_param mod_param = {0};
    mod_param.mod_type = HI_VENC_MOD_H265;
    ret = hi_mpi_venc_get_mod_param(&mod_param);
    if (ret != HI_SUCCESS) {
        sample_print("venc_get_mod_param 265 ret %#x\n", ret);
    }
    mod_param.h265_mod_param.one_stream_buf = 1;
    ret = hi_mpi_venc_set_mod_param(&mod_param);
    if (ret != HI_SUCCESS) {
        sample_print("venc_set_mod_param 265 ret %#x\n", ret);
    }

    (hi_void)memset_s(&mod_param, sizeof(hi_venc_mod_param), 0, sizeof(hi_venc_mod_param));
    mod_param.mod_type = HI_VENC_MOD_H264;
    ret = hi_mpi_venc_get_mod_param(&mod_param);
    if (ret != HI_SUCCESS) {
        sample_print("venc_get_mod_param 264 ret %#x\n", ret);
    }
    mod_param.h264_mod_param.one_stream_buf = 1;
    ret = hi_mpi_venc_set_mod_param(&mod_param);
    if (ret != HI_SUCCESS) {
        sample_print("venc_set_mod_param 264 ret %#x\n", ret);
    }

    (hi_void)memset_s(&mod_param, sizeof(hi_venc_mod_param), 0, sizeof(hi_venc_mod_param));
    mod_param.mod_type = HI_VENC_MOD_JPEG;
    ret = hi_mpi_venc_get_mod_param(&mod_param);
    if (ret != HI_SUCCESS) {
        sample_print("venc_get_mod_param jpeg ret %#x\n", ret);
    }
    mod_param.jpeg_mod_param.one_stream_buf = 1;
    ret = hi_mpi_venc_set_mod_param(&mod_param);
    if (ret != HI_SUCCESS) {
        sample_print("venc_set_mod_param jpeg ret %#x\n", ret);
    }
}

static hi_s32 sample_uvc_venc_init(uvc_media_cfg *media_cfg)
{
    hi_s32 ret;
    hi_venc_chn venc_chn = media_cfg->venc_chn;

    if (media_cfg->venc_chn_param.type == HI_PT_H264) {
        media_cfg->venc_chn_param.rc_mode = SAMPLE_RC_AVBR;
    }

    sample_venc_set_one_stream_buf();

    ret = sample_comm_venc_start(venc_chn, &media_cfg->venc_chn_param);
    if (ret != HI_SUCCESS) {
        sample_print("start venc failed!\n");
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

static hi_void sample_uvc_get_default_venc_chn_param(sample_comm_venc_chn_param *venc_chn_param)
{
    venc_chn_param->frame_rate = 30;            /* 30: default frame rate */
    venc_chn_param->stats_time = 2;             /* 2: default stats_time */
    venc_chn_param->gop = 60;                   /* 60: default gop */
    venc_chn_param->venc_size.width = 1920;     /* 1920: default width */
    venc_chn_param->venc_size.height = 1080;    /* 1080: default height */
    venc_chn_param->profile = 0;
    venc_chn_param->is_rcn_ref_share_buf = HI_FALSE;
    venc_chn_param->gop_attr.gop_mode = HI_VENC_GOP_MODE_NORMAL_P;
    venc_chn_param->gop_attr.normal_p.ip_qp_delta = 2;  /* 2: default ip_qp_delta */
    venc_chn_param->type = HI_PT_H264;
    venc_chn_param->rc_mode = SAMPLE_RC_CBR;
}

static hi_void sample_uvc_media_cfg(uvc_media_cfg *media_cfg)
{
    hi_u32 i;
    hi_size sensor_size;

    media_cfg->sns_type = SENSOR0_TYPE;
    media_cfg->aiisp_mode = HI_VI_AIISP_MODE_DEFAULT;
    media_cfg->vi_vpss_mode_type = HI_VI_ONLINE_VPSS_ONLINE;

    sample_uvc_get_sensor_size(media_cfg->sns_type, &sensor_size);

    sample_comm_vpss_get_default_grp_attr(&media_cfg->vpss_grp_attr);
    media_cfg->vpss_grp_attr.max_width = sensor_size.width;
    media_cfg->vpss_grp_attr.max_height = sensor_size.height;
    if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
        media_cfg->vpss_grp_attr.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422;
    } else {
        media_cfg->vpss_grp_attr.pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    }

    for (i = 0; i < HI_VPSS_MAX_PHYS_CHN_NUM; i++) {
        sample_comm_vpss_get_default_chn_attr(&media_cfg->vpss_chn_attr[i]);
        media_cfg->vpss_chn_attr[i].width = sensor_size.width;
        media_cfg->vpss_chn_attr[i].height = sensor_size.height;
        media_cfg->vpss_chn_enable[i] = HI_FALSE;
        media_cfg->vpss_chn_attr[i].compress_mode = HI_COMPRESS_MODE_NONE;
    }
    media_cfg->vpss_chn = HI_VPSS_CHN0;
    media_cfg->vpss_chn_enable[HI_VPSS_CHN0] = HI_TRUE;

    sample_uvc_get_default_venc_chn_param(&media_cfg->venc_chn_param);

    media_cfg->supplement_cfg = HI_VB_SUPPLEMENT_BNR_MOT_MASK;
}

static hi_s32 sample_uvc_init_and_bind_vi_vpss(uvc_media_cfg *media_cfg)
{
    hi_s32 ret = sample_uvc_vi_init(media_cfg);
    if (ret != HI_SUCCESS) {
        sample_print("vi init failed!\n");
        return HI_FAILURE;
    }

    ret = sample_uvc_vpss_init(media_cfg);
    if (ret != HI_SUCCESS) {
        sample_print("vpss init failed!\n");
        goto vi_exit;
    }

    ret = sample_comm_vi_bind_vpss(media_cfg->vi_pipe, media_cfg->vi_chn, media_cfg->vpss_grp, media_cfg->vpss_chn);
    if (ret != HI_SUCCESS) {
        sample_print("vi bind vpss failed!\n");
        goto vpss_exit;
    }
    return HI_SUCCESS;

vpss_exit:
    sample_uvc_vpss_de_init(media_cfg);
vi_exit:
    sample_comm_vi_stop_vi(&media_cfg->vi_cfg);
    return HI_FAILURE;
}

static hi_s32 sampe_uvc_start_vi_vpss_venc(uvc_media_cfg *media_cfg)
{
    hi_uvc_chn    uvc_chn = 0;

    hi_s32 ret = sample_uvc_init_and_bind_vi_vpss(media_cfg);
    if (ret != HI_SUCCESS) {
        return HI_FAILURE;
    }

    ret = sample_uvc_chn_init(uvc_chn);
    if (ret != HI_SUCCESS) {
        sample_print("init uvc err for %#x!\n", ret);
        goto vi_unbind_vpss;
    }

    if (sample_uvc_is_need_venc(g_encoder_property.format) == HI_TRUE) {
        ret = sample_uvc_venc_init(media_cfg);
        if (ret != HI_SUCCESS) {
            sample_print("venc init failed!\n");
            goto uvc_exit;
        }

        ret = sample_comm_vpss_bind_venc(media_cfg->vpss_grp, media_cfg->vpss_chn, media_cfg->venc_chn);
        if (ret != HI_SUCCESS) {
            goto venc_exit;
        }
        if (g_data_input_mode == HI_UVC_MPP_BIND_UVC) {
            ret = sample_comm_venc_bind_uvc(media_cfg->venc_chn, uvc_chn);
            if (ret != HI_SUCCESS) {
                goto vpss_unbind_venc;
            }
        }
    } else {
        if (g_data_input_mode != HI_UVC_SEND_YUV_FRAME) {
            hi_vpss_chn vpss_chn =
                g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV ? HI_VPSS_MAX_PHYS_CHN_NUM : media_cfg->vpss_chn;
            ret = sample_comm_vpss_bind_uvc(media_cfg->vpss_grp, vpss_chn, uvc_chn);
            if (ret != HI_SUCCESS) {
                goto uvc_exit;
            }
        }
    }

    return HI_SUCCESS;

vpss_unbind_venc:
    sample_comm_vpss_un_bind_venc(media_cfg->vpss_grp, media_cfg->vpss_chn, media_cfg->venc_chn);
venc_exit:
    sample_comm_venc_stop(media_cfg->venc_chn);
uvc_exit:
    sample_uvc_chn_deinit(uvc_chn);
vi_unbind_vpss:
    sample_comm_vi_un_bind_vpss(media_cfg->vi_pipe, media_cfg->vi_chn, media_cfg->vpss_grp, media_cfg->vpss_chn);
    sample_uvc_vpss_de_init(media_cfg);
    sample_comm_vi_stop_vi(&media_cfg->vi_cfg);
    return HI_FAILURE;
}

static hi_void sample_uvc_stop_vi_vpss_venc(uvc_media_cfg *media_cfg)
{
    hi_vi_pipe    vi_pipe      = media_cfg->vi_pipe;
    hi_vi_chn     vi_chn       = media_cfg->vi_chn;
    sample_vi_cfg *vi_cfg      = &media_cfg->vi_cfg;
    hi_vpss_grp   vpss_grp     = media_cfg->vpss_grp;
    hi_vpss_chn   vpss_chn     = media_cfg->vpss_chn;
    hi_vpss_chn   vpss_ext_chn = HI_VPSS_MAX_PHYS_CHN_NUM;
    hi_venc_chn   venc_chn     = media_cfg->venc_chn;
    hi_uvc_chn    uvc_chn      = 0;
    hi_bool       is_need_venc = sample_uvc_is_need_venc(g_encoder_property.format);
    if (is_need_venc == HI_TRUE) {
        (hi_void)hi_mpi_venc_stop_chn(venc_chn); // need stop venc chn firstly when venc unbind uvc.
        if (g_data_input_mode == HI_UVC_MPP_BIND_UVC) {
            sample_comm_venc_un_bind_uvc(venc_chn, uvc_chn);
        }
        sample_comm_vpss_un_bind_venc(vpss_grp, vpss_chn, venc_chn);
        sample_uvc_chn_deinit(uvc_chn);
        sample_comm_venc_stop(venc_chn);
    } else {
        if (g_data_input_mode != HI_UVC_SEND_YUV_FRAME) {
            if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
                sample_comm_vpss_un_bind_uvc(vpss_grp, vpss_ext_chn, uvc_chn);
            } else {
                sample_comm_vpss_un_bind_uvc(vpss_grp, vpss_chn, uvc_chn);
            }
        }
        sample_uvc_chn_deinit(uvc_chn);
    }

    sample_comm_vi_un_bind_vpss(vi_pipe, vi_chn, vpss_grp, vpss_chn);
    sample_uvc_vpss_de_init(media_cfg);
    sample_comm_vi_stop_vi(vi_cfg);
}

static hi_s32 sample_uvc_media_route(hi_void)
{
    hi_s32 ret;
    hi_pic_size pic_size = PIC_1080P;
    hi_size stream_size;

    if (g_data_input_mode == HI_UVC_SEND_YUV_FRAME && sample_uvc_is_need_venc(g_encoder_property.format)) {
        g_data_input_mode = HI_UVC_SEND_VENC_STREAM;
    } else if (g_data_input_mode == HI_UVC_SEND_VENC_STREAM && !sample_uvc_is_need_venc(g_encoder_property.format)) {
        g_data_input_mode = HI_UVC_SEND_YUV_FRAME;
    }
    sample_uvc_media_cfg(&g_media_cfg);

    /* edit media cfg here */
    set_user_config_format(&g_media_cfg.venc_chn_param.type, &pic_size, &g_media_cfg.venc_chn_num);
    ret = sample_comm_sys_get_pic_size(pic_size, &stream_size);
    if (ret != HI_SUCCESS) {
        sample_print("get stream_size failed!\n");
        return ret;
    }

    g_media_cfg.venc_chn_param.venc_size.width = stream_size.width;
    g_media_cfg.venc_chn_param.venc_size.height = stream_size.height;
    g_media_cfg.venc_chn_param.size = pic_size;

    g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].width = stream_size.width;
    g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].height = stream_size.height;
    g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].frame_rate.src_frame_rate = (hi_s32)FRAME_INTERVAL_MAX;
    g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].frame_rate.dst_frame_rate = (hi_s32)g_encoder_property.fps;
    if (g_data_input_mode == OT_UVC_SEND_YUV_FRAME && g_encoder_property.format != VIDEO_IMG_FORMAT_YUYV) {
        g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].depth = 1; /* for user space get YUV(NV12 or NV21) */
    } else {
        g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].depth = 0;
    }

    if (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) {
        g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_422;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_NV12) {
        g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].pixel_format = HI_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    } else if (g_encoder_property.format == VIDEO_IMG_FORMAT_NV21) {
        g_media_cfg.vpss_chn_attr[g_media_cfg.vpss_chn].pixel_format = HI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    }

    ret = sampe_uvc_start_vi_vpss_venc(&g_media_cfg);
    if (ret != HI_SUCCESS) {
        sample_print("start vi-vpss-venc failed!\n");
        goto sys_exit;
    }

    return HI_SUCCESS;

sys_exit:
    sample_comm_sys_exit();
    return ret;
}

uint16_t venc_brightness_get(hi_void)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);

    return (uint16_t)cscf_attr.luma;
}

uint16_t venc_contrast_get(hi_void)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);

    return (uint16_t)cscf_attr.contr;
}

uint16_t venc_hue_get(hi_void)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);

    return (uint16_t)cscf_attr.hue;
}

uint8_t venc_power_line_frequency_get(hi_void)
{
    hi_isp_exposure_attr exp_attr;

    hi_mpi_isp_get_exposure_attr(0, &exp_attr);

    if ((exp_attr.auto_attr.antiflicker.frequency != 50) &&   /* 50: frequency */
        (exp_attr.auto_attr.antiflicker.frequency != 60)) {   /* 60: frequency */
        return 0;
    }

    return (exp_attr.auto_attr.antiflicker.frequency == 50) ? 0x1 : 0x2;    /* 50: frequency */
}

uint16_t venc_saturation_get(hi_void)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);

    return (uint16_t)cscf_attr.satu;
}

uint8_t venc_white_balance_temperature_auto_get(hi_void)
{
    hi_isp_wb_attr wb_attr;

    hi_mpi_isp_get_wb_attr(0, &wb_attr);

    return (wb_attr.op_type == HI_OP_MODE_AUTO) ? 0x1 : 0x0;
}

uint16_t venc_white_balance_temperature_get(hi_void)
{
    hi_isp_wb_info wb_info;

    hi_mpi_isp_query_wb_info(0, &wb_info);

    return (uint16_t)wb_info.color_temp;
}

hi_void venc_brightness_set(uint16_t v)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);
    cscf_attr.luma = (hi_u8)v;
    hi_mpi_isp_set_csc_attr(0, &cscf_attr);
}

hi_void venc_contrast_set(uint16_t v)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);
    cscf_attr.contr = (hi_u8)v;
    hi_mpi_isp_set_csc_attr(0, &cscf_attr);
}

hi_void venc_hue_set(uint16_t v)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);
    cscf_attr.hue = (hi_u8)v;
    hi_mpi_isp_set_csc_attr(0, &cscf_attr);
}

hi_void venc_power_line_frequency_set(uint8_t v)
{
    hi_s32 ret;
    hi_isp_exposure_attr exp_attr;

    hi_mpi_isp_get_exposure_attr(0, &exp_attr);
    if (v == 0) {
        exp_attr.auto_attr.antiflicker.enable = HI_FALSE;
    } else if (v == 1) {
        exp_attr.auto_attr.antiflicker.enable = HI_TRUE;
        exp_attr.auto_attr.antiflicker.frequency = 50;  /* 50: frequency */
    } else if (v == 2) {    /* 2: value */
        exp_attr.auto_attr.antiflicker.enable = HI_TRUE;
        exp_attr.auto_attr.antiflicker.frequency = 60;  /* 60: frequency */
    }

    ret = hi_mpi_isp_set_exposure_attr(0, &exp_attr);
    if (ret != HI_SUCCESS) {
        rlog("hi_mpi_isp_set_exposure_attr err 0x%x\n", ret);
    }
}

hi_void venc_saturation_set(uint16_t v)
{
    hi_isp_csc_attr cscf_attr;

    hi_mpi_isp_get_csc_attr(0, &cscf_attr);
    cscf_attr.satu = (hi_u8)v;
    hi_mpi_isp_set_csc_attr(0, &cscf_attr);
}

hi_void venc_white_balance_temperature_auto_set(uint8_t v)
{
    hi_isp_wb_attr wb_attr;

    hi_mpi_isp_get_wb_attr(0, &wb_attr);
    wb_attr.op_type = (v == 1) ? HI_OP_MODE_AUTO : HI_OP_MODE_MANUAL;
    hi_mpi_isp_set_wb_attr(0, &wb_attr);
}

hi_void venc_white_balance_temperature_set(uint16_t v)
{
    hi_isp_wb_info wb_info;
    hi_isp_wb_attr wb_attr;
    hi_u16 color_temp;
    hi_u16 awb_gain[4];
    errno_t ret;

    hi_mpi_isp_query_wb_info(0, &wb_info);
    hi_mpi_isp_get_wb_attr(0, &wb_attr);

    color_temp = v;
    hi_mpi_isp_cal_gain_by_temp(0, &wb_attr, color_temp, 0, awb_gain);

    wb_attr.op_type = HI_OP_MODE_MANUAL;
    ret = memcpy_s(&wb_attr.manual_attr, sizeof(wb_attr.manual_attr), awb_gain, sizeof(wb_attr.manual_attr));
    if (ret != EOK) {
        err("memcpy_s awb gain fail %x\n", ret);
    }

    hi_mpi_isp_set_wb_attr(0, &wb_attr);
}

uint8_t venc_exposure_auto_mode_get(hi_void)
{
    hi_isp_exposure_attr exp_attr;

    hi_mpi_isp_get_exposure_attr(0, &exp_attr);

    return (exp_attr.op_type == HI_OP_MODE_AUTO) ? 0x02 : 0x04;
}

uint32_t venc_exposure_ansolute_time_get(hi_void)
{
    hi_isp_exposure_attr exp_attr;

    hi_mpi_isp_get_exposure_attr(0, &exp_attr);

    return exp_attr.manual_attr.exp_time;
}

hi_void venc_exposure_auto_mode_set(uint8_t v)
{
    hi_isp_exposure_attr exp_attr;

    hi_mpi_isp_get_exposure_attr(0, &exp_attr);
    exp_attr.op_type = (v == 4) ? HI_OP_MODE_MANUAL : HI_OP_MODE_AUTO;  /* 4: value */
    hi_mpi_isp_set_exposure_attr(0, &exp_attr);
}

hi_void venc_exposure_ansolute_time_set(uint32_t v)
{
    hi_isp_exposure_attr exp_attr;

    hi_mpi_isp_get_exposure_attr(0, &exp_attr);
    exp_attr.manual_attr.exp_time = v * 100;    /* 100: ratio */
    exp_attr.manual_attr.exp_time_op_type = HI_OP_MODE_MANUAL;
    hi_mpi_isp_set_exposure_attr(0, &exp_attr);
}

hi_s32 sample_venc_set_idr(hi_void)
{
    return hi_mpi_venc_request_idr(0, HI_TRUE);
}

hi_s32 sample_venc_init(hi_void)
{
    hi_s32 ret;
    sample_sns_type sns_type = SENSOR0_TYPE;

    ret = sample_uvc_sys_init(sns_type, g_media_cfg.supplement_cfg);
    if (ret != HI_SUCCESS) {
        sample_print("sys init failed!\n");
        return ret;
    }

    return HI_SUCCESS;
}

hi_s32 sample_venc_deinit(hi_void)
{
    sample_comm_sys_exit();
    return HI_SUCCESS;
}

hi_s32 sample_venc_startup(hi_void)
{
    hi_s32 ret;

    g_is_media_start = HI_TRUE;
    ret = sample_uvc_media_route();
    if (g_encoder_property.format == VIDEO_IMG_FORMAT_H264 || g_encoder_property.format == VIDEO_IMG_FORMAT_H265) {
        (hi_void)hi_mpi_venc_request_idr(0, HI_TRUE);
    }
    return ret;
}

hi_s32 sample_venc_shutdown(hi_void)
{
    if (g_is_media_start == HI_FALSE) {
        return 0;
    }

    g_is_media_start = HI_FALSE;
    sample_uvc_stop_vi_vpss_venc(&g_media_cfg);

    return 0;
}

hi_s32 sample_venc_set_property(encoder_property *p)
{
    if (p == NULL) {
        err("p is NULL\n");
        return HI_FAILURE;
    }
    (hi_void)memcpy_s(&g_encoder_property, sizeof(encoder_property), p, sizeof(encoder_property));
    return 0;
}

hi_void sample_uvc_get_encoder_property(encoder_property *p)
{
    if (p == NULL) {
        err("p is NULL\n");
        return;
    }
    (hi_void)memcpy_s(p, sizeof(encoder_property), &g_encoder_property, sizeof(encoder_property));
}

hi_void sample_uvc_send_user_stream(hi_venc_stream *venc_stream)
{
    hi_s32 ret;
    hi_uvc_user_stream user_stream;

    for (td_u32 i = 0; i < venc_stream->pack_cnt; i++) {
        user_stream.pts = venc_stream->pack[i].pts;
        user_stream.addr = venc_stream->pack[i].addr + venc_stream->pack[i].offset;
        user_stream.len = venc_stream->pack[i].len - venc_stream->pack[i].offset;
        user_stream.is_frame_end = venc_stream->pack[i].is_frame_end;
        ret = hi_mpi_uvc_send_user_stream(0, &user_stream, 0); /* replace stream addr with your own buffer */
        if (ret != 0) {
            sample_print("hi_mpi_uvc_send_user_stream failed %#x\n", ret);
        }
    }
}

hi_s32 sample_send_yuv_frame(td_void)
{
    hi_s32 ret = HI_SUCCESS;
    ot_video_frame_info frame_info;
    ot_vpss_chn vpss_chn =
        (g_encoder_property.format == VIDEO_IMG_FORMAT_YUYV) ? HI_VPSS_MAX_PHYS_CHN_NUM : g_media_cfg.vpss_chn;

    if ((ret = hi_mpi_vpss_get_chn_frame(0, vpss_chn, &frame_info, 100)) != HI_SUCCESS) { /* 100 timeout ms */
        printf("get frame from VPSS %d fail(0x%x)!\n", vpss_chn, ret);
        return TD_FAILURE;
    }

    ret = hi_mpi_uvc_send_frame(0, &frame_info, 0);
    if (ret != HI_SUCCESS && ret != HI_ERR_UVC_UNEXIST) {
        sample_print("uvc_send_frame failed %#x\n", ret);
    }

    /* release frame after using */
    ret = hi_mpi_vpss_release_chn_frame(0, vpss_chn, &frame_info);
    if (ret != HI_SUCCESS) {
        sample_print("mpi_vpss_release_chn_frame failed %#x\n", ret);
    }
    return ret;
}

hi_s32 sample_venc_get_uvc_send(hi_void)
{
    hi_s32 ret;
    hi_venc_stream stream = {0};
    hi_venc_chn_status stat;

    if (g_is_media_start == HI_TRUE && g_data_input_mode == HI_UVC_SEND_YUV_FRAME
        && !sample_uvc_is_need_venc(g_encoder_property.format)) {
        return sample_send_yuv_frame();
    }

    if (g_is_media_start == HI_FALSE || g_data_input_mode == HI_UVC_MPP_BIND_UVC ||
        (!sample_uvc_is_need_venc(g_encoder_property.format))) {
        return HI_SUCCESS;
    }

    ret = hi_mpi_venc_query_status(g_media_cfg.venc_chn, &stat);
    if (ret == HI_ERR_VENC_UNEXIST) {
        return HI_SUCCESS;
    } else if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_venc_query_status chn[%d] failed with %#x!\n", g_media_cfg.venc_chn, ret);
        return HI_FAILURE;
    }

    if (stat.cur_packs == 0) {
        return HI_SUCCESS;
    }

    stream.pack = (hi_venc_pack *)malloc(sizeof(hi_venc_pack) * stat.cur_packs);
    if (stream.pack == HI_NULL) {
        sample_print("malloc stream pack failed!\n");
        return HI_FAILURE;
    }

    stream.pack_cnt = stat.cur_packs;
    ret = hi_mpi_venc_get_stream(g_media_cfg.venc_chn, &stream, 100); /* get stream timeout 100 ms */
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_venc_get_stream failed with %#x!\n", ret);
        goto end;
    }

    if (g_data_input_mode == HI_UVC_SEND_VENC_STREAM) {
        ret = hi_mpi_uvc_send_stream(0, g_media_cfg.venc_chn, &stream, 0);
        if (ret != HI_SUCCESS) {
            sample_print("hi_mpi_uvc_send_stream failed with %#x!\n", ret);
            goto end;
        }
    } else { /* HI_UVC_SEND_USER_STREAM */
        sample_uvc_send_user_stream(&stream);
    }

end:
    ret = hi_mpi_venc_release_stream(g_media_cfg.venc_chn, &stream);
    if (ret != HI_SUCCESS) {
        sample_print("hi_mpi_venc_release_stream failed %x\n", ret);
    }

    free(stream.pack);

    return ret;
}


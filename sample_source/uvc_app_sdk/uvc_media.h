/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#ifndef __SAMPLE_UVC_MEDIA_H__
#define __SAMPLE_UVC_MEDIA_H__

#include "hi_common_video.h"
#include "hi_common_vpss.h"
#include "sample_comm.h"
#include "ot_camera.h"

typedef struct {
    sample_sns_type            sns_type;
    hi_size                    sensor_size;

    hi_vi_aiisp_mode           aiisp_mode;
    hi_vi_vpss_mode_type       vi_vpss_mode_type;
    hi_vpss_grp_attr           vpss_grp_attr;
    hi_bool                    vpss_chn_enable[HI_VPSS_MAX_PHYS_CHN_NUM];
    hi_vpss_chn_attr           vpss_chn_attr[HI_VPSS_MAX_PHYS_CHN_NUM];
    sample_comm_venc_chn_param venc_chn_param;

    hi_vi_pipe      vi_pipe;
    hi_vi_chn       vi_chn;
    sample_vi_cfg   vi_cfg;
    hi_vpss_grp     vpss_grp;
    hi_vpss_chn     vpss_chn;
    hi_venc_chn     venc_chn;
    hi_s32          venc_chn_num;

    hi_u32          supplement_cfg;
} uvc_media_cfg;

#ifdef __cplusplus
extern "C" {
#endif

uint16_t venc_brightness_get(hi_void);
uint16_t venc_contrast_get(hi_void);
uint16_t venc_hue_get(hi_void);
uint8_t venc_power_line_frequency_get(hi_void);
uint16_t venc_saturation_get(hi_void);
uint8_t venc_white_balance_temperature_auto_get(hi_void);
uint16_t venc_white_balance_temperature_get(hi_void);
hi_void venc_brightness_set(uint16_t v);
hi_void venc_contrast_set(uint16_t v);
hi_void venc_hue_set(uint16_t v);
hi_void venc_power_line_frequency_set(uint8_t v);
hi_void venc_saturation_set(uint16_t v);
hi_void venc_white_balance_temperature_auto_set(uint8_t v);
hi_void venc_white_balance_temperature_set(uint16_t v);
uint8_t venc_exposure_auto_mode_get(hi_void);
uint32_t venc_exposure_ansolute_time_get(hi_void);
hi_void venc_exposure_auto_mode_set(uint8_t v);
hi_void venc_exposure_ansolute_time_set(uint32_t v);

hi_s32 sample_venc_set_idr(hi_void);
hi_s32 sample_venc_init(hi_void);
hi_s32 sample_venc_deinit(hi_void);
hi_s32 sample_venc_startup(hi_void);
hi_s32 sample_venc_shutdown(hi_void);

hi_s32 sample_venc_set_property(encoder_property *p);
hi_void sample_uvc_get_encoder_property(encoder_property *p);
hi_s32 sample_venc_get_uvc_send(hi_void);

#ifdef __cplusplus
}
#endif

#endif /* __SAMPLE_UVC_MEDIA_H__ */

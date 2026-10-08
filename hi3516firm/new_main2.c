#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <endian.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "securec.h"
#include "ot_pq_bin.h"          // PQTOOL的校准bin文件 导入导出函数

#include "ot_buffer.h"
#include "ot_common_adec.h"
#include "ot_common_aenc.h"
#include "ot_common_aio.h"
#include "ot_common_isp.h"
#include "ot_common_region.h"
#include "ot_common_sys.h"
#include "ot_common_vb.h"
#include "ot_common_venc.h"
#include "ot_common_vgs.h"
#include "ot_common_vi.h"
#include "ot_common_vpss.h"
#include "ot_common.h"
#include "ot_defines.h"
#include "ot_mipi_rx.h"

#include "ot_math.h"
#include "ot_sns_ctrl.h"
#include "ss_mpi_ae.h"
#include "ss_mpi_audio.h"
#include "ss_mpi_awb.h"
#include "ss_mpi_isp.h"
#include "ss_mpi_region.h"
#include "ss_mpi_sys.h"
#include "ss_mpi_sys_bind.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_venc.h"
#include "ss_mpi_vgs.h"
#include "ss_mpi_vi.h"
#include "ss_mpi_vpss.h"

// 直接把sensor的驱动代码包含了，同样也要编译.c
#include "./smart_sc500ai_100/sc500ai_cmos.h"

// 根据文档记录 复刻RTSP的代码来实现代码 最后加入低延迟特性

// 初始化部分涉及的结构体
// VI组件，SYS_CFG、SNS_INFO、MIPI_INFO、DEV_INFO、BIND_PIPE、GRP_INFO、PIPE_INFO结构体
//sampe_sys_cfg sys_cfg = { 0 };
//sample_sns_info sns_info = { 0 };
//sample_mipi_info mipi_info = { 0 };
//sample_vi_dev_info dev_info = { 0 };
//ot_vi_bind_pipe bind_pipe = { 0 };
//sample_vi_grp_info grp_info = { 0 };
//sample_vi_pipe_info pipe_info[2] = { 0 };
//
//// VPSS组件，vpss_grp、grp_attr、nr_attr、chn_en、chn_attr、wrap_attr
//ot_vpss_grp vpss_grp;
//ot_vpss_grp_attr grp_attr;
//ot_3dnr_attr nr_attr;
//td_bool chn_en[OT_VPSS_MAX_PHYS_CHN_NUM];
//ot_vpss_chn_attr chn_attr[OT_VPSS_MAX_PHYS_CHN_NUM];
//ot_vpss_chn_buf_wrap_attr wrap_attr[OT_VPSS_MAX_PHYS_CHN_NUM];

typedef enum {
    SC4336P_MIPI_4M_30FPS_10BIT,
    OS04D10_MIPI_4M_30FPS_10BIT,
    GC4023_MIPI_4M_30FPS_10BIT,
    SC431HAI_MIPI_4M_30FPS_10BIT,
    SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1,
    SC450AI_MIPI_4M_30FPS_10BIT,
    SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1,
    SC500AI_MIPI_5M_30FPS_10BIT,
    SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1,
    SC500AI_MIPI_2M_100FPS_10BIT,
    SC4336P_MIPI_3M_30FPS_10BIT,
    SC4336P_MIPI_2M_30FPS_10BIT,
    SNS_TYPE_BUTT,
} sample_sns_type;
#define SENSOR0_TYPE SC500AI_MIPI_2M_100FPS_10BIT

#define SAMPLE_VIE_MAX_ROUTE_NUM 4
#define CHN_NUM_MAX 2
typedef struct {
    td_s32               route_num;
    ot_vi_vpss_mode_type mode_type;
    ot_fmu_mode          vi_fmu[SAMPLE_VIE_MAX_ROUTE_NUM];
    ot_fmu_mode          vpss_fmu[SAMPLE_VIE_MAX_ROUTE_NUM];
    td_bool              vpss_wrap_en;
    td_u32               vpss_wrap_size;
} sampe_sys_cfg;

typedef struct {
    sample_sns_type sns_type;
    td_u32          sns_clk_src;
    td_u32          sns_rst_src;
    td_u32          bus_id;
    td_bool         sns_clk_rst_en;
} sample_sns_info;

typedef struct {
    td_s32             mipi_dev;
    lane_divide_mode_t divide_mode;
    combo_dev_attr_t   combo_dev_attr;
    ext_data_type_t    ext_data_type_attr;
} sample_mipi_info;

typedef struct {
    ot_vi_dev      vi_dev;
    ot_vi_dev_attr dev_attr;
    ot_vi_bas_attr bas_attr;
} sample_vi_dev_info;

typedef struct {
    td_u32                    grp_num;
    ot_vi_grp                 fusion_grp[OT_VI_MAX_WDR_FUSION_GRP_NUM];
    ot_vi_wdr_fusion_grp_attr fusion_grp_attr[OT_VI_MAX_WDR_FUSION_GRP_NUM];
    td_u32                    distribute_grp_num;
    ot_vi_grp                 distribute_grp[OT_VI_MAX_DISTRIBUTE_GRP_NUM];
    ot_vi_distribute_grp_attr distribute_grp_attr[OT_VI_MAX_DISTRIBUTE_GRP_NUM];
} sample_vi_grp_info;

typedef struct {
    ot_isp_pub_attr isp_pub_attr;
} sample_isp_info;

typedef struct {
    ot_vi_chn      vi_chn;
    ot_vi_chn_attr chn_attr;
    ot_fmu_mode    fmu_mode;
} sample_vi_chn_info;

typedef struct {
    ot_vi_pipe_attr    pipe_attr;

    td_bool            pipe_need_start;
    td_bool            isp_need_run;
    sample_isp_info    isp_info;
    td_bool            isp_be_end_trigger;
    td_bool            isp_quick_start;

    td_u32             chn_num;
    sample_vi_chn_info chn_info[OT_VI_MAX_PHYS_CHN_NUM];
    ot_3dnr_attr       nr_attr;
    td_bool            vc_change_en;
    td_u32             vc_number;
    td_bool            is_master_pipe;
    td_u32             bnr_bnf_num;
    ot_vi_pipe_buf_wrap_attr wrap_attr;
    td_u32             pixel_rate;
    ot_low_delay_info low_delay_info;
    ot_frame_interrupt_attr frame_interrupt_attr;
    td_bool            set_early_end_mode;
    ot_vb_pool         attach_pool;
} sample_vi_pipe_info;

typedef struct {
    ot_vpss_grp                 vpss_grp;
    ot_vpss_grp_attr            grp_attr;
    ot_3dnr_attr                nr_attr;
    td_bool                     chn_en[OT_VPSS_MAX_PHYS_CHN_NUM];
    ot_vpss_chn_attr            chn_attr[OT_VPSS_MAX_PHYS_CHN_NUM];
    ot_vpss_chn_buf_wrap_attr   wrap_attr[OT_VPSS_MAX_PHYS_CHN_NUM];
} sample_vpss_cfg;

typedef struct {
    sample_sns_info     sns_info;
    sample_mipi_info    mipi_info;
    sample_vi_dev_info  dev_info;
    ot_vi_bind_pipe     bind_pipe;
    sample_vi_grp_info  grp_info;
    sample_vi_pipe_info pipe_info[OT_VI_MAX_PHYS_PIPE_NUM];
} sample_vi_cfg;

typedef struct {
    ot_vpss_chn vpss_chn[CHN_NUM_MAX];
    ot_venc_chn venc_chn[CHN_NUM_MAX];
} sample_venc_vpss_chn;

#define WIDTH_1920 1920
#define HEIGHT_1080 1080
static combo_dev_attr_t g_mipi_4lane_chn0_sensor_sc500ai_10bit_2m_100_nowdr_attr = {
    .devno = 0,
    .input_mode = INPUT_MODE_MIPI,
    .data_rate = MIPI_DATA_RATE_X1,
    .img_rect = {0, 0, WIDTH_1920, HEIGHT_1080},
    .mipi_attr = {
        DATA_TYPE_RAW_10BIT,
        OT_MIPI_WDR_MODE_NONE,
        {0, 1, 2, 3}
    }
};

#define MIPI_NUM 3
static ext_data_type_t g_mipi_ext_data_type_default_attr = {
    .devno = 0,
    .num = MIPI_NUM,
    .ext_data_bit_width = {12, 12, 12},
    .ext_data_type = {0x2c, 0x2c, 0x2c}
};

#define WIDTH_3840 3840
#define HEIGHT_2160 2160
static ot_vi_dev_attr g_mipi_raw_dev_attr = {
    .intf_mode = OT_VI_INTF_MODE_MIPI,

    /* Invalid argument */
    .work_mode = OT_VI_WORK_MODE_MULTIPLEX_1,

    /* mask component */
    .component_mask = {0xfff00000, 0x00000000},

    .scan_mode = OT_VI_SCAN_PROGRESSIVE,

    /* Invalid argument */
    .ad_chn_id = {-1, -1, -1, -1},

    /* data seq */
    .data_seq = OT_VI_DATA_SEQ_YVYU,

    /* sync param */
    .sync_cfg = {
        .vsync = OT_VI_VSYNC_FIELD,
        .vsync_neg = OT_VI_VSYNC_NEG_HIGH,
        .hsync = OT_VI_HSYNC_VALID_SIG,
        .hsync_neg = OT_VI_HSYNC_NEG_HIGH,
        .vsync_valid = OT_VI_VSYNC_VALID_SIG,
        .vsync_valid_neg = OT_VI_VSYNC_VALID_NEG_HIGH,
        .timing_blank = {
        /* hsync_hfb      hsync_act     hsync_hhb */
        0,                0,            0,
        /* vsync0_vhb     vsync0_act    vsync0_hhb */
        0,                0,            0,
        /* vsync1_vhb     vsync1_act    vsync1_hhb */
        0,                0,            0
    }
},

/* data type */
.data_type = OT_VI_DATA_TYPE_RAW,

/* data reverse */
.data_reverse = TD_FALSE,

/* input size */
.in_size = {WIDTH_3840, HEIGHT_2160},

/* data rate */
.data_rate = OT_DATA_RATE_X1,
};

static ot_isp_pub_attr g_isp_pub_attr_sc500ai_mipi_2m_100fps = {
    {0, 0, 1920, 1080},
    { 1920, 1080 },
    100,
    OT_ISP_BAYER_BGGR,
    OT_WDR_MODE_NONE,
    SC500AI_2M_100FPS_10BIT_LINEAR_MODE,        // 原代码 值0
    0,
    0,
    {
        0,
        {0, 0, 1920, 1080},
    },
};

#define MIPI_DEV_NAME "/dev/ot_mipi_rx"

typedef enum {
    PIC_CIF,
    PIC_360P,    /* 640 * 360 */
    PIC_D1_PAL,  /* 720 * 576 */
    PIC_D1_NTSC, /* 720 * 480 */
    PIC_960H,      /* 960 * 576 */
    PIC_720P,    /* 1280 * 720 */
    PIC_1080P,   /* 1920 * 1080 */
    PIC_480P,
    PIC_576P,
    PIC_800X600,
    PIC_1024X768,
    PIC_1280X1024,
    PIC_1366X768,
    PIC_1440X900,
    PIC_1280X800,
    PIC_1600X1200,
    PIC_1680X1050,
    PIC_1920X1200,
    PIC_640X480,
    PIC_1920X2160,
    PIC_2304X1296,
    PIC_2560X1440,
    PIC_2560X1600,
    PIC_2592X1520,
    PIC_2688X1520,
    PIC_2592X1944,
    PIC_3840X2160,
    PIC_4096X2160,
    PIC_3000X3000,
    PIC_4000X3000,
    PIC_6080X2800,
    PIC_7680X4320,
    PIC_3840X8640,
    PIC_2880X1620,
    PIC_BUTT
} ot_pic_size;

/* The order of g_sample_pic_size's element must be consistent with the enum value defined in "ot_pic_size". */
static ot_size g_sample_pic_size[PIC_BUTT] = {
    { 352,  288  },  /* PIC_CIF */
    { 640,  360  },  /* PIC_360P */
    { 720,  576  },  /* PIC_D1_PAL */
    { 720,  480  },  /* PIC_D1_NTSC */
    { 960,  576  },  /* PIC_960H */
    { 1280, 720  },  /* PIC_720P */
    { 1920, 1080 },  /* PIC_1080P */
    { 720,  480  },  /* PIC_480P */
    { 720,  576  },  /* PIC_576P */
    { 800,  600  },  /* PIC_800X600 */
    { 1024, 768  },  /* PIC_1024X768 */
    { 1280, 1024 },  /* PIC_1280X1024 */
    { 1366, 768  },  /* PIC_1366X768 */
    { 1440, 900  },  /* PIC_1440X900 */
    { 1280, 800  },  /* PIC_1280X800 */
    { 1600, 1200 },  /* PIC_1600X1200 */
    { 1680, 1050 },  /* PIC_1680X1050 */
    { 1920, 1200 },  /* PIC_1920X1200 */
    { 640,  480  },  /* PIC_640X480 */
    { 1920, 2160 },  /* PIC_1920X2160 */
    { 2304, 1296 },  /* PIC_2304X1296 */
    { 2560, 1440 },  /* PIC_2560X1440 */
    { 2560, 1600 },  /* PIC_2560X1600 */
    { 2592, 1520 },  /* PIC_2592X1520 */
    { 2688, 1520 },  /* PIC_2688X1520 */
    { 2592, 1944 },  /* PIC_2592X1944 */
    { 3840, 2160 },  /* PIC_3840X2160 */
    { 4096, 2160 },  /* PIC_4096X2160 */
    { 3000, 3000 },  /* PIC_3000X3000 */
    { 4000, 3000 },  /* PIC_4000X3000 */
    { 6080, 2800 },  /* PIC_6080X2800 */
    { 7680, 4320 },  /* PIC_7680X4320 */
    { 3840, 8640 }   /* PIC_3840X8640 */
};

typedef enum {
    SAMPLE_RC_ABR = 0,
    SAMPLE_RC_CBR,
    SAMPLE_RC_VBR,
    SAMPLE_RC_AVBR,
    SAMPLE_RC_CVBR,
    SAMPLE_RC_QVBR,
    SAMPLE_RC_QPMAP,
    SAMPLE_RC_FIXQP
} sample_rc;

#pragma pack(push, 1)
struct RTPHeader {
    uint8_t version;
    uint8_t payload_type;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc_id;
};
#pragma pack(pop)

double getTimeInterval(
    struct timespec* timestamp, struct timespec* last_meansure_timestamp) {
    return (timestamp->tv_sec - last_meansure_timestamp->tv_sec) +
        (timestamp->tv_nsec - last_meansure_timestamp->tv_nsec) / 1000000000.;
}

uint8_t stream_mode = 1;     // 1是RTP发送 0是UDP裸流

// ISP组件
static pthread_t g_isp_pid[OT_VI_MAX_PIPE_NUM] = { 0 };
static td_bool g_start_isp[OT_VI_MAX_PIPE_NUM] = { TD_FALSE };
volatile sig_atomic_t g_loop_running = 1;            // 跨线程代码需要添加原子操作，防止资源静态访问

uint8_t* g_tx_buffer;
uint8_t* g_combined_buffer;


void set_vi_sys_cfg(sampe_sys_cfg *sys_cfg)
{
    sys_cfg->route_num = 1;
    sys_cfg->mode_type = OT_VI_OFFLINE_VPSS_ONLINE;     // VI离线 VPSS在线
    sys_cfg->vpss_wrap_en = TD_FALSE;                   // 不开环形缓冲区
}

void set_vi_sns_info(sample_sns_info* sns_info)
{
    sns_info->sns_type = SENSOR0_TYPE;
    sns_info->sns_clk_src = 0;
    sns_info->sns_rst_src = 0;
    sns_info->bus_id = 0; /* asic i2c4 , FPGA default 0 */
    sns_info->sns_clk_rst_en = TD_TRUE;
}

void set_vi_mipi_info(sample_mipi_info* mipi_info)
{
    mipi_info->mipi_dev = 0;
    mipi_info->divide_mode = LANE_DIVIDE_MODE_0;

    // 设置mipi_info的combo_dev_attr属性
    // g_mipi_4lane_chn0_sensor_sc500ai_10bit_2m_100_nowdr_attr
    (td_void)memcpy_s(&mipi_info->combo_dev_attr, sizeof(combo_dev_attr_t),
            &g_mipi_4lane_chn0_sensor_sc500ai_10bit_2m_100_nowdr_attr, sizeof(combo_dev_attr_t));

    // 设置mipi_info的ext_data_type_attr属性
    // g_mipi_ext_data_type_default_attr
    (td_void)memcpy_s(&mipi_info->ext_data_type_attr, sizeof(ext_data_type_t),
        &g_mipi_ext_data_type_default_attr, sizeof(ext_data_type_t));
}

void set_dev_info(sample_vi_dev_info* dev_info)
{
    dev_info->vi_dev = 0;

    // 设置dev_attr属性
    // g_mipi_raw_dev_attr
    (td_void)memcpy_s(&dev_info->dev_attr, sizeof(ot_vi_dev_attr), &g_mipi_raw_dev_attr, sizeof(ot_vi_dev_attr));

    dev_info->dev_attr.in_size.width = 1920;
    dev_info->dev_attr.in_size.height = 1080;
    dev_info->bas_attr.enable = TD_FALSE;           // 不启用VI缩放
}

void set_vi_bind_pipe(ot_vi_bind_pipe* bind_pipe)
{
    bind_pipe->pipe_num = 1;        // 非WDR 只需要一个VI管道即可
    bind_pipe->pipe_id[0] = 0;
}

void set_vi_grp_info(sample_vi_grp_info* grp_info)
{
    grp_info->grp_num = 1;      // 一个融合组
    grp_info->fusion_grp[0] = 0;

    grp_info->fusion_grp_attr[0].wdr_mode = OT_WDR_MODE_NONE;
    grp_info->fusion_grp_attr[0].cache_line = 1080/2;
    grp_info->fusion_grp_attr[0].pipe_reverse = TD_FALSE;
    grp_info->fusion_grp_attr[0].pipe_id[0] = 0;        // 绑定VI管道
}

// 管道像素速率计算
static td_u32 calc_pipe_pixel_rate(td_u32 lane_rate, td_u32 lane_num, td_u32 bit_num, td_u32 wdr_num)
{
    td_u32 lane_base_bps = 1000 * 1000; /* base: 1Mbps */
    td_u64 sensor_total_rate = lane_rate * lane_base_bps * lane_num / bit_num;
    return sensor_total_rate / wdr_num;
}

void set_vi_pipe_info(sample_vi_pipe_info pipe_info[])
{
    pipe_info[0].pipe_attr.pipe_bypass_mode = OT_VI_PIPE_BYPASS_NONE;
    pipe_info[0].pipe_attr.isp_bypass = TD_FALSE;
    pipe_info[0].pipe_attr.size.width = 1920;
    pipe_info[0].pipe_attr.size.height = 1080;
    pipe_info[0].pipe_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_12BPP; // 12->10    此处取消压缩到10bit    OT_PIXEL_FORMAT_RGB_BAYER_10BPP
    pipe_info[0].pipe_attr.compress_mode = OT_COMPRESS_MODE_NONE;   // 此处取消行压缩  OT_COMPRESS_MODE_LINE
    pipe_info[0].pipe_attr.frame_rate_ctrl.src_frame_rate = -1;
    pipe_info[0].pipe_attr.frame_rate_ctrl.dst_frame_rate = -1;

    //pipe_info[0].bnr_bnf_num = 3;             // 默认2buf无需配置
    pipe_info[0].pipe_need_start = TD_TRUE;
    pipe_info[0].isp_need_run = TD_TRUE;
    pipe_info[0].isp_quick_start = TD_FALSE;

    pipe_info[0].is_master_pipe = TD_TRUE;
    
    // 设置pipe_info的ISP发布属性
    // g_isp_pub_attr_sc500ai_mipi_2m_100fps
    (td_void)memcpy_s(&pipe_info[0].isp_info.isp_pub_attr, sizeof(ot_isp_pub_attr),
        &g_isp_pub_attr_sc500ai_mipi_2m_100fps, sizeof(ot_isp_pub_attr));

    pipe_info[0].nr_attr.enable = TD_TRUE;
    //pipe_info[0].nr_attr.compress_mode = OT_COMPRESS_MODE_LINE;                // 原代码是 OT_COMPRESS_MODE_FRAME 修改代码OT_COMPRESS_MODE_LINE OT_COMPRESS_MODE_NONE 
    pipe_info[0].nr_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    pipe_info[0].nr_attr.nr_type = OT_NR_TYPE_VIDEO_NORM;
    pipe_info[0].nr_attr.nr_motion_mode = OT_NR_MOTION_MODE_NORM;       // 原代码是 OT_NR_MOTION_MODE_NORM 修改代码OT_NR_MOTION_MODE_COMPENSATION
    //pipe_info[0].pixel_rate = calc_pipe_pixel_rate(792, 4, 10, 1);            // VI离线模式无需计算像素速率
    //pipe_info[0].attach_pool = OT_VB_INVALID_POOL_ID;                         // attach_pool似乎也可以不要

    pipe_info[0].chn_num = 1;
    pipe_info[0].chn_info[0].vi_chn = 0;
    pipe_info[0].chn_info[0].fmu_mode = OT_FMU_MODE_OFF;
    pipe_info[0].chn_info[0].chn_attr.size.width = 1920;                        // 以下原代码与新代码无区别
    pipe_info[0].chn_info[0].chn_attr.size.height = 1080;
    pipe_info[0].chn_info[0].chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    pipe_info[0].chn_info[0].chn_attr.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    pipe_info[0].chn_info[0].chn_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
    pipe_info[0].chn_info[0].chn_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    pipe_info[0].chn_info[0].chn_attr.mirror_en = TD_FALSE;
    pipe_info[0].chn_info[0].chn_attr.flip_en = TD_FALSE;
    pipe_info[0].chn_info[0].chn_attr.depth = 0;
    pipe_info[0].chn_info[0].chn_attr.frame_rate_ctrl.src_frame_rate = -1;
    pipe_info[0].chn_info[0].chn_attr.frame_rate_ctrl.dst_frame_rate = -1;
}

void set_vpss_grp(ot_vpss_grp_attr* grp_attr)
{
    grp_attr->ie_en = TD_FALSE;
    grp_attr->dci_en = TD_FALSE;
    grp_attr->buf_share_en = TD_FALSE;
    grp_attr->mcf_en = TD_FALSE;
    grp_attr->max_width = 1920;     // max 2880
    grp_attr->max_height = 1080;       // max 1620
    grp_attr->max_dei_width = 0;
    grp_attr->max_dei_height = 0;
    grp_attr->dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    grp_attr->pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    grp_attr->dei_mode = OT_VPSS_DEI_MODE_OFF;
    //grp_attr->buf_share_chn = OT_VPSS_CHN0;           新代码缓冲区共享关闭
    grp_attr->frame_rate.src_frame_rate = -1;
    grp_attr->frame_rate.dst_frame_rate = -1;
}

void set_vpss_chn(ot_vpss_chn_attr* chn_attr)
{
    chn_attr->mirror_en = TD_FALSE;
    chn_attr->flip_en = TD_FALSE;
    chn_attr->border_en = TD_FALSE;
    chn_attr->width = 1920;
    chn_attr->height = 1080;
    chn_attr->depth = 0;
    chn_attr->chn_mode = OT_VPSS_CHN_MODE_USER;
    chn_attr->video_format = OT_VIDEO_FORMAT_LINEAR;
    chn_attr->dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    chn_attr->pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    chn_attr->compress_mode = OT_COMPRESS_MODE_NONE;
    chn_attr->aspect_ratio.mode = OT_ASPECT_RATIO_NONE;
    chn_attr->frame_rate.src_frame_rate = -1;
    chn_attr->frame_rate.dst_frame_rate = -1;
}

void set_vpss_cfg(sample_vpss_cfg* vpss_cfg)
{
    vpss_cfg->vpss_grp = 0;
    vpss_cfg->chn_en[0] = TD_TRUE;
    vpss_cfg->chn_en[1] = TD_FALSE;

    // 不开环形缓冲区用不到 不开压缩用不到
    // vpss_cfg->chn_attr[OT_VPSS_CHN0].compress_mode = OT_COMPRESS_MODE_SEG_COMPACT;  // VPSS通道0 分段压缩
    // vpss_cfg->wrap_attr[0].enable = TD_FALSE;       // 不开环形缓冲区
}

void sys_init(sampe_sys_cfg* sys_cfg)
{
    ot_vb_cfg vb_cfg = { 0 };
    ot_pic_buf_attr tmp_buf_attr;
    ot_vi_vpss_mode vi_vpss_mode;

    ot_vb_supplement_cfg supplement_conf = { 0 };
    supplement_conf.supplement_cfg = OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK;
    
    // 由于不使用环形缓冲区，而且需要计算缓冲区大小，创建一个临时变量
    tmp_buf_attr.width = 1920;
    tmp_buf_attr.height = 1080;
    tmp_buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    tmp_buf_attr.align = OT_DEFAULT_ALIGN;
    tmp_buf_attr.bit_width = OT_DATA_BIT_WIDTH_12;
    tmp_buf_attr.pixel_format = OT_PIXEL_FORMAT_RGB_BAYER_12BPP;
    tmp_buf_attr.video_format = OT_VIDEO_FORMAT_LINEAR;

    (td_void)memset_s(&vb_cfg, sizeof(ot_vb_cfg), 0, sizeof(ot_vb_cfg));
    vb_cfg.max_pool_cnt = 2;
    vb_cfg.common_pool[0].blk_cnt = 3;
    vb_cfg.common_pool[0].blk_size = ot_common_get_pic_buf_size(&tmp_buf_attr);        // 通过调用MPP库函数来进行计算

    // 01.退出SYS和VB 上次可能残留的
    ss_mpi_sys_exit();
    ss_mpi_vb_exit();

    // 02.进行VB设置
    ss_mpi_vb_set_cfg(&vb_cfg);

    // 03.进行VB补充配置，如果需要用到JPEG或者3D NR和运动预测要用到
    // ss_mpi_vb_set_supplement_cfg(&supplement_conf);

    // 04.VB初始化
    ss_mpi_vb_init();

    // 05.SYS初始化
    ss_mpi_sys_init();

    // 06.VI_VPSS模式设置
    ss_mpi_sys_get_vi_vpss_mode(&vi_vpss_mode);
    vi_vpss_mode.mode[0] = OT_VI_OFFLINE_VPSS_ONLINE;
    vi_vpss_mode.mode[1] = OT_VI_OFFLINE_VPSS_ONLINE;
    vi_vpss_mode.mode[2] = OT_VI_OFFLINE_VPSS_ONLINE;
    vi_vpss_mode.mode[3] = OT_VI_OFFLINE_VPSS_ONLINE;
    ss_mpi_sys_set_vi_vpss_mode(&vi_vpss_mode);

    // 07.如果要用到AI ISP功能
    ss_mpi_sys_set_vi_aiisp_mode(0, OT_VI_AIISP_MODE_DEFAULT);
}

void vi_init(sample_vi_cfg* vi_cfg)
{
    td_s32 fd;
    fd = open(MIPI_DEV_NAME, O_RDWR);

    ot_vi_pipe vi_pipe = vi_cfg->bind_pipe.pipe_id[0];

    // 08.设置MIPI LANE拆分、使能MCLK时钟、复位SENSOR、取消复位SENSOR
    // 开启MIPI内部时钟、复位MIPI、设置MIPI设备属性和额外设备属性、取消复位MIPI
    ioctl(fd, OT_MIPI_SET_HS_MODE, &vi_cfg->mipi_info.divide_mode);

    ioctl(fd, OT_MIPI_ENABLE_SENSOR_CLOCK, &vi_cfg->sns_info.sns_clk_src);

    ioctl(fd, OT_MIPI_RESET_SENSOR, &vi_cfg->sns_info.sns_rst_src);

    ioctl(fd, OT_MIPI_UNRESET_SENSOR, &vi_cfg->sns_info.sns_rst_src);

    ioctl(fd, OT_MIPI_ENABLE_MIPI_CLOCK, &vi_cfg->mipi_info.mipi_dev);

    ioctl(fd, OT_MIPI_RESET_MIPI, &vi_cfg->mipi_info.mipi_dev);

    ioctl(fd, OT_MIPI_SET_DEV_ATTR, &vi_cfg->mipi_info.combo_dev_attr);

    ioctl(fd, OT_MIPI_SET_EXT_DATA_TYPE, &vi_cfg->mipi_info.ext_data_type_attr);

    ioctl(fd, OT_MIPI_UNRESET_MIPI, &vi_cfg->mipi_info.mipi_dev);
    
    // 09.设置VI设备属性
    ss_mpi_vi_set_dev_attr(vi_cfg->dev_info.vi_dev, &vi_cfg->dev_info.dev_attr);

    // 10.使能VI设备
    ss_mpi_vi_enable_dev(vi_cfg->dev_info.vi_dev);

    // 11.绑定VI设备到管道
    ss_mpi_vi_bind(vi_cfg->dev_info.vi_dev, vi_pipe);

    // 12.设置VI GRP 属性
    ss_mpi_vi_set_wdr_fusion_grp_attr(vi_cfg->grp_info.fusion_grp[0], &vi_cfg->grp_info.fusion_grp_attr[0]);

    // 13.设置管道去噪缓冲区数量，原代码使能，修改代码禁用
    //ss_mpi_vi_set_pipe_bnr_buf_num(vi_pipe, vi_cfg->pipe_info[0].bnr_bnf_num);
    
    // 14.设置环形缓冲区，没使用
    // hi_mpi_vi_set_pipe_buf_wrap_attr(vi_pipe, &vi_cfg->pipe_info[0].wrap_attr);

    // 15.创建VI管道
    ss_mpi_vi_create_pipe(vi_pipe, &vi_cfg->pipe_info[0].pipe_attr);

    // 16.VI离线不用设置像素速率
    // hi_mpi_vi_set_pipe_online_clock(vi_pipe, pixel_rate);

    // 17.设置虚拟通道数量，CMOS没有开启2个VC，所以不用开
    // hi_mpi_vi_set_pipe_vc_number(vi_pipe, vi_cfg->pipe_info[0].vc_number);

    // 18.相比官方代码增加VI提前中断
    ot_frame_interrupt_attr vi_interrupt_attr;
    vi_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_EARLY;
    vi_interrupt_attr.early_line = 1080 - 200;
    ss_mpi_vi_set_pipe_frame_interrupt_attr(vi_pipe, &vi_interrupt_attr);

    // 19.启动管道
    ss_mpi_vi_start_pipe(vi_pipe);

    // 20.相比官方代码新增管道低延迟
    ot_low_delay_info info;
    ss_mpi_vi_get_pipe_low_delay(vi_pipe, &info);
    info.enable = TD_TRUE;
    info.line_cnt = 1080 / 4;
    ss_mpi_vi_set_pipe_low_delay(vi_pipe, &info);
    
    // 21.设置VI通道属性
    ss_mpi_vi_set_chn_attr(vi_pipe, vi_cfg->pipe_info[0].chn_info[0].vi_chn, &vi_cfg->pipe_info[0].chn_info[0].chn_attr);

    // 22.设置3D NR和额外VI PIPE属性  相比官方代码设置了nr_effect_mode
    ot_vi_pipe_param tmp_vi_pipe_param;
    ss_mpi_vi_get_pipe_param(vi_pipe, &tmp_vi_pipe_param);
    //tmp_vi_pipe_param.nr_effect_mode = OT_VI_NR_EFFECT_MODE_ADVANCE;   // 降噪效果好 但是有一帧延迟
    tmp_vi_pipe_param.nr_effect_mode = OT_VI_NR_EFFECT_MODE_NORM;
    ss_mpi_vi_set_pipe_param(vi_pipe, &tmp_vi_pipe_param);
    ss_mpi_vi_set_pipe_3dnr_attr(vi_pipe, &vi_cfg->pipe_info[0].nr_attr);

    // 23.开启VI通道
    ss_mpi_vi_enable_chn(vi_pipe, vi_cfg->pipe_info[0].chn_info[0].vi_chn);
}

static void* sample_comm_isp_thread(td_void* param);
void true_isp_init(sample_vi_cfg* vi_cfg)
{
    ot_isp_3a_alg_lib ae_lib;
    ot_isp_3a_alg_lib awb_lib;
    //ot_isp_3a_alg_lib thermo_lib;

    ot_isp_sns_commbus sns_bus_info;

    ot_isp_sns_obj* sns_obj = &g_sns_sc500ai_obj;
    ot_vi_pipe vi_pipe = vi_cfg->bind_pipe.pipe_id[0];

    ae_lib.id = vi_pipe;
    awb_lib.id = vi_pipe;

    strncpy_s(ae_lib.lib_name, sizeof(ae_lib.lib_name), OT_AE_LIB_NAME, sizeof(OT_AE_LIB_NAME));
    strncpy_s(awb_lib.lib_name, sizeof(awb_lib.lib_name), OT_AWB_LIB_NAME, sizeof(OT_AWB_LIB_NAME));

    // 24.注册AE AWB库回调
    sns_obj->pfn_register_callback(vi_pipe, &ae_lib, &awb_lib);

    sns_bus_info.i2c_dev = vi_cfg->sns_info.bus_id;

    // 25.设置I2C总线信息
    sns_obj->pfn_set_bus_info(vi_pipe, sns_bus_info);

    // 26.注册AE库
    ss_mpi_ae_register(vi_pipe, &ae_lib);

    // 27.注册AWB库
    ss_mpi_awb_register(vi_pipe, &awb_lib);

    // 28.ISP设置控制参数 ISP在FE还是BE运行，以及是否开ISP快速启动 应该用不到
    /*
    ot_isp_ctrl_param isp_ctrl_param = {};
    ss_mpi_isp_get_ctrl_param(vi_pipe, &isp_ctrl_param);
    if (vi_cfg->pipe_info[0].isp_be_end_trigger)
        isp_ctrl_param.isp_run_wakeup_select = OT_ISP_RUN_WAKEUP_BE_END;
    else
        isp_ctrl_param.isp_run_wakeup_select = OT_ISP_RUN_WAKEUP_FE_START;
    isp_ctrl_param.quick_start_en = vi_cfg->pipe_info[0].is_isp_quick_start;
    ss_mpi_isp_set_ctrl_param(vi_pipe, &isp_ctrl_param);
    */

    // 29.ISP内存初始化
    ss_mpi_isp_mem_init(vi_pipe);

    // 30.ISP设置发布属性
    ss_mpi_isp_set_pub_attr(vi_pipe, &vi_cfg->pipe_info[0].isp_info.isp_pub_attr);

    // 31.ISP初始化
    ss_mpi_isp_init(vi_pipe);

    // 32.ISP曝光限制 相比官方新增代码 限制曝光(VMAX)以免影响帧率
    ot_isp_exposure_attr attr;
    ss_mpi_isp_get_exposure_attr(vi_pipe, &attr);
    attr.auto_attr.exp_time_range.max = 10000 * 100 / 100;     // 最后的100是帧率     曝光时间不得超过1000 000 us /100 也就是10ms

    //attr.auto_attr.sys_gain_range.max = 1024 * 10;      // 增益限制
    attr.auto_attr.a_gain_range.max = 24645/2;
    attr.auto_attr.d_gain_range.max = 1024 * 1;
    attr.auto_attr.ispd_gain_range.max = 1024 * 1;

    //attr.auto_attr.compensation = 0x20;                 // AE目标亮度值 让ISP接受暗光环境 不会再增大增益或者曝光时间 默认0x38
    ss_mpi_isp_set_exposure_attr(vi_pipe, &attr);

    // 33.启动ISP
    pthread_attr_t* thread_attr = NULL;
    pthread_create(&g_isp_pid[vi_pipe], thread_attr, sample_comm_isp_thread, (td_void*)(td_uintptr_t)vi_pipe);

    g_start_isp[vi_pipe] = TD_TRUE;
}

static void* sample_comm_isp_thread(td_void* param)
{
    td_s32 ret;
    ot_isp_dev isp_dev;
    td_char thread_name[20];
    isp_dev = (ot_isp_dev)(td_uintptr_t)param;
    errno_t err;

    err = snprintf_s(thread_name, sizeof(thread_name), sizeof(thread_name) - 1, "ISP%d_RUN", isp_dev); /* 20,19 chars */
    if (err < 0) {
        return NULL;
    }
    prctl(PR_SET_NAME, thread_name, 0, 0, 0);

    printf("ISP Dev %d running !\n", isp_dev);
    ret = ss_mpi_isp_run(isp_dev);
    if (ret != TD_SUCCESS) {
        printf("OT_MPI_ISP_Run failed with %#x!\n", ret);
        return NULL;
    }

    return NULL;
}

// 模拟PQTOOL加载isp校准bin文件
void isp_calibration_load_bin()
{
    PQ_BIN_MODULE_S binParam;
    unsigned int tempLen;
    unsigned int size;
    unsigned int ret;
    unsigned char* binBuffer = NULL;

    memset_s(&binParam, sizeof(binParam), 0, sizeof(binParam));
    binParam.stISP.enable = 1;
    binParam.st3DNR.enable = 1;
    binParam.st3DNR.viPipe = 0;

    FILE* fp = fopen("./sc500ai_1080P_100.bin", "r");
    if (fp == NULL) {
        printf("fopen error.\n");
        return;
    }
    ret = fseek(fp, 0, SEEK_END);
    if (ret == -1) {
        printf("fseek end fail \n");
        return;
    }
    size = ftell(fp);
    ret = fseek(fp, 0, SEEK_SET);
    if (ret == -1) {
        printf("fseek set fail \n");
        return;
    }

    binBuffer = (unsigned char*)malloc(size);
    if (binBuffer == NULL) {
        printf("malloc erro\n");
        ret = -1;
        return;
    }
    tempLen = fread(binBuffer, sizeof(unsigned char), size, fp);
    if (tempLen == 0) {
        printf("read erro\n");
        ret = -1;
        return;
    }
    ret = OT_PQ_BIN_ImportBinData(&binParam, binBuffer, size);
    if (ret != 0) {
        printf("OT_PQ_BIN_ImportBinData error(%#x)\n", ret);
    }
    else {
        printf("OT_PQ_BIN_ParseBinData success!\n");
    }
}

void vpss_init(sample_vpss_cfg* vpss_cfg)
{
    const ot_low_delay_info low_delay_info = { TD_TRUE, 1080/4, TD_FALSE };

    // 只有VI VPSS都为在线模式 才能使用VPSS提前中断? 此处注释原代码
    // ot_frame_interrupt_attr frame_interrupt_attr = { 0 };
    // frame_interrupt_attr.interrupt_type = OT_FRAME_INTERRUPT_EARLY_END;
    // frame_interrupt_attr.early_line = vpss_cfg->grp_attr.max_height / 2;
    // ss_mpi_vpss_set_grp_frame_interrupt_attr(0, &frame_interrupt_attr);
    
    // 34.创建VPSS GRP
    ss_mpi_vpss_create_grp(vpss_cfg->vpss_grp, &vpss_cfg->grp_attr);

    // 35.设置VPSS CHN属性
    ss_mpi_vpss_set_chn_attr(vpss_cfg->vpss_grp, 0, &vpss_cfg->chn_attr[0]);

    // 36.设置VPSS CHN低延迟
    ss_mpi_vpss_set_chn_low_delay(vpss_cfg->vpss_grp, 0, &low_delay_info);

    // 37.使能VPSS GRP
    ss_mpi_vpss_start_grp(vpss_cfg->vpss_grp);

    // 38.使能VPSS CHN
    ss_mpi_vpss_enable_chn(vpss_cfg->vpss_grp, 0);
}

void vi_bind_vpss(sample_vi_cfg* vi_cfg, sample_vpss_cfg* vpss_cfg)
{
    ot_mpp_chn src_chn;
    ot_mpp_chn dest_chn;

    src_chn.mod_id = OT_ID_VI;
    src_chn.dev_id = vi_cfg->bind_pipe.pipe_id[0];
    src_chn.chn_id = 0;

    dest_chn.mod_id = OT_ID_VPSS;
    dest_chn.dev_id = vpss_cfg->vpss_grp;
    dest_chn.chn_id = 0;

    // 39.绑定 VI PIPE0 CHN0 和 VPSS GRP0 CHN0
    ss_mpi_sys_bind(&src_chn, &dest_chn);
}

void encoder_init(sample_venc_vpss_chn *venc_vpss_chn, sample_vpss_cfg* vpss_cfg)
{
    venc_vpss_chn->vpss_chn[0] = 0;
    venc_vpss_chn->venc_chn[0] = 0;

    td_u32 frame_rate = 100;
    td_u32 stats_time = 1;              // 降低统计时间
    td_u32 gop = frame_rate*1;         // 1s发一次完整帧 或者 1s完整刷新完一张图像       目前1s
    ot_pic_size size = PIC_1080P;
    td_u64 custom_bit_rate = ((td_u64)4096 * (g_sample_pic_size[size].width * g_sample_pic_size[size].height)) / (1920 * 1080); /* 3072: 3M 1920, 1080: FHD */
    td_u32 profile = 0;
    td_bool is_rcn_ref_share_buf = TD_FALSE;
    ot_payload_type type = OT_PT_H265;

    ot_venc_gop_attr gop_attr;
    gop_attr.gop_mode = OT_VENC_GOP_MODE_NORMAL_P;
    gop_attr.normal_p.ip_qp_delta = 2;

    // h265cbr
    ot_venc_h265_cbr h265_cbr;
    h265_cbr.gop = gop;
    h265_cbr.stats_time = stats_time;
    h265_cbr.src_frame_rate = frame_rate;
    h265_cbr.dst_frame_rate = frame_rate;
    // 码率为4M*分辨率与1080P的比值 单位为K，原厂是5M 30FPS
    h265_cbr.bit_rate = custom_bit_rate;

    // h265avbr
    ot_venc_h265_avbr h265_avbr;
    h265_avbr.gop = gop;
    h265_avbr.stats_time = stats_time;
    h265_avbr.src_frame_rate = frame_rate;
    h265_avbr.dst_frame_rate = frame_rate;
    h265_avbr.max_bit_rate = custom_bit_rate;

    // 由于指定了venc_chn_attr.buf_size所以不需要使用小缓冲区插件了
    /*
    * hi_venc_mod_param mod_param = { 0 };
    * mod_param.mod_type = HI_VENC_MOD_H265;
    * hi_mpi_venc_get_mod_param(&mod_param);
    * mod_param.h265_mod_param.mini_buf_mode = 1;
    * hi_mpi_venc_set_mod_param(&mod_param);
    */
    
    ot_venc_chn_attr venc_chn_attr;
    venc_chn_attr.venc_attr.type = type;
    venc_chn_attr.venc_attr.max_pic_width = g_sample_pic_size[PIC_1080P].width;
    venc_chn_attr.venc_attr.max_pic_height = g_sample_pic_size[PIC_1080P].height;
    venc_chn_attr.venc_attr.pic_width = g_sample_pic_size[PIC_1080P].width;
    venc_chn_attr.venc_attr.pic_height = g_sample_pic_size[PIC_1080P].height;
    venc_chn_attr.venc_attr.buf_size = OT_ALIGN_UP(1920 * 1080 * 3 / 4, 64);                  // 原代码没有 新增缓冲区大小对齐
    venc_chn_attr.venc_attr.profile = 0;
    venc_chn_attr.venc_attr.is_by_frame = TD_FALSE;      // 按帧拿还是切片拿 切片拿快一些
    venc_chn_attr.venc_attr.h265_attr.frame_buf_ratio = 100;        //原值是75
    venc_chn_attr.venc_attr.h265_attr.rcn_ref_share_buf_en = TD_FALSE;      // 原代码rcn_ref_share_buf_en是启用的
    venc_chn_attr.gop_attr = gop_attr;

    venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H265_CBR;
    venc_chn_attr.rc_attr.h265_cbr = h265_cbr;
    //venc_chn_attr.rc_attr.rc_mode = OT_VENC_RC_MODE_H265_AVBR;       // H265 AVBR
    //venc_chn_attr.rc_attr.h265_avbr = h265_avbr;                      // H265 AVBR

    // 40.创建VENC通道
    ss_mpi_venc_create_chn(venc_vpss_chn->venc_chn[0], &venc_chn_attr);         //venc_chn_attr必须

    // 41.设置码流控制参数
    ot_venc_rc_param tmp_rc_param;
    ss_mpi_venc_get_rc_param(venc_vpss_chn->venc_chn[0], &tmp_rc_param);

    tmp_rc_param.h265_cbr_param.max_reencode_times = 0;
    tmp_rc_param.h265_cbr_param.min_i_qp = 18;          // 
    tmp_rc_param.h265_cbr_param.max_i_qp = 42;          // 42       51
    tmp_rc_param.h265_cbr_param.min_qp = 20;            //
    tmp_rc_param.h265_cbr_param.max_qp = 45;            // 45       51

    //tmp_rc_param.h265_avbr_param.max_reencode_times = 0;    // 不允许重编码
    //tmp_rc_param.h265_avbr_param.min_i_qp = 22;          // 限制静态画质上限，防止码率虚高
    //tmp_rc_param.h265_avbr_param.max_i_qp = 42;          // I 帧最差画质底线
    //tmp_rc_param.h265_avbr_param.min_qp = 24;            // P 帧 QP 通常比 I 帧略高
    //tmp_rc_param.h265_avbr_param.max_qp = 38;            // 重点！把最大 QP 放宽到 48，剧烈运动时允许出马赛克来保住帧率

    tmp_rc_param.first_frame_start_qp = -1;         // 第一帧的QP自适应
    tmp_rc_param.scene_chg_detect.adapt_insert_idr_frame_en = TD_FALSE;      // 自适应插入IDR帧 即时解码刷新帧 特殊I帧 会清空所有历史缓存数据 
    tmp_rc_param.scene_chg_detect.detect_scene_chg_en = TD_FALSE;                // 监测场景变化

    ss_mpi_venc_set_rc_param(venc_vpss_chn->venc_chn[0], &tmp_rc_param);


    // 42.开启切片功能    新增
    ot_venc_slice_split slice_param;
    ss_mpi_venc_get_slice_split(venc_vpss_chn->venc_chn[0], &slice_param);
    slice_param.split_mode = 1;             // 按宏块或者LCU行分割          这里旋转的LCU行   一个LCU 64x64像素
    slice_param.enable = 1;                 // 开启切片
    slice_param.slice_output_en = 1;        // 切片输出启用
    slice_param.split_size = 4;             // 每个切片包含4个LCU
    ss_mpi_venc_set_slice_split(venc_vpss_chn->venc_chn[0], &slice_param);


    // 43.开启长参考帧 新增
    ot_venc_ref_param ref_param;
    ss_mpi_venc_get_ref_param(venc_vpss_chn->venc_chn[0], &ref_param);
    ref_param.pred_en = 1;
    ref_param.enhance = 0;
    ref_param.base = 1;
    ss_mpi_venc_set_ref_param(venc_vpss_chn->venc_chn[0], &ref_param);


    // 44.开启丢大包策略 新增           (防止大包堆积)
    ot_venc_frame_lost_strategy lost_param;
    ss_mpi_venc_get_frame_lost_strategy(venc_vpss_chn->venc_chn[0], &lost_param);
    lost_param.enable = 1;
    lost_param.mode = OT_VENC_FRAME_LOST_P_SKIP;
    lost_param.bit_rate_threshold = custom_bit_rate * 1024 * 1.2;  // 最大允许超过1.2倍码率就丢包
    lost_param.frame_gap = 1;   // 如果超过码率 丢包
    ss_mpi_venc_set_frame_lost_strategy(venc_vpss_chn->venc_chn[0], &lost_param);

    // 45.开启ROI编码 感兴趣区域为中间 抛弃周围1/4 新增         禁用 没什么作用
    //ot_venc_roi_attr roi_config = { 0 };
    ////uint16_t roi_qp = 20;
    //roi_config.enable = TD_TRUE;
    //roi_config.is_abs_qp = TD_FALSE;        // 相对QP模式
    //roi_config.qp = (uint16_t)(-3);         // 中心画质优于边缘
    //roi_config.idx = 0;
    //roi_config.rect.x = OT_ALIGN_UP(1920 / 4, 16);
    //roi_config.rect.y = OT_ALIGN_UP(1080 / 4, 16);
    //roi_config.rect.width = OT_ALIGN_UP(1920 / 2, 16);
    //roi_config.rect.height = OT_ALIGN_UP(1080 / 2, 16);
    //roi_config.is_abs_qp = TD_TRUE;
    //ss_mpi_venc_set_roi_attr(venc_vpss_chn->venc_chn[0], &roi_config);

    // 46.开启P帧内刷新   (防止大包堆积)
    ot_venc_intra_refresh intra_refresh;
    ss_mpi_venc_get_intra_refresh(venc_vpss_chn->venc_chn[0], &intra_refresh);
    intra_refresh.enable = TD_TRUE;
    // OT_VENC_INTRA_REFRESH_ROW 按行刷新，OT_VENC_INTRA_REFRESH_MACROBLOCK 按宏块刷新
    intra_refresh.mode = OT_VENC_INTRA_REFRESH_ROW;
    // 每次刷新多少行 CTU，每次刷新1行CTU，也就是64像素，刷完整帧需要1080/64=17次  一帧只有一行CTU刷新
    intra_refresh.refresh_num = 1;      
    intra_refresh.request_i_qp = 30; // 刷新块的 QP 值，根据你的画质需求微调（通常比正常 P 帧稍低或持平）
    ss_mpi_venc_set_intra_refresh(venc_vpss_chn->venc_chn[0], &intra_refresh);

    // 47.启动编码通道
    ot_venc_start_param start_param = { 0 };
    start_param.recv_pic_num = -1;
    ss_mpi_venc_start_chn(venc_vpss_chn->venc_chn[0], &start_param);            //start_param必须

}

void vpss_bind_venc(sample_venc_vpss_chn* venc_vpss_chn, sample_vpss_cfg* vpss_cfg)
{
    ot_mpp_chn src_chn;
    ot_mpp_chn dest_chn;
    src_chn.mod_id = OT_ID_VPSS;
    src_chn.dev_id = vpss_cfg->vpss_grp;
    src_chn.chn_id = venc_vpss_chn->vpss_chn[0];

    dest_chn.mod_id = OT_ID_VENC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = venc_vpss_chn->venc_chn[0];

    ss_mpi_sys_bind(&src_chn, &dest_chn);
}

struct timespec last_timestamp = { 0, 0 };
uint32_t bytes_sent = 0;
uint32_t frames_sent = 0;
uint64_t seq_last = 0;

uint64_t jitter_sum = 0;
uint64_t jitter_cnt = 0;
uint32_t nal_max_size = 0;
uint32_t single_packets = 0;

uint32_t pps_count = 0;
uint32_t sps_count = 0;
uint32_t idr_count = 0;
uint32_t sei_count = 0;
uint32_t vps_count = 0;
uint32_t s_count = 0;
uint32_t packets_sent = 0;

uint32_t sequence_id = 0;
uint32_t frame_id = 0;
uint16_t rtp_sequence = 0;

// 新增RTP时间戳和帧结束位标志
void transmit(int socket_handle, uint8_t* tx_buffer, uint32_t tx_size, struct sockaddr* dst_address, uint32_t rtp_timestamp, bool marker_bit) {

    switch (stream_mode) {
        // UDP包直发
    case 0:
        sendto(socket_handle, tx_buffer, tx_size, 0, dst_address, sizeof(struct sockaddr_in));
        break;

        // 发RTP包
    case 1:
    {
        struct RTPHeader rtp_header;
        rtp_header.version = 0x80;
        rtp_header.sequence = htobe16(rtp_sequence++);

        //rtp_header.payload_type = 0x60;
        rtp_header.payload_type = marker_bit ? (0x60 | 0x80) : 0x60;        // 如果是最后一包 将最高位置1   0x60是H265标志位

        rtp_header.timestamp = htobe32(rtp_timestamp);
        rtp_header.ssrc_id = htobe32(0xDEADBEEF);

        // 等待测试的双缓冲区发送 默认情况是要拷贝payload和RTP包进行组合
        // 通过msghdr发送可以实现0拷贝
        // struct iovec iov[2];
        // iov[0].iov_base = &rtp_header;
        // iov[0].iov_len = sizeof(struct RTPHeader);
        // iov[1].iov_base = tx_buffer;
        // iov[1].iov_len = tx_size;

        // struct msghdr msg;
        // msg.msg_iovlen = 2;
        // msg.msg_iov = iov;
        // msg.msg_name = dst_address;
        // msg.msg_namelen = sizeof(struct sockaddr_in);

        // sendmsg(socket_handle, &msg, 0);

        //uint8_t* combined_buffer = (uint8_t*)malloc(sizeof(struct RTPHeader) + tx_size);    性能不行 改为全局变量
        memcpy(g_combined_buffer, &rtp_header, sizeof(struct RTPHeader));
        memcpy(g_combined_buffer + sizeof(struct RTPHeader), tx_buffer, tx_size);
        sendto(socket_handle, g_combined_buffer, sizeof(struct RTPHeader) + tx_size,
            0, dst_address, sizeof(struct sockaddr_in));
        //free(combined_buffer);    全局变量无需free
        break;
    }
    }
}

void sendPacket(uint8_t* pack_data, uint32_t pack_size, int socket_handle, struct sockaddr* dst_address, uint32_t max_size, uint32_t rtp_timestamp, bool is_frame_end) {

    uint8_t prefix = 0;

    if (pack_size < 4) return;      // 如果进来的数据甚至低于4BYTE 则直接退出   防止以下代码越界访问
    if (pack_data[0] == 0 && pack_data[1] == 0 && pack_data[2] == 0 && pack_data[3] == 1) {     // 正常H265数据帧前四位是 0 0 0 1 静默处理
        prefix = 4;
    }
    else if (pack_data[0] == 0 && pack_data[1] == 0 && pack_data[2] == 1) {             // 特殊H265数据帧 0 0 1  打印显示并赋值
        prefix = 3;
        printf("3BYTE NALU Separator\n");
    }
    else {
        printf("Anomaly NALU Separator\n");         // 不是4B又不是3B格式的Annex B格式数据帧不接受
        return;
    }

    uint8_t fu_size = 3;            // H265 帧头 3Byte FU头

    pack_data += prefix;    // 剔除NALU帧间隔
    pack_size -= prefix;

    frame_id++;
    frames_sent++;

    uint8_t nal_type_hevc = (pack_data[0] >> 1) & 0x3F;       // H265 nal帧类型
    uint8_t nal_bits_hevc = pack_data[0] & 0x81;         // H265 0x1000 0001     得到禁止位和层级标识

    // 判断帧类型是否为0/1/19/20/21 这是I帧或者是P/B帧
    bool is_image_frame = (nal_type_hevc == 0 || nal_type_hevc == 1 || nal_type_hevc == 19 || nal_type_hevc == 20 || nal_type_hevc == 21);

    if (pack_size > nal_max_size) {
        nal_max_size = pack_size;   // 记录最大包
    }

    // 统计NALU类型数量
    switch (nal_type_hevc) {
    case 0:
    case 1:                 // TRAIL_N / TRAIL_R (非 I 帧，通常是 P 帧或 B 帧)
        s_count++;
        break;
    case 19:                // IDR_W_RADL (带有随机接入的瞬时解码刷新，即典型的 I 帧)
    case 20:                // IDR_N_LP (无前导图片的瞬时解码刷新，也是 I 帧)
    case 21:                // CRA_NUT (干净随机接入，类似 I 帧)
        idr_count++;
        break;
    case 32:                // VPS (视频参数集，H.265 特有！极度重要)
        vps_count++;
        break;
    case 33:                // SPS (序列参数集，视频宽、高、帧率等)
        sps_count++;
        break;
    case 34:                // PPS (图像参数集，初始量化参数 QP 等)
        pps_count++;
        break;
    case 39:                // Prefix SEI (补充增强信息)
    case 40:                // Suffix SEI
        sei_count++;
        break;
    default:
        break;
    }

    // 小于最大 RTP Payload尺寸(1405) 无论是什么类型的包都直接发送
    if (pack_size <= max_size) {
        //transmit(socket_handle, pack_data, pack_size, dst_address);
        transmit(socket_handle, pack_data, pack_size, dst_address, rtp_timestamp, is_frame_end);
        packets_sent++;
        single_packets++;           // 统计小于H265_PAYLOAD包的数量，也就是能直发包的数量
        return;
    }

    // 如果大于 RTP PAYLOAD 1405B 而且是图像 那么拆包 分包前有3B的FU
    if (is_image_frame) {

        uint8_t tx_size = 3; // H265 FU 头部 3Byte
        uint32_t max_chunk_payload = max_size - tx_size; // 最大PAYLOAD载荷，例如 1405 - 3 = 1402
        bool start_bit = true;

        pack_data += 2;     // 减去NALU头 2BYTE 使用FU切片头 3BYTE代替
        pack_size -= 2;

        while (pack_size > 0) { // 循环拆包 直到剩下大小为0
            // 如果剩余发送数据量>最大载荷大小 发送最大载荷大小 否则直接发送剩余数据
            uint32_t chunk_size = pack_size > max_chunk_payload ? max_chunk_payload : pack_size;        
            bool current_marker = false;        // 切片包默认结束位是False

            // 拼装 H265 FU 头部
            g_tx_buffer[0] = nal_bits_hevc | (49 << 1);
            g_tx_buffer[1] = 1;
            g_tx_buffer[2] = nal_type_hevc;

            if (start_bit) {                            // 非最后一个分包
                g_tx_buffer[2] |= 0x80;   // 开始标志位 0x1000 0000        最高位置1
                start_bit = false;
            }
            else if (chunk_size == pack_size) {         // 最后一个分包
                g_tx_buffer[2] |= 0x40;   // 结束标志位 0x0100 0000        次高位置1
                if (is_frame_end) {
                    current_marker = true;      // 结束位置True
                }
            }

            memcpy(g_tx_buffer + tx_size, pack_data, chunk_size);
            transmit(socket_handle, g_tx_buffer, chunk_size + tx_size, dst_address, rtp_timestamp, current_marker);

            packets_sent++;
            bytes_sent += chunk_size + tx_size;     // 分包大小=数据大小+FU头
            pack_data += chunk_size;
            pack_size -= chunk_size;
        }
    }
    else {
        // 非图像包 大包 直接丢弃
        printf("WARNING: Dropped massive non-VCL NALU! Type: %d, Size: %d\n", nal_type_hevc, pack_size);
    }
}

int processStream(ot_venc_chn channel_id, int socket_handle, struct sockaddr* dst_address, uint16_t max_frame_size) {

    // 查询编码器通道状态
    ot_venc_chn_status channel_status;
    int ret = ss_mpi_venc_query_status(channel_id, &channel_status);
    if (ret != TD_SUCCESS) {
        printf("WARN: Unable to query VENC status = 0x%x\n", ret);
        usleep(100000);
        return 0;
    }

    // 如果编码器通道中没有包则继续循环
    if (!channel_status.cur_packs) {
        return 0;
    }

    // 切片输出模式一次只返回一个切片
    // 帧输出模式一次可能会输出多个切片
    ot_venc_pack packet_descriptor[32];

    // 创建流缓存
    ot_venc_stream stream;
    memset(&stream, 0x00, sizeof(stream));
    stream.pack = packet_descriptor;
    stream.pack_cnt = channel_status.cur_packs;

    // 请求编码流
    ret = ss_mpi_venc_get_stream(channel_id, &stream, 0);
    if (ret != TD_SUCCESS) {
        printf("WARN: Failed to get VENC stream  = 0x%x. Current packs = %d\n",
            ret, channel_status.cur_packs);
        usleep(100000);
        return 0;
    }

    // 发送编码后的包
    for (uint32_t i = 0; i < stream.pack_cnt; i++) {
        uint32_t hw_rtp_ts = (uint32_t)((stream.pack[i].pts * 9) / 100);    // 记录pack pts时间
        bool is_end = stream.pack[i].is_frame_end;      // 该pack是否为整帧尾巴

        sendPacket(stream.pack[i].addr + stream.pack[i].offset,         // payload地址 = 基地址+数据包起始偏移(硬件设计的内存对齐引起的)
            stream.pack[i].len - stream.pack[i].offset,                 // payload长度 = 总长度-数据包起始偏移
            socket_handle, dst_address, max_frame_size, hw_rtp_ts, is_end);                // socket句柄 接收方地址 MTU大小
    }

    // 释放编码流
    ss_mpi_venc_release_stream(channel_id, &stream);

    // 打印速率状态
    struct timespec current_timestamp;
    if (!clock_gettime(CLOCK_MONOTONIC_COARSE, &current_timestamp)) {
        double interval = getTimeInterval(&current_timestamp, &last_timestamp);
        if (interval > 1) {
            printf("> Rate: %.2f Mbit/sec. (%.1f pps) | Frames: %d, NotFrag: "
                "%d | AVG Size: %d, MAX Size: %d | S: %d, IDR: %d, SEI: %d, "
                "PPS: %d, SPS: %d, VPS: %d | Packets: %d\n",
                ((double)bytes_sent * 8) / interval / 1024 / 1024,
                (double)frames_sent / interval, /* jitter_sum / jitter_cnt,*/
                frames_sent, single_packets, bytes_sent / frames_sent,
                nal_max_size, s_count, idr_count, sei_count, pps_count,
                sps_count, vps_count, packets_sent);

            bytes_sent = 0;
            frames_sent = 0;
            jitter_sum = 0;
            jitter_cnt = 0;
            nal_max_size = 0;
            s_count = 0;
            idr_count = 0;
            pps_count = 0;
            sps_count = 0;
            sei_count = 0;
            vps_count = 0;
            single_packets = 0;
            packets_sent = 0;
            last_timestamp = current_timestamp;
        }
    }
    return 1;
}

static void handler(int value) {
    g_loop_running = false;
}

void rtp_send(sample_venc_vpss_chn* venc_vpss_chn)
{
    uint32_t udp_sink_ip = inet_addr("192.168.0.2");
    uint16_t udp_sink_port = 5622;
    uint16_t max_frame_size = 1405;     // MTU=1445，实际WFB支持MTU为1445，H265 RTP头12B，UDP头8B，IP头20B    40B的协议开销 实际载荷大小应该小于1405B

    int socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst_addr;
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(udp_sink_port);
    dst_addr.sin_addr.s_addr = udp_sink_ip;

    // socket udp 异步模式代码 允许丢包 不允许阻塞
    int flags = fcntl(socket_handle, F_GETFL, 0);
    fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK);
    int sndbuf = 2 * 1024 * 1024;
    setsockopt(socket_handle, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    // socket udp 异步模式代码 允许丢包 不允许阻塞

    // 分配payload发送缓冲区64K
    g_tx_buffer = malloc(65536);
    // 分配RTP+payload发送缓冲区64K
    g_combined_buffer = malloc(65536);
    printf("> Ready for streaming\n");
    signal(SIGINT, handler);

    while (g_loop_running) {
        // 循环查询数据流和发送
        if (!processStream(venc_vpss_chn->venc_chn[0], socket_handle,
            (struct sockaddr*)&dst_addr, max_frame_size)) {
            usleep(1);
        }
    }
}

td_s32 main(td_s32 argc, td_char* argv[])
{
    sampe_sys_cfg sys_cfg = { 0 };

    sample_vi_cfg vi_cfg = { 0 };
    (td_void)memset_s(&vi_cfg, sizeof(sample_vi_cfg), 0, sizeof(sample_vi_cfg));

    sample_vpss_cfg vpss_cfg = { 0 };

    sample_venc_vpss_chn venc_vpss_chn = { 0 };

    // 第一部分 设置VI
    set_vi_sys_cfg(&sys_cfg);

    // 设置vi_cfg->sns_info
    set_vi_sns_info(&vi_cfg.sns_info);

    // 设置vi_cfg->mipi_info
    set_vi_mipi_info(&vi_cfg.mipi_info);

    // 设置vi_cfg->dev_info
    set_dev_info(&vi_cfg.dev_info);

    // 设置vi_cfg->bind_pipe
    set_vi_bind_pipe(&vi_cfg.bind_pipe);

    // 设置vi_cfg->grp_info
    set_vi_grp_info(&vi_cfg.grp_info);

    // 设置vi_cfg->pipe_info
    set_vi_pipe_info(vi_cfg.pipe_info);


    // 第二部分 设置VPSS
    
    // 设置vpss_cfg->grp_attr
    set_vpss_grp(&vpss_cfg.grp_attr);

    // 设置vpss_cfg->chn_attr
    set_vpss_chn(vpss_cfg.chn_attr);

    // 设置其他vpss_cfg属性
    set_vpss_cfg(&vpss_cfg);


    // 第三部分 SYS 初始化
    sys_init(&sys_cfg);


    // 第四部分 VI初始化
    vi_init(&vi_cfg);


    // 第五部分 ISP初始化      ISP线程名称ISP0_RUN
    true_isp_init(&vi_cfg);

    // 第五部分 第二步 加载ISP灯箱校准文件
    isp_calibration_load_bin();

    // 第六部分 VPSS初始化
    vpss_init(&vpss_cfg);

    // 绑定VI和VPSS
    vi_bind_vpss(&vi_cfg, &vpss_cfg);

    // 第七部分 Encoder初始化
    encoder_init(&venc_vpss_chn, &vpss_cfg);

    // 绑定VPSS和VENC
    vpss_bind_venc(&venc_vpss_chn, &vpss_cfg);

    // 第八部分 获取编码流和RTP发送
    rtp_send(&venc_vpss_chn);

    exit(0);

}

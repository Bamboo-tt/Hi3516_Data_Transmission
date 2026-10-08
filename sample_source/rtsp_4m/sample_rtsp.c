/*
 * Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 *
 * ============================================================================
 *  RTSP-4M 精简版示例   （由原 3700 行 sample_rtsp.c 精简而来）
 * ============================================================================
 *
 *  这个程序做的事情，一句话概括：
 *      把摄像头采集到的画面，编码成 H.264，再通过 RTSP 协议推到网络上，
 *      手机/电脑用 VLC 播放器输入 rtsp://<开发板IP>/test.264 就能实时看画面。
 *
 *  整条数据流水线（海思 SDK 的标准套路）：
 *
 *      VI  ──►  VPSS  ──►  VENC  ──►  本程序取流线程  ──►  RTSP 推流
 *    (采集)   (缩放/降噪)  (编码)        (select 等数据)      (发到网络)
 *
 *      VI   : Video Input，从 sensor(图像传感器) 拿原始画面
 *      VPSS : Video Process Sub-System，做缩放、降噪等处理，并分出多路
 *      VENC : Video Encoder，把 YUV 画面压缩成 H.264 码流
 *
 *  本示例开了 2 路编码：
 *      通道0 : 2560x1440 (4M 主码流)  ← 这一路推到 RTSP
 *      通道1 : 1024x768  (子码流)     ← 只编码不推流（可按需扩展）
 *
 *  程序启动后一直运行，按 Ctrl+C 收到信号后优雅退出。
 *
 *  原始文件里有大量从 sample_comm_venc.c 整段拷贝、但本示例根本没用到的代码
 *  （各种码率控制变体、JPEG 缩略图、QPMAP/SVC 等），本精简版已全部删除，
 *  只保留 RTSP 推流真正走到的那条路径。
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/prctl.h>

#include "sample_comm.h"
#include "securec.h"
#include "rtsp_demo.h"      /* 第三方简易 RTSP 服务器（librtsp.a） */

/* ============================ 全局配置 / 全局变量 ============================ */

#define RTSP_PORT           554                 /* RTSP 标准端口 */
#define RTSP_STREAM_PATH    "/test.264"         /* 播放地址：rtsp://<IP>/test.264 */

#define BIG_STREAM_SIZE     PIC_2560X1440       /* 主码流分辨率：4M */
#define SMALL_STREAM_SIZE   PIC_1024X768        /* 子码流分辨率 */

#define CHN_NUM_MAX         2                    /* 一共 2 路编码通道 */
#define VPSS_GRP            0                    /* 使用的 VPSS group 号 */

/* RTSP 服务器句柄与会话句柄：取流线程要用，所以做成全局 */
rtsp_demo_handle    g_rtsplive = NULL;
rtsp_session_handle g_session  = NULL;

/* 收到 Ctrl+C / kill 后置为 TRUE，主循环据此退出 */
static volatile td_bool g_sample_venc_exit = TD_FALSE;

/* 取流线程的线程号 + 传给线程的参数（线程开关、通道号等，类型来自 SDK 头文件） */
static pthread_t g_venc_pid;
static sample_venc_getstream_para g_para = {
    .thread_start = TD_FALSE,
    .cnt = 0,
};

/* 描述每路编码通道的运行时信息（取流线程内部用） */
typedef struct {
    td_s32 venc_fd[CHN_NUM_MAX];   /* 每个 VENC 通道的文件描述符，用 select 等待"有新码流"事件 */
    td_s32 max_fd;                 /* 所有 fd 里的最大值，select 的第一个参数要用 */
    td_s32 chn_total;              /* 实际通道数量 */
} venc_stream_ctx;

/* ============================ 信号处理：优雅退出 ============================ */

/* 捕获 Ctrl+C(SIGINT) / kill(SIGTERM)，只置一个标志位，让主循环自己跳出 */
static td_void sample_venc_handle_sig(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        g_sample_venc_exit = TD_TRUE;
    }
}

/* ====================== 核心：从 VENC 取一帧并推到 RTSP ====================== */

/*
 * 处理"某个编码通道有一帧新码流"事件：
 *   1. 问 VENC 这一帧被切成了几个包(pack)
 *   2. 申请内存接收这些包
 *   3. 取出整帧码流
 *   4. 逐包推送到 RTSP（只推主码流通道0）
 *   5. 释放码流、释放内存
 */
static td_s32 venc_get_and_send_one_frame(td_s32 chn)
{
    ot_venc_stream stream;
    ot_venc_chn_status status;
    td_s32 ret;
    td_u32 j;

    (td_void)memset_s(&stream, sizeof(stream), 0, sizeof(stream));

    /* 1. 查询当前帧由几个包组成 */
    if (ss_mpi_venc_query_status(chn, &status) != TD_SUCCESS) {
        sample_print("venc chn[%d] query status failed!\n", chn);
        return TD_FAILURE;
    }
    if (status.cur_packs == 0) {
        return TD_SUCCESS;   /* 暂时没有完整帧，直接返回 */
    }

    /* 2. 按包数申请存放包信息的内存 */
    stream.pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * status.cur_packs);
    if (stream.pack == TD_NULL) {
        sample_print("malloc stream pack failed!\n");
        return TD_FAILURE;
    }
    stream.pack_cnt = status.cur_packs;

    /* 3. 真正把这一帧码流取出来 */
    ret = ss_mpi_venc_get_stream(chn, &stream, TD_TRUE);
    if (ret != TD_SUCCESS) {
        sample_print("venc chn[%d] get stream failed: 0x%x\n", chn, ret);
        free(stream.pack);
        return TD_FAILURE;
    }

    /* 4. 只把主码流(通道0)推到 RTSP；子码流这里忽略，需要的话可自行扩展 */
    if (chn == 0 && g_rtsplive != NULL) {
        for (j = 0; j < stream.pack_cnt; j++) {
            /* SEI 是附加信息帧，播放器不需要，跳过它 */
            if (stream.pack[j].data_type.h264_type == OT_VENC_H264_NALU_SEI) {
                continue;
            }
            /* addr+offset 才是真正的码流起点；len-offset 是有效长度 */
            td_u8 *data = stream.pack[j].addr + stream.pack[j].offset;
            td_s32 size = stream.pack[j].len - stream.pack[j].offset;
            rtsp_sever_tx_video(g_rtsplive, g_session, data, size, stream.pack[j].pts);
        }
    }

    /* 5. 用完必须归还码流缓冲区，否则 VENC 很快就没 buffer 可用 */
    if (ss_mpi_venc_release_stream(chn, &stream) != TD_SUCCESS) {
        sample_print("venc chn[%d] release stream failed!\n", chn);
    }
    free(stream.pack);
    return TD_SUCCESS;
}

/*
 * 取流线程主体：
 *   先拿到每个通道的 fd，然后用 select() 阻塞等待"哪个通道来了新数据"，
 *   谁有数据就去取谁、推谁。这样不用空转 CPU。
 */
static td_void *venc_get_stream_proc(td_void *arg)
{
    sample_venc_getstream_para *para = (sample_venc_getstream_para *)arg;
    venc_stream_ctx ctx = {0};
    fd_set read_fds;
    struct timeval tv;
    td_s32 i, ret;

    prctl(PR_SET_NAME, "get_venc_stream", 0, 0, 0);  /* 给线程起个名字，方便调试 */

    /* 取出每个通道的 fd，并记录最大 fd */
    ctx.chn_total = para->cnt;
    for (i = 0; i < ctx.chn_total && i < CHN_NUM_MAX; i++) {
        ctx.venc_fd[i] = ss_mpi_venc_get_fd(para->venc_chn[i]);
        if (ctx.venc_fd[i] < 0) {
            sample_print("get venc fd[%d] failed!\n", i);
            para->thread_start = TD_FALSE;
            return TD_NULL;
        }
        if (ctx.venc_fd[i] > ctx.max_fd) {
            ctx.max_fd = ctx.venc_fd[i];
        }
    }

    /* 主循环：直到被通知停止 */
    while (para->thread_start == TD_TRUE) {
        FD_ZERO(&read_fds);
        for (i = 0; i < ctx.chn_total && i < CHN_NUM_MAX; i++) {
            FD_SET(ctx.venc_fd[i], &read_fds);
        }

        tv.tv_sec = 2;      /* 最多等 2 秒 */
        tv.tv_usec = 0;
        ret = select(ctx.max_fd + 1, &read_fds, TD_NULL, TD_NULL, &tv);
        if (ret < 0) {
            sample_print("select failed!\n");
            break;
        }
        if (ret == 0) {
            continue;       /* 2 秒内没数据，再等一轮 */
        }

        /* 检查每个通道，谁就绪就处理谁 */
        for (i = 0; i < ctx.chn_total && i < CHN_NUM_MAX; i++) {
            if (FD_ISSET(ctx.venc_fd[i], &read_fds)) {
                venc_get_and_send_one_frame(para->venc_chn[i]);
            }
        }
    }

    para->thread_start = TD_FALSE;
    return TD_NULL;
}

/* 启动取流线程 */
static td_s32 venc_start_get_stream(const ot_venc_chn *venc_chn, td_s32 cnt)
{
    td_s32 i;

    if (cnt > CHN_NUM_MAX) {
        sample_print("chn count %d invalid\n", cnt);
        return TD_FAILURE;
    }

    g_para.thread_start = TD_TRUE;
    g_para.cnt = cnt;
    for (i = 0; i < cnt; i++) {
        g_para.venc_chn[i] = venc_chn[i];
    }

    if (pthread_create(&g_venc_pid, 0, venc_get_stream_proc, &g_para) != 0) {
        sample_print("create get-stream thread failed!\n");
        g_para.thread_start = TD_FALSE;
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

/* 通知取流线程退出并回收它 */
static td_void venc_stop_get_stream(td_void)
{
    if (g_para.thread_start == TD_TRUE) {
        g_para.thread_start = TD_FALSE;
        pthread_join(g_venc_pid, 0);
    }
}

/* ========================== VENC 编码通道的配置与启停 ========================== */

/*
 * 填写两路编码通道的参数：
 *   都用 H.264 + CBR(固定码率)，30fps，GOP=60。
 *   通道0=主码流(大分辨率)，通道1=子码流(小分辨率)。
 */
static td_void venc_set_chn_param(const ot_size *enc_size, sample_comm_venc_chn_param *chn_param,
    ot_venc_gop_attr gop_attr)
{
    td_s32 i;

    for (i = 0; i < CHN_NUM_MAX; i++) {
        chn_param[i].frame_rate = 30;                                  /* 帧率 30fps */
        chn_param[i].gop = 60;                                         /* 每 60 帧一个 I 帧 */
        chn_param[i].stats_time = 2;                                   /* 码率统计周期(秒) */
        chn_param[i].gop_attr = gop_attr;                             /* GOP 结构 */
        chn_param[i].type = OT_PT_H264;                              /* 编码格式：H.264 */
        chn_param[i].size = sample_comm_sys_get_pic_enum(&enc_size[i]);/* 分辨率枚举 */
        chn_param[i].rc_mode = SAMPLE_RC_CBR;                        /* 码控：固定码率 */
        chn_param[i].profile = 0;                                     /* baseline profile */
        chn_param[i].is_rcn_ref_share_buf = TD_TRUE;                 /* 共享参考帧 buffer，省内存 */
    }
}

/*
 * 启动编码：
 *   对每一路：启动 VENC 通道，并把对应的 VPSS 输出通道绑定到它，
 *   这样 VPSS 处理完的画面会自动喂给 VENC 编码。
 *   最后启动取流线程开始推流。
 */
static td_s32 venc_start_encode(const ot_size *enc_size)
{
    sample_comm_venc_chn_param chn_param[CHN_NUM_MAX] = {0};
    ot_venc_gop_attr gop_attr;
    ot_venc_chn venc_chn[CHN_NUM_MAX];
    td_s32 i, ret;

    /* GOP 用最普通的 "全 P 帧" 结构即可 */
    if ((ret = sample_comm_venc_get_gop_attr(OT_VENC_GOP_MODE_NORMAL_P, &gop_attr)) != TD_SUCCESS) {
        sample_print("get gop attr failed: 0x%x\n", ret);
        return ret;
    }

    venc_set_chn_param(enc_size, chn_param, gop_attr);
    sample_comm_venc_mini_buf_en(chn_param, CHN_NUM_MAX);  /* 开启小 buffer 模式，省内存 */

    /* 逐路：启动 VENC + 绑定 VPSS。失败则回退已启动的通道 */
    for (i = 0; i < CHN_NUM_MAX; i++) {
        venc_chn[i] = i;
        if ((ret = sample_comm_venc_start(i, &chn_param[i])) != TD_SUCCESS) {
            sample_print("venc start chn[%d] failed: 0x%x\n", i, ret);
            goto rollback;
        }
        if ((ret = sample_comm_vpss_bind_venc(VPSS_GRP, i, i)) != TD_SUCCESS) {
            sample_print("vpss bind venc chn[%d] failed: 0x%x\n", i, ret);
            sample_comm_venc_stop(i);
            goto rollback;
        }
    }

    /* 一切就绪，启动取流推流线程 */
    if ((ret = venc_start_get_stream(venc_chn, CHN_NUM_MAX)) != TD_SUCCESS) {
        i = CHN_NUM_MAX;   /* 让 rollback 把所有通道都停掉 */
        goto rollback;
    }
    return TD_SUCCESS;

rollback:
    while (--i >= 0) {
        sample_comm_vpss_un_bind_venc(VPSS_GRP, i, i);
        sample_comm_venc_stop(i);
    }
    return TD_FAILURE;
}

/* 停止编码：先停取流线程，再解绑并停掉每个 VENC 通道 */
static td_void venc_stop_encode(td_void)
{
    td_s32 i;

    venc_stop_get_stream();
    for (i = 0; i < CHN_NUM_MAX; i++) {
        sample_comm_vpss_un_bind_venc(VPSS_GRP, i, i);
        sample_comm_venc_stop(i);
    }
}

/* ================= 系统/VI/VPSS 初始化（4M sensor 在线-wrap 模式） ================= */

/* 配置 VB(视频缓冲池)：每路一个子码流池 + 一个 VPSS wrap 环形缓冲池 */
static td_void get_vb_cfg(ot_vb_cfg *vb_cfg, const ot_size *enc_size, td_u32 wrap_size)
{
    ot_vb_calc_cfg calc_cfg;
    ot_pic_buf_attr buf_attr;
    td_s32 i;

    (td_void)memset_s(vb_cfg, sizeof(ot_vb_cfg), 0, sizeof(ot_vb_cfg));
    vb_cfg->max_pool_cnt = 128;

    /* 为子码流(通道1)分配缓冲池；通道0 用 wrap 模式不占常规池 */
    for (i = 1; i < CHN_NUM_MAX; i++) {
        buf_attr.width = enc_size[i].width;
        buf_attr.height = enc_size[i].height;
        buf_attr.align = OT_DEFAULT_ALIGN;
        buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
        buf_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        buf_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
        ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);

        vb_cfg->common_pool[i].blk_size = calc_cfg.vb_size;
        vb_cfg->common_pool[i].blk_cnt = 3;
    }

    /* 最后一个池给 VPSS wrap 环形缓冲使用 */
    vb_cfg->common_pool[i].blk_cnt = 1;
    vb_cfg->common_pool[i].blk_size = wrap_size;
}

/* 系统级配置：VI 和 VPSS 都用在线(online)模式，并开启 VPSS wrap */
static td_void get_sys_cfg(sampe_sys_cfg *sys_cfg)
{
    sys_cfg->route_num = 1;
    sys_cfg->mode_type = OT_VI_ONLINE_VPSS_ONLINE;
    sys_cfg->vpss_wrap_en = TD_TRUE;
}

/* VPSS 配置：按每路编码分辨率设置各通道，并打开通道0的 wrap */
static td_void get_vpss_cfg(sample_sns_type sns_type, sample_vpss_cfg *vpss_cfg, const ot_size *enc_size)
{
    ot_size in_size;
    td_u32 max_w, max_h;
    ot_vpss_chn chn;

    sample_comm_vi_get_size_by_sns_type(sns_type, &in_size);
    sample_comm_vpss_get_default_grp_attr(&vpss_cfg->grp_attr);
    sample_comm_vpss_get_default_3dnr_attr(&vpss_cfg->nr_attr);
    vpss_cfg->vpss_grp = VPSS_GRP;
    max_w = in_size.width;
    max_h = in_size.height;

    for (chn = 0; chn < CHN_NUM_MAX && chn < OT_VPSS_MAX_PHYS_CHN_NUM; chn++) {
        vpss_cfg->chn_en[chn] = TD_TRUE;
        sample_comm_vpss_get_default_chn_attr(&vpss_cfg->chn_attr[chn]);
        vpss_cfg->chn_attr[chn].width = enc_size[chn].width;
        vpss_cfg->chn_attr[chn].height = enc_size[chn].height;
        vpss_cfg->chn_attr[chn].depth = 0;

        max_w = MAX2(max_w, enc_size[chn].width);
        max_h = MAX2(max_h, enc_size[chn].height);
    }
    vpss_cfg->chn_attr[OT_VPSS_CHN0].compress_mode = OT_COMPRESS_MODE_SEG_COMPACT;
    vpss_cfg->grp_attr.max_width = max_w;
    vpss_cfg->grp_attr.max_height = max_h;
    vpss_cfg->wrap_attr[0].enable = TD_TRUE;
}

/*
 * VPSS wrap 需要知道 sensor 一帧有多少行(full_lines_std)才能算环形缓冲大小。
 * 不同 4M/5M sensor 行数不同；不认识的 sensor 就关掉 wrap。
 */
static td_void get_wrap_cfg(sample_sns_type sns_type, sampe_sys_cfg *sys_cfg, sample_vpss_cfg *vpss_cfg)
{
    td_u32 full_lines_std;

    if (!sys_cfg->vpss_wrap_en) {
        return;
    }

    if (sns_type == SC4336P_MIPI_4M_30FPS_10BIT || sns_type == OS04D10_MIPI_4M_30FPS_10BIT ||
        sns_type == GC4023_MIPI_4M_30FPS_10BIT || sns_type == SC431HAI_MIPI_4M_30FPS_10BIT ||
        sns_type == SC431HAI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
        full_lines_std = 1500;
    } else if (sns_type == SC450AI_MIPI_4M_30FPS_10BIT || sns_type == SC450AI_MIPI_4M_30FPS_10BIT_WDR2TO1) {
        full_lines_std = 1585;
    } else if (sns_type == SC500AI_MIPI_5M_30FPS_10BIT || sns_type == SC500AI_MIPI_5M_30FPS_10BIT_WDR2TO1) {
        full_lines_std = 1700;
    } else {
        sys_cfg->vpss_wrap_en = TD_FALSE;
        vpss_cfg->wrap_attr[0].enable = TD_FALSE;
        return;
    }

    (td_void)sample_comm_vpss_get_wrap_cfg(vpss_cfg->chn_attr, sys_cfg->mode_type, full_lines_std,
        &vpss_cfg->wrap_attr[0]);
    sys_cfg->vpss_wrap_size = vpss_cfg->wrap_attr[0].buf_size;
}

/* 系统初始化：分配 VB 缓冲池 + 设置 VI/VPSS 工作模式 */
static td_s32 sys_init(const ot_size *enc_size, sampe_sys_cfg *sys_cfg)
{
    ot_vb_cfg vb_cfg = {0};
    td_u32 supplement = OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK;

    get_vb_cfg(&vb_cfg, enc_size, sys_cfg->vpss_wrap_size);

    if (sample_comm_sys_init_with_vb_supplement(&vb_cfg, supplement) != TD_SUCCESS) {
        sample_print("sys init failed!\n");
        return TD_FAILURE;
    }
    if (sample_comm_vi_set_vi_vpss_mode(sys_cfg->mode_type, OT_VI_AIISP_MODE_DEFAULT) != TD_SUCCESS) {
        sample_comm_sys_exit();
        return TD_FAILURE;
    }
    return TD_SUCCESS;
}

/* 启动 VPSS：设置帧中断、把各通道属性下发，并给通道1开低延时 */
static td_s32 vpss_start(sample_vpss_cfg *vpss_cfg)
{
    ot_frame_interrupt_attr frame_int = {0};
    sample_vpss_chn_attr vpss_chn_attr = {0};
    td_s32 ret;

    /* 让 VPSS 在画面处理到一半时就产生中断，配合在线低延时 */
    frame_int.interrupt_type = OT_FRAME_INTERRUPT_EARLY_END;
    frame_int.early_line = vpss_cfg->grp_attr.max_height / 2;
    if ((ret = ss_mpi_vpss_set_grp_frame_interrupt_attr(VPSS_GRP, &frame_int)) != TD_SUCCESS) {
        sample_print("set vpss frame interrupt failed: 0x%x\n", ret);
        return ret;
    }

    /* 把前面准备好的通道属性整理到一个结构里，一次性下发 */
    (td_void)memcpy_s(&vpss_chn_attr.chn_attr[0], sizeof(ot_vpss_chn_attr) * OT_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->chn_attr, sizeof(ot_vpss_chn_attr) * OT_VPSS_MAX_PHYS_CHN_NUM);
    (td_void)memcpy_s(vpss_chn_attr.chn_enable, sizeof(vpss_chn_attr.chn_enable),
        vpss_cfg->chn_en, sizeof(vpss_chn_attr.chn_enable));
    (td_void)memcpy_s(&vpss_chn_attr.wrap_attr[0], sizeof(ot_vpss_chn_buf_wrap_attr) * OT_VPSS_MAX_PHYS_CHN_NUM,
        vpss_cfg->wrap_attr, sizeof(ot_vpss_chn_buf_wrap_attr) * OT_VPSS_MAX_PHYS_CHN_NUM);
    vpss_chn_attr.chn_array_size = OT_VPSS_MAX_PHYS_CHN_NUM;

    if ((ret = sample_common_vpss_start(VPSS_GRP, &vpss_cfg->grp_attr, &vpss_chn_attr)) != TD_SUCCESS) {
        return ret;
    }

    /* 子码流通道开低延时，减少端到端时延 */
    if (vpss_cfg->chn_en[1]) {
        const ot_low_delay_info low_delay = { TD_TRUE, 200, TD_FALSE };
        if (ss_mpi_vpss_set_chn_low_delay(VPSS_GRP, 1, &low_delay) != TD_SUCCESS) {
            sample_common_vpss_stop(VPSS_GRP, vpss_cfg->chn_en, OT_VPSS_MAX_PHYS_CHN_NUM);
            return TD_FAILURE;
        }
    }
    return TD_SUCCESS;
}

/*
 * 把整条采集链路搭起来：SYS -> VI -> VPSS，并把 VI 绑定到 VPSS。
 * 任一步失败都按相反顺序回退已启动的部分。
 */
static td_s32 start_sys_vi_vpss(sample_sns_type sns_type, sample_vi_cfg *vi_cfg,
    sample_vpss_cfg *vpss_cfg, const ot_size *enc_size)
{
    sampe_sys_cfg sys_cfg = {0};
    td_s32 ret;

    /* step 0: 准备各种默认配置参数 */
    get_sys_cfg(&sys_cfg);
    sample_comm_vi_get_default_vi_cfg(sns_type, vi_cfg);
    get_vpss_cfg(sns_type, vpss_cfg, enc_size);
    get_wrap_cfg(sns_type, &sys_cfg, vpss_cfg);

    /* step 1: 系统初始化 + 分配 VB */
    if ((ret = sys_init(enc_size, &sys_cfg)) != TD_SUCCESS) {
        sample_print("sys init failed: 0x%x\n", ret);
        return ret;
    }
    /* step 2: 启动 VI(采集) */
    if ((ret = sample_comm_vi_start_vi(vi_cfg)) != TD_SUCCESS) {
        sample_print("vi start failed: 0x%x\n", ret);
        goto exit_sys;
    }
    /* step 3: 启动 VPSS(处理) */
    if ((ret = vpss_start(vpss_cfg)) != TD_SUCCESS) {
        sample_print("vpss start failed: 0x%x\n", ret);
        goto exit_vi;
    }
    /* step 4: VI 绑定 VPSS，画面就能自动从采集流到处理 */
    if ((ret = sample_comm_vi_bind_vpss(0, 0, VPSS_GRP, 0)) != TD_SUCCESS) {
        sample_print("vi bind vpss failed: 0x%x\n", ret);
        goto exit_vpss;
    }
    return TD_SUCCESS;

exit_vpss:
    sample_common_vpss_stop(VPSS_GRP, vpss_cfg->chn_en, OT_VPSS_MAX_PHYS_CHN_NUM);
exit_vi:
    sample_comm_vi_stop_vi(vi_cfg);
exit_sys:
    sample_comm_sys_exit();
    return ret;
}

/* 拆掉整条链路：解绑 VI/VPSS，停 VPSS、停 VI、退出 SYS（与启动相反顺序） */
static td_void stop_sys_vi_vpss(sample_vi_cfg *vi_cfg, sample_vpss_cfg *vpss_cfg)
{
    sample_comm_vi_un_bind_vpss(0, 0, VPSS_GRP, 0);
    sample_common_vpss_stop(VPSS_GRP, vpss_cfg->chn_en, OT_VPSS_MAX_PHYS_CHN_NUM);
    sample_comm_vi_stop_vi(vi_cfg);
    sample_comm_sys_exit();
}

/* ============================== 主流程 ============================== */

/*
 * 把分辨率枚举(PIC_xxx)换算成实际宽高(ot_size)，VENC/VPSS 都要用具体宽高。
 */
static td_s32 get_enc_size(ot_size enc_size[CHN_NUM_MAX])
{
    hi_pic_size pic_size[CHN_NUM_MAX] = { BIG_STREAM_SIZE, SMALL_STREAM_SIZE };
    td_s32 i;

    for (i = 0; i < CHN_NUM_MAX; i++) {
        if (sample_comm_sys_get_pic_size(pic_size[i], &enc_size[i]) != TD_SUCCESS) {
            sample_print("get pic size[%d] failed!\n", i);
            return TD_FAILURE;
        }
    }
    return TD_SUCCESS;
}

/*
 * 整个业务：H.264 主码流 2560x1440 + 子码流 1024x768，主码流通过 RTSP 推出。
 */
static td_s32 sample_venc_rtsp(td_void)
{
    sample_sns_type sns_type = SENSOR0_TYPE;
    sample_vi_cfg vi_cfg = {0};
    sample_vpss_cfg vpss_cfg = {0};
    ot_size enc_size[CHN_NUM_MAX] = {0};
    td_s32 ret;

    if ((ret = get_enc_size(enc_size)) != TD_SUCCESS) {
        return ret;
    }

    /* 启动采集链路 SYS/VI/VPSS */
    if ((ret = start_sys_vi_vpss(sns_type, &vi_cfg, &vpss_cfg, enc_size)) != TD_SUCCESS) {
        return ret;
    }

    /* 启动编码 + 取流推流 */
    if ((ret = venc_start_encode(enc_size)) != TD_SUCCESS) {
        stop_sys_vi_vpss(&vi_cfg, &vpss_cfg);
        return ret;
    }

    /* 一直运行，等待用户 Ctrl+C / kill */
    printf("RTSP streaming... play at  rtsp://<board-ip>%s   (Ctrl+C to exit)\n", RTSP_STREAM_PATH);
    while (g_sample_venc_exit == TD_FALSE) {
        sleep(1);
    }

    /* 退出：先停编码/推流，再拆采集链路 */
    venc_stop_encode();
    stop_sys_vi_vpss(&vi_cfg, &vpss_cfg);
    return TD_SUCCESS;
}

td_s32 main(td_s32 argc, td_char *argv[])
{
    td_s32 ret;

    ot_unused(argc);
    ot_unused(argv);

    /* 注册信号处理，保证 Ctrl+C 能优雅退出（释放硬件资源） */
    sample_sys_signal(sample_venc_handle_sig);

    /* 创建 RTSP 服务器并建立一个会话，对应播放地址 rtsp://<IP>/test.264 */
    g_rtsplive = create_rtsp_demo(RTSP_PORT);
    g_session = create_rtsp_session(g_rtsplive, RTSP_STREAM_PATH);
    if (g_rtsplive == NULL || g_session == NULL) {
        printf("create rtsp server failed!\n");
        return -1;
    }

    ret = sample_venc_rtsp();
    printf(ret == TD_SUCCESS ? "program exit normally!\n" : "program exit abnormally!\n");

    /* 关闭 RTSP 服务器 */
    if (g_session != NULL) {
        rtsp_del_session(g_session);
    }
    if (g_rtsplive != NULL) {
        rtsp_del_demo(g_rtsplive);
    }

    exit(ret);
}

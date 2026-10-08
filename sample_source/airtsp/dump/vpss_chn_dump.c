/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "ot_math.h"
#include "ot_buffer.h"
#include "securec.h"
#include "ot_common.h"
#include "ot_common_video.h"
#include "ot_common_sys.h"
#include "ot_common_vb.h"
#include "../../../cbb/isp/ext_inc/ot_mpi_sys_mem.h"
#include "../../../cbb/isp/ext_inc/ot_mpi_vb.h"
#include "hi_mpi_vgs.h"
#include "ot_common_vpss.h"
#include "hi_mpi_vpss.h"

#include "hi_mpi_aidetect.h"
extern td_u32 g_input_frame_width;
extern td_u32 g_input_frame_height;


#define MAX_DIGIT_LEN 4
#define MAX_DUMP_FRAME_CNT 64
#define MAX_FRM_WIDTH 20480
#define FILE_NAME_LENGTH 128
#define PIXEL_FORMAT_STRING_LEN 10
#define GET_CHN_FRAME_TIMEOUT 50000
#define VPSS_ATTR_ARGV_BASE 10

/* set VPSS_GET_CHN_FRAME_CONTINUOUSLY to 1 to get continuous frame */
#define VPSS_GET_CHN_FRAME_CONTINUOUSLY 0//1//0 swann
/* set the value of VPSS_GET_CHN_FRAME_NUM according to how many frames you need to dump */
#define VPSS_GET_CHN_FRAME_NUM MAX_DUMP_FRAME_CNT

typedef struct {
    ot_vb_blk vb_blk;
    ot_vb_pool vb_pool;

    td_phys_addr_t phys_addr;
    td_void *virt_addr;
} vpss_dump_mem;

static td_u32 g_vpss_depth_flag = 0;
static volatile sig_atomic_t g_signal_flag = 0;

static ot_vpss_grp g_vpss_grp = 0;
static ot_vpss_chn g_vpss_chn = 0;
static td_u32 g_orig_depth = 0;
static ot_vpss_chn_mode g_orig_chn_mode = OT_VPSS_CHN_MODE_AUTO;
static ot_video_frame_info g_frame;
static ot_video_frame_info g_multi_frame[VPSS_GET_CHN_FRAME_NUM];

static ot_vb_pool g_pool = OT_VB_INVALID_POOL_ID;
static vpss_dump_mem g_dump_mem = { 0 };
static ot_vgs_handle g_handle = -1;
static td_u32 g_blk_size = 0;

static td_char *g_user_page_addr[2] = { TD_NULL, TD_NULL }; /* 2 Y and C */
static td_u32 g_size = 0;
static td_u32 g_c_size = 0;

static FILE *g_pfd = TD_NULL;
//frame为从vpss ch获取的视频帧数据，fd为需要保存的文件，uv_height表示图像高度，is_uv_invert表示是否uv转换
//总结：将frame数据的uv分量保存在fd文件中，uv_height指定图像高度，is_uv_invert指定uv转换
static td_void vpss_chn_dump_covert_chroma_sp42x_to_planar(const ot_video_frame *frame, FILE *fd,
    td_u32 uv_height, td_bool is_uv_invert)
{
    /* If this value is too small and the image is big, this memory may not be enough */
    static unsigned char tmp_buf[MAX_FRM_WIDTH];
    char *mem_content = TD_NULL;
    char *virt_addr_c = TD_NULL;
    td_u32 w, h;
    td_phys_addr_t phys_addr;
	//获取frame的uv分量的物理地址phys_addr[1]，赋值给phys_addr
    phys_addr = frame->phys_addr[1];
	//调用ss_mpi_sys_mmap映射虚拟地址给g_user_page_addr[1]
    g_user_page_addr[1] = (td_char *)ss_mpi_sys_mmap(phys_addr, g_c_size);
    if (g_user_page_addr[1] == TD_NULL) {
        printf("mmap chroma data error!!!\n");
        return;
    }
    virt_addr_c = g_user_page_addr[1];

    (td_void)fflush(fd);
	//保存u分量
    /* save U */
    (td_void)fprintf(stderr, "U......");
    (td_void)fflush(stderr);
    for (h = 0; h < uv_height; h++) {
        mem_content = virt_addr_c + h * frame->stride[1];
        if (!is_uv_invert) {
            mem_content += 1;
        }

        for (w = 0; w < frame->width / 2; w++) { /* 2 chroma width */
            tmp_buf[w] = *mem_content;
            mem_content += 2; /* 2 semiplanar steps */
        }

        (td_void)fwrite(tmp_buf, frame->width / 2, 1, fd); /* 2 chroma width */
    }
    (td_void)fflush(fd);
	//保存v分量
    /* save V */
    (td_void)fprintf(stderr, "V......");
    (td_void)fflush(stderr);
    for (h = 0; h < uv_height; h++) {
        mem_content = virt_addr_c + h * frame->stride[1];
        if (is_uv_invert) {
            mem_content += 1;
        }

        for (w = 0; w < frame->width / 2; w++) { /* 2 chroma width */
            tmp_buf[w] = *mem_content;
            mem_content += 2; /* 2 semiplanar steps */
        }

        (td_void)fwrite(tmp_buf, frame->width / 2, 1, fd); /* 2 chroma width */
    }

    (td_void)fflush(fd);
    if (g_user_page_addr[1] != TD_NULL) {
        ss_mpi_sys_munmap(g_user_page_addr[1], g_c_size);
        g_user_page_addr[1] = TD_NULL;
    }
}
//转成yuv422，实际没用到
static td_void vpss_chn_dump_vy1uy0_pkg_to_yuv422(td_u32 width, td_u32 height, td_char *virt_addr, FILE *fd)
{
    td_u32 w, h;

    (td_void)fprintf(stderr, "saving......vy1uy0_pkg_422 to p422......\n");
    (td_void)fprintf(stderr, "saving......Y......");
    for (h = 0; h < height; h++) {
        for (w = 0; w < width * 2; w += 4) { /* 4, memory layout: v[byte3] y1[byte2] u[byte1] y0[byte0] */
            td_u8 y0, y1;
            y0 = virt_addr[h * width * 2 + w]; /* 2, each line has 2*w bytes */
            y1 = virt_addr[h * width * 2 + w + 2]; /* 2, y1 offset */
            (td_void)fwrite(&y0, 1, 1, fd);
            (td_void)fwrite(&y1, 1, 1, fd);
        }
    }
    (td_void)fprintf(stderr, "saving......U......");
    for (h = 0; h < height; h++) {
        for (w = 0; w < width * 2; w += 4) { /* 4, memory layout: v[byte3] y1[byte2] u[byte1] y0[byte0] */
            td_u8 u = virt_addr[h * width * 2 + w + 1];
            (td_void)fwrite(&u, 1, 1, fd);
        }
    }
    (td_void)fprintf(stderr, "saving......V......");
    for (h = 0; h < height; h++) {
        for (w = 0; w < width * 2; w += 4) { /* 4, memory layout: v[byte3] y1[byte2] u[byte1] y0[byte0] */
            td_u8 v = virt_addr[h * width * 2 + w + 3];
            (td_void)fwrite(&v, 1, 1, fd);
        }
    }
}
extern td_void sample_aidetect_result_init(ot_aidetect_result_array *result);
extern td_bool sample_aidetect_proc_one_frame_new(td_u32 index_num, td_char *p_img_data, td_u32 data_img_size,
    td_phys_addr_t u_phy_addr, td_void *p_vir_addr, ot_aidetect_result_array *result,const ot_video_frame *frame);
extern td_char *g_p_img_data;
extern td_phys_addr_t u_phy_addr;
extern td_void *p_vir_addr;
extern ot_aidetect_result_array g_result;
extern td_u32 g_index_num;


//根据pixel_format格式写入文件,开发板实际是 yvu_sp420
//将dump到的sp42x的frame数据的y分量保存在文件中,并调用vpss_chn_dump_covert_chroma_sp42x_to_planar保存uv分量
//总结：保存yuv数据到文件中
/* When saving a file, sp420 will be denoted by p420 and sp422 will be denoted by p422 in the name of the file */
td_void sample_yuv_dump_aidetect(const ot_video_frame *frame, FILE *fd)
{
    unsigned int h;
    char *virt_addr_y = TD_NULL;
    char *mem_content = TD_NULL;
    td_phys_addr_t phys_addr;
    ot_pixel_format pixel_format = frame->pixel_format;
    /* When the storage format is a planar format, this variable is used to keep the height of the UV component */
    td_u32 uv_height = 0;
	//开发板uv不需要转换位置 is_uv_invert=TD_FALSE
    td_bool is_uv_invert = (pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420 ||
        pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422) ? TD_TRUE : TD_FALSE;
///////////////////////这里是保存yuv文件/////////////////////
#if 0
    g_size = (frame->stride[0]) * (frame->height);
//------开发板实际是 OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 //开发板实际走的是这里
    if (pixel_format == OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 || pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
        g_c_size = (frame->stride[1]) * (frame->height) / 2; /* 2 uv height */
        uv_height = frame->height / 2; /* 2 uv height */
    }
	
	//获取frame的Y分量的物理地址phys_addr[0]
    phys_addr = frame->phys_addr[0];
	//调用ss_mpi_sys_mmap映射虚拟地址给g_user_page_addr[0]
    g_user_page_addr[0] = (td_char *)ss_mpi_sys_mmap(phys_addr, g_size);
    if (g_user_page_addr[0] == TD_NULL) {
        return;
    }

    virt_addr_y = g_user_page_addr[0];
	
    /* save Y */
    (td_void)fprintf(stderr, "saving......Y......");
    (td_void)fflush(stderr);

    for (h = 0; h < frame->height; h++) {
        mem_content = virt_addr_y + h * frame->stride[0];
        (td_void)fwrite(mem_content, frame->width, 1, fd);
    }
	//清除fd缓存
    (td_void)fflush(fd);
	//保存uv分量
    vpss_chn_dump_covert_chroma_sp42x_to_planar(frame, fd, uv_height, is_uv_invert);
	(td_void)fprintf(stderr, "done %u!\n", frame->time_ref);//帧序列
    (td_void)fflush(stderr);
	//解除虚拟地址映射
    ss_mpi_sys_munmap(g_user_page_addr[0], g_size);
    g_user_page_addr[0] = NULL;
#endif
//////////////////////这里添加aidetect 中 sample_aidetect_proc_one_frame 处理1帧///////////////////////////

	//data_img_size为一张半图片，和make_frame的处理对应
	td_u32 data_img_size = g_input_frame_width * g_input_frame_height * 3 / 2;

	//td_bool sample_aidetect_proc_one_frame_new(td_u32 index_num, td_char *p_img_data, td_u32 data_img_size, ot_video_frame *frame, ot_aidetect_result_array *result)
	sample_aidetect_proc_one_frame_new(g_index_num, g_p_img_data, data_img_size, u_phy_addr, p_vir_addr, &g_result,frame);
	g_index_num++;

	//(td_void)fprintf(stderr, "done %u!\n", frame->time_ref);//帧序列
	(td_void)fflush(stderr);

//////////////////////////////////////////////

}

//根据pixel_format格式写入文件,开发板实际是 yvu_sp420
//将dump到的sp42x的frame数据的y分量保存在文件中,并调用vpss_chn_dump_covert_chroma_sp42x_to_planar保存uv分量
//总结：保存yuv数据到文件中
/* When saving a file, sp420 will be denoted by p420 and sp422 will be denoted by p422 in the name of the file */
static td_void sample_yuv_8bit_dump(const ot_video_frame *frame, FILE *fd)
{
    unsigned int h;
    char *virt_addr_y = TD_NULL;
    char *mem_content = TD_NULL;
    td_phys_addr_t phys_addr;
    ot_pixel_format pixel_format = frame->pixel_format;
    /* When the storage format is a planar format, this variable is used to keep the height of the UV component */
    td_u32 uv_height = 0;
	//开发板uv不需要转换位置 is_uv_invert=TD_FALSE
    td_bool is_uv_invert = (pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420 ||
        pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422) ? TD_TRUE : TD_FALSE;

    g_size = (frame->stride[0]) * (frame->height);
//------开发板实际是 OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 //开发板实际走的是这里
    if (pixel_format == OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 || pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420) {
        g_c_size = (frame->stride[1]) * (frame->height) / 2; /* 2 uv height */
        uv_height = frame->height / 2; /* 2 uv height */
	//其他格式
    } else if (pixel_format == OT_PIXEL_FORMAT_YVU_SEMIPLANAR_422 ||
        pixel_format == OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422) {
        g_c_size = (frame->stride[1]) * (frame->height);
        uv_height = frame->height;
    } else if (pixel_format == OT_PIXEL_FORMAT_YUV_400) {
        g_c_size = 0;
        uv_height = frame->height;
    }
	//获取frame的Y分量的物理地址phys_addr[0]
    phys_addr = frame->phys_addr[0];
	//调用ss_mpi_sys_mmap映射虚拟地址给g_user_page_addr[0]
    g_user_page_addr[0] = (td_char *)ss_mpi_sys_mmap(phys_addr, g_size);
    if (g_user_page_addr[0] == TD_NULL) {
        return;
    }

    virt_addr_y = g_user_page_addr[0];

    if (pixel_format == OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422) {//如果是yuv422格式，就调用vpss_chn_dump_vy1uy0_pkg_to_yuv422处理
        vpss_chn_dump_vy1uy0_pkg_to_yuv422(frame->width, frame->height, virt_addr_y, fd);
    } else {//如果不是yuv422格式，可以直接fwrite写
//------开发板实际走的是这里
        /* save Y */
        (td_void)fprintf(stderr, "saving......Y......");
        (td_void)fflush(stderr);

        for (h = 0; h < frame->height; h++) {
            mem_content = virt_addr_y + h * frame->stride[0];
            (td_void)fwrite(mem_content, frame->width, 1, fd);
        }
    }
	//清除fd缓存
    (td_void)fflush(fd);
	//如果不是 OT_PIXEL_FORMAT_YUV_400 和      OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422 就调用vpss_chn_dump_covert_chroma_sp42x_to_planar处理
//----开发板实际走的是这里
    if (pixel_format != OT_PIXEL_FORMAT_YUV_400 && pixel_format != OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422) {
		//保存uv分量，is_uv_invert为false, OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420
        vpss_chn_dump_covert_chroma_sp42x_to_planar(frame, fd, uv_height, is_uv_invert);
    }

  //  (td_void)fprintf(stderr, "done %u!\n", frame->time_ref);
    (td_void)fflush(stderr);
	//解除虚拟地址映射
    ss_mpi_sys_munmap(g_user_page_addr[0], g_size);
    g_user_page_addr[0] = NULL;
}
//将vpss初始的g_orig_chn_mode g_orig_depth改回去
static td_void vpss_restore_depth(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn)
{
    td_s32 ret;
    ot_vpss_chn_attr chn_attr;
    ot_vpss_ext_chn_attr ext_chn_attr;

    if (vpss_chn >= OT_VPSS_MAX_PHYS_CHN_NUM) {
        ret = ss_mpi_vpss_get_ext_chn_attr(vpss_grp, vpss_chn, &ext_chn_attr);
        if (ret != TD_SUCCESS) {
            printf("get ext chn attr error!!!\n");
        }

        ext_chn_attr.depth = g_orig_depth;
        ret = ss_mpi_vpss_set_ext_chn_attr(vpss_grp, vpss_chn, &ext_chn_attr);
        if (ret != TD_SUCCESS) {
            printf("set ext chn attr error!!!\n");
        }
    } else {
        ret = ss_mpi_vpss_get_chn_attr(vpss_grp, vpss_chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            printf("get chn attr error!!!\n");
        }

        chn_attr.depth = g_orig_depth;
        chn_attr.chn_mode = g_orig_chn_mode;
        ret = ss_mpi_vpss_set_chn_attr(vpss_grp, vpss_chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            printf("set chn attr error!!!\n");
        }
    }
}
//收尾工作，release_chn vgs_cancel vb_release sys_munmap vpss_restore_depth
static td_void vpss_restore(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn)
{
    td_s32 ret;

    if (g_frame.pool_id != OT_VB_INVALID_POOL_ID) {
        ret = ss_mpi_vpss_release_chn_frame(vpss_grp, vpss_chn, &g_frame);
        if (ret != TD_SUCCESS) {
            printf("Release Chn Frame error!!!\n");
        }

        g_frame.pool_id = OT_VB_INVALID_POOL_ID;
    }

    if (g_handle != -1) {
        ss_mpi_vgs_cancel_job(g_handle);
        g_handle = -1;
    }

    if (g_dump_mem.vb_pool != OT_VB_INVALID_POOL_ID) {
        ss_mpi_vb_release_blk(g_dump_mem.vb_blk);
        g_dump_mem.vb_pool = OT_VB_INVALID_POOL_ID;
    }

    if (g_pool != OT_VB_INVALID_POOL_ID) {
        ss_mpi_vb_destroy_pool(g_pool);
        g_pool = OT_VB_INVALID_POOL_ID;
    }

    if (g_user_page_addr[0] != TD_NULL) {
        ss_mpi_sys_munmap(g_user_page_addr[0], g_size);
        g_user_page_addr[0] = TD_NULL;
    }
    if (g_user_page_addr[1] != TD_NULL) {
        ss_mpi_sys_munmap(g_user_page_addr[1], g_size);
        g_user_page_addr[1] = TD_NULL;
    }

    if (g_pfd != TD_NULL) {
        fclose(g_pfd);
        g_pfd = TD_NULL;
    }

    if (g_vpss_depth_flag) {
        vpss_restore_depth(vpss_grp, vpss_chn);
        g_vpss_depth_flag = 0;
    }
    return;
}
//处理退出信号
extern td_bool g_sample_venc_exit;

static void vpss_chn_dump_handle_sig(int signo)
{
    if (g_signal_flag) {
        return;
    }

    if (signo == SIGINT || signo == SIGTERM) {//ctrl+c或kill
        g_signal_flag = 1;
    }
    return;
}
//根据输入参数grp chn frame_cnt信息，得到保存的文件名字./vpss_grp0_chn1_720x576_P420_100.yuv，并打开得到g_pfd
static td_s32 vpss_chn_dump_make_yuv_file_name(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt)
{
    td_char yuv_name[FILE_NAME_LENGTH];
    td_char pixel_str[PIXEL_FORMAT_STRING_LEN];
    switch (g_frame.video_frame.pixel_format) {
        case OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420:
        case OT_PIXEL_FORMAT_YUV_SEMIPLANAR_420:
            if (snprintf_s(pixel_str, PIXEL_FORMAT_STRING_LEN, PIXEL_FORMAT_STRING_LEN - 1, "P420") == -1) {
                printf("set pixel name fail!\n");
                return TD_FAILURE;
            }
            break;

        case OT_PIXEL_FORMAT_YVU_SEMIPLANAR_422:
        case OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422:
        case OT_PIXEL_FORMAT_VY1UY0_PACKAGE_422:
            if (snprintf_s(pixel_str, PIXEL_FORMAT_STRING_LEN, PIXEL_FORMAT_STRING_LEN - 1, "P422") == -1) {
                printf("set pixel name fail!\n");
                return TD_FAILURE;
            }
            break;

        default:
            if (snprintf_s(pixel_str, PIXEL_FORMAT_STRING_LEN, PIXEL_FORMAT_STRING_LEN - 1, "P400") == -1) {
                printf("set pixel name fail!\n");
                return TD_FAILURE;
            }
            break;
    }

    /* make file name */
    if (snprintf_s(yuv_name, FILE_NAME_LENGTH, FILE_NAME_LENGTH - 1, "./vpss_grp%d_chn%d_%ux%u_%s_%u.yuv",
        grp, chn, g_frame.video_frame.width,
        g_frame.video_frame.height, pixel_str, frame_cnt) == -1) {
        printf("set out put file name fail!\n");
        return TD_FAILURE;
    }
	//Dump YUV frame of vpss chn 1  to file: "./vpss_grp0_chn1_720x576_P420_100.yuv"
    printf("Dump YUV frame of vpss chn %d  to file: \"%s\"\n", chn, yuv_name);

    /* open file */
    g_pfd = fopen(yuv_name, "wb");
    if (g_pfd == TD_NULL) {
        printf("open file failed, errno %d!\n", errno);
        return TD_FAILURE;
    }
    (td_void)fflush(stdout);
    return TD_SUCCESS;
}
//根据dump_mem、vb_calc_cfg、vpss_frame_info填充vgs_frame_info
static td_void vpss_chn_dump_set_vgs_frame_info(ot_video_frame_info *vgs_frame_info, const vpss_dump_mem *dump_mem,
    const ot_vb_calc_cfg *vb_calc_cfg, const ot_video_frame_info *vpss_frame_info)
{
    vgs_frame_info->video_frame.phys_addr[0] = dump_mem->phys_addr;
    vgs_frame_info->video_frame.phys_addr[1] = vgs_frame_info->video_frame.phys_addr[0] + vb_calc_cfg->main_y_size;
    vgs_frame_info->video_frame.width = vpss_frame_info->video_frame.width;
    vgs_frame_info->video_frame.height = vpss_frame_info->video_frame.height;
    vgs_frame_info->video_frame.stride[0] = vb_calc_cfg->main_stride;
    vgs_frame_info->video_frame.stride[1] = vb_calc_cfg->main_stride;
    vgs_frame_info->video_frame.compress_mode = OT_COMPRESS_MODE_NONE;
    vgs_frame_info->video_frame.pixel_format = vpss_frame_info->video_frame.pixel_format;
    vgs_frame_info->video_frame.video_format = OT_VIDEO_FORMAT_LINEAR;
    vgs_frame_info->video_frame.dynamic_range = vpss_frame_info->video_frame.dynamic_range;
    vgs_frame_info->video_frame.pts = 0;
    vgs_frame_info->video_frame.time_ref = 0;
    vgs_frame_info->pool_id = dump_mem->vb_pool;
    vgs_frame_info->mod_id = OT_ID_VGS;
}
//初始化vgs的vb_pool
static td_s32 vpss_chn_dump_init_vgs_pool(vpss_dump_mem *dump_mem, ot_vb_calc_cfg *vb_calc_cfg)
{
    td_u32 width = g_frame.video_frame.width;
    td_u32 height = g_frame.video_frame.height;
    ot_pixel_format pixel_format = g_frame.video_frame.pixel_format;
    ot_pic_buf_attr buf_attr = {0};
    ot_vb_pool_cfg vb_pool_cfg = {0};

    buf_attr.width = width;
    buf_attr.height = height;
    buf_attr.pixel_format = pixel_format;
    buf_attr.bit_width = OT_DATA_BIT_WIDTH_8;
    buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    buf_attr.align = 0;
    ot_common_get_pic_buf_cfg(&buf_attr, vb_calc_cfg);

    g_blk_size = vb_calc_cfg->vb_size;

    vb_pool_cfg.blk_size = g_blk_size;
    vb_pool_cfg.blk_cnt = 1;
    vb_pool_cfg.remap_mode = OT_VB_REMAP_MODE_NONE;

    g_pool = ss_mpi_vb_create_pool(&vb_pool_cfg);
    if (g_pool == OT_VB_INVALID_POOL_ID) {
        printf("OT_MPI_VB_CreatePool failed! \n");
        return TD_FAILURE;
    }

    dump_mem->vb_pool = g_pool;
    dump_mem->vb_blk = ss_mpi_vb_get_blk(dump_mem->vb_pool, g_blk_size, TD_NULL);
    if (dump_mem->vb_blk == OT_VB_INVALID_HANDLE) {
        printf("get vb blk failed!\n");
        return TD_FAILURE;
    }

    dump_mem->phys_addr = ss_mpi_vb_handle_to_phys_addr(dump_mem->vb_blk);
    return TD_SUCCESS;
}
//vgs相关进行Crop、缩放、像素格式转换、视频存储格式转换、画线、旋转、亮度和统计、Corner_rect、拼接、AspectRatio、Mirror/Flip等处理。
//先放到vgs处理再dump
static td_s32 vpss_chn_dump_send_vgs_and_dump(const ot_video_frame_info *vpss_frame_info)
{
    ot_video_frame_info vgs_frame_info = {0};
    ot_vgs_task_attr vgs_task_attr;
    ot_vb_calc_cfg vb_calc_cfg = {0};

    if (vpss_chn_dump_init_vgs_pool(&g_dump_mem, &vb_calc_cfg) != TD_SUCCESS) {
        printf("init vgs pool failed\n");
        return TD_FAILURE;
    }

    vpss_chn_dump_set_vgs_frame_info(&vgs_frame_info, &g_dump_mem, &vb_calc_cfg, vpss_frame_info);

    if (ss_mpi_vgs_begin_job(&g_handle) != TD_SUCCESS) {
        printf("ss_mpi_vgs_begin_job failed\n");
        return TD_FAILURE;
    }

    if (memcpy_s(&vgs_task_attr.img_in, sizeof(ot_video_frame_info),
        vpss_frame_info, sizeof(ot_video_frame_info)) != EOK) {
        printf("memcpy_s img_in failed\n");
        goto err_exit;
    }
    if (memcpy_s(&vgs_task_attr.img_out, sizeof(ot_video_frame_info),
        &vgs_frame_info, sizeof(ot_video_frame_info)) != EOK) {
        printf("memcpy_s img_out failed\n");
        goto err_exit;
    }
    if (ss_mpi_vgs_add_scale_task(g_handle, &vgs_task_attr, OT_VGS_SCALE_COEF_NORM) != TD_SUCCESS) {
        printf("ss_mpi_vgs_add_scale_task failed\n");
        goto err_exit;
    }
    if (ss_mpi_vgs_end_job(g_handle) != TD_SUCCESS) {
        printf("ss_mpi_vgs_end_job failed\n");
        goto err_exit;
    }

    /* save frame to file */
    if (vgs_frame_info.video_frame.dynamic_range == OT_DYNAMIC_RANGE_SDR8) {
        sample_yuv_8bit_dump(&vgs_frame_info.video_frame, g_pfd);
    } else {
        printf("Only support dump 8-bit data!\n");
    }
    ss_mpi_vb_release_blk(g_dump_mem.vb_blk);
    g_dump_mem.vb_pool = OT_VB_INVALID_POOL_ID;
    if (g_pool != OT_VB_INVALID_POOL_ID) {
        ss_mpi_vb_destroy_pool(g_pool);
        g_pool = OT_VB_INVALID_POOL_ID;
    }
    g_handle = -1;
    return TD_SUCCESS;
err_exit:
    ss_mpi_vgs_cancel_job(g_handle);
    g_handle = -1;
    return TD_FAILURE;
}
//读取grp的chn的chn_attr信息，判断是否需要vgs,返回TD_TRUE 或者 TD_FALSE
static td_bool vpss_chn_dump_is_vgs_required(ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_s32 ret;
    ot_vpss_chn_attr chn_attr;
    ot_vpss_ext_chn_attr ext_chn_attr;

    if (chn >= OT_VPSS_MAX_PHYS_CHN_NUM) {
        /* please don't change vpss ext chn attr when dump frame */
        ret = ss_mpi_vpss_get_ext_chn_attr(grp, chn, &ext_chn_attr);
        if (ret != TD_SUCCESS) {
            printf("get vpss ext chn attr failed!\n");
            return TD_FALSE;
        }

        if ((ext_chn_attr.compress_mode != OT_COMPRESS_MODE_NONE) ||
            (ext_chn_attr.video_format != OT_VIDEO_FORMAT_LINEAR)) {
            return TD_TRUE;
        } else {
            return TD_FALSE;
        }
    } else {
        /* please don't change vpss chn attr when dump frame */
        ret = ss_mpi_vpss_get_chn_attr(grp, chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            printf("get vpss chn attr failed!\n");
            return TD_FALSE;
        }

        if ((chn_attr.compress_mode != OT_COMPRESS_MODE_NONE) || (chn_attr.video_format != OT_VIDEO_FORMAT_LINEAR)) {
            return TD_TRUE;
        } else {
            return TD_FALSE;
        }
    }
}
//读取 VPSS_GET_CHN_FRAME_CONTINUOUSLY 如果为0则dump帧不连续，返回TD_FALSE
//如果输入的 frame_cnt > VPSS_GET_CHN_FRAME_NUM 64则返回TD_FALSE
//否则返回TD_TRUE
//总结：根据VPSS_GET_CHN_FRAME_CONTINUOUSLY和frame_cnt决定dump的帧是否连续
static td_bool vpss_chn_dump_is_frame_continuous(td_u32 frame_cnt, td_bool send_to_vgs)
{
    if (send_to_vgs == TD_TRUE) {
        return TD_FALSE;
    }

    if (VPSS_GET_CHN_FRAME_CONTINUOUSLY == 0) {
        return TD_FALSE;
    }

    if (frame_cnt > VPSS_GET_CHN_FRAME_NUM) {
        printf("VPSS_GET_CHN_FRAME_NUM(%d) is small than frame_cnt(%d), "
            "please edit the value of this macro if you want to get frame continuously!\n",
            VPSS_GET_CHN_FRAME_NUM, frame_cnt);
        return TD_FALSE;
    }

    return TD_TRUE;
}
//从grp的chn通道dump frame_cnt个非连续帧。且对是否需要 send_to_vgs 区别处理
static td_s32 vpss_chn_dump_uncontinuous_frame(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt, td_bool send_to_vgs)
{
    td_u32 cnt = frame_cnt;
    const td_s32 milli_sec = GET_CHN_FRAME_TIMEOUT;
    td_s32 ret;

    /* get frame */
    //while (cnt--) {
    while (g_sample_venc_exit == TD_FALSE) {//用户按下ctrl+c或者kill则退出

		//从grp的chn通道dump 1帧放在g_frame
        if (ss_mpi_vpss_get_chn_frame(grp, chn, &g_frame, milli_sec) != TD_SUCCESS) {
            printf("Get frame failed!\n");
            usleep(1000); /* 1000 sleep */
            continue;
        }

        if (send_to_vgs) {//需要vgs图形处理
			printf("vpss_chn_dump_uncontinuous_frame send_to_vgs!\n");
            if (vpss_chn_dump_send_vgs_and_dump(&g_frame) != TD_SUCCESS) {
                ss_mpi_vpss_release_chn_frame(grp, chn, &g_frame);
                g_frame.pool_id = OT_VB_INVALID_POOL_ID;
                return TD_FAILURE;
            }
//-----------------实际走的是这里-----------------
        } else {//不需要vgs图形处理
        	//printf("vpss_chn_dump_uncontinuous_frame not send_to_vgs!\n");
			//只处理dynamic_range为8-bit data
            if (OT_DYNAMIC_RANGE_SDR8 == g_frame.video_frame.dynamic_range) {
				//printf("vpss_chn_dump_uncontinuous_frame OT_DYNAMIC_RANGE_SDR8!\n");
                //sample_yuv_8bit_dump(&g_frame.video_frame, g_pfd);
				sample_yuv_dump_aidetect(&g_frame.video_frame, g_pfd);
            } else {
                printf("Only support dump 8-bit data!\n");
            }
        }
		//Get frame 99
        //printf("Get frame %u\n", cnt);
        /* release frame after using */
        ret = ss_mpi_vpss_release_chn_frame(grp, chn, &g_frame);
        if (ret != TD_SUCCESS) {
            printf("Release frame failed, now exit!\n");
            return ret;
        }
        g_frame.pool_id = OT_VB_INVALID_POOL_ID;
    }
	
exit:
	vpss_restore(grp, chn);
    return TD_SUCCESS;
}
//从grp的chn通道dump frame_cnt个连续帧
static td_s32 vpss_chn_dump_continuous_frame(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt)
{
    td_u32 cnt = frame_cnt;
    td_u32 get_frame_cnt = 0;
    const td_s32 milli_sec = GET_CHN_FRAME_TIMEOUT;
    td_s32 ret;

    /* get frame */
    while (cnt--) {
        if (g_signal_flag == 1) {//中途退出
            printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
            return TD_FAILURE;
        }
		//从grp的chn通道dump frame_cnt个连续帧，存在g_multi_frame[]数组里
        //if (ss_mpi_vpss_get_chn_frame(grp, chn, &g_multi_frame[get_frame_cnt], milli_sec) != TD_SUCCESS) {
        ret=ss_mpi_vpss_get_chn_frame(grp, chn, &g_multi_frame[get_frame_cnt], milli_sec);
		printf("ss_mpi_vpss_get_chn_frame,ret=%#x!\n",ret);
        if (ret<0) {
            printf("Get frame failed,ret=%#x!\n",ret);
            usleep(1000); /* 1000 sleep */
            continue;
        }
		//get_frame_cnt存放已经get的帧数量
        get_frame_cnt++;
    }
	//将g_multi_frame[]中的数据保存在文件中
    cnt = 0;
    while (cnt < get_frame_cnt) {//0-get_frame_cnt帧全部保存
        if (g_signal_flag == 1) {//中途退出
            printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
            return TD_FAILURE;
        }
		//只处理dynamic_range为8-bit data
        if (OT_DYNAMIC_RANGE_SDR8 == g_multi_frame[cnt].video_frame.dynamic_range) {
            sample_yuv_8bit_dump(&g_multi_frame[cnt].video_frame, g_pfd);
        } else {
            printf("Only support dump 8-bit data!\n");
        }
		//Get frame 0
        printf("Get frame %u\n", cnt);
        /* release frame after using */
        ret = ss_mpi_vpss_release_chn_frame(grp, chn, &g_multi_frame[cnt]);
        if (ret != TD_SUCCESS) {
            printf("Release frame failed, now exit!\n");
            return ret;
        }
        g_multi_frame[cnt].pool_id = OT_VB_INVALID_POOL_ID;
        cnt++;//循环处理
    }

    return TD_SUCCESS;
}
//循环 调用ss_mpi_vpss_release_chn_frame 释放frame_cnt帧通道图像
static td_void vpss_chn_dump_release_multi_frame(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt)
{
    td_u32 i;

    for (i = 0; i < frame_cnt; i++) {
        if (g_multi_frame[i].pool_id != OT_VB_INVALID_POOL_ID) {
            (td_void)ss_mpi_vpss_release_chn_frame(grp, chn, &g_multi_frame[i]);
        }
    }
}
//根据输入参数 grp chn frame_cnt决定是否需要send_to_vgs，是否获取连续帧get_ctoninuous_frame
//然后区别对待，连续帧放在g_multi_frame[]中等全部缓存完毕再写文件，非连续帧直接保存文件
static td_s32 vpss_chn_dump_get_frame(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt)
{
    td_s32 ret;
	//读取grp的chn的chn_attr信息，判断是否需要vgs,返回TD_TRUE 或者 TD_FALSE
    td_bool send_to_vgs = vpss_chn_dump_is_vgs_required(grp, chn);
	printf("vpss_chn_dump_get_frame send_to_vgs=%d\n",send_to_vgs);
	//根据VPSS_GET_CHN_FRAME_CONTINUOUSLY和frame_cnt决定dump的帧是否连续
    td_bool get_ctoninuous_frame = vpss_chn_dump_is_frame_continuous(frame_cnt, send_to_vgs);
	printf("vpss_chn_dump_get_frame get_ctoninuous_frame=%d\n",get_ctoninuous_frame);
	//如果连续
    if (get_ctoninuous_frame == TD_TRUE) {
		printf("vpss_chn_dump_get_frame get_ctoninuous_frame-----\n");
        ret = vpss_chn_dump_continuous_frame(grp, chn, frame_cnt);
        if (ret != TD_SUCCESS) {
            vpss_chn_dump_release_multi_frame(grp, chn, frame_cnt);
        }
	//如果不连续
    } else {
    	printf("vpss_chn_dump_get_frame get_unctoninuous_frame+++++++\n");
        ret = vpss_chn_dump_uncontinuous_frame(grp, chn, frame_cnt, send_to_vgs);
    }

    return ret;
}
//尝试得到一帧数据，但是没有保存
static td_s32 vpss_chn_dump_try_get_frame(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt)
{
    const td_s32 milli_sec = GET_CHN_FRAME_TIMEOUT;
    td_s32 try_times = 10;
    td_s32 ret;
	//调用ss_mpi_vpss_get_chn_frame获取grp的chn中的数据，写入g_frame。超时时间为GET_CHN_FRAME_TIMEOUT参数的单位是毫秒
    while (ss_mpi_vpss_get_chn_frame(grp, chn, &g_frame, milli_sec) != TD_SUCCESS) {//milli_sec swann
        if (g_signal_flag == 1) {
            printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
            return TD_FAILURE;
        }
        try_times--;
        if (try_times <= 0) {
            printf("get frame error for 10 times,now exit !!!\n");
            return TD_FAILURE;
        }

        usleep(40000); /* 40000 sleep swann*/ //设置了超时等待时间，就不用延时了
        printf("----------vpss_chn_dump_try_get_frame-------------\n");
    }
	//检查上面获取的g_frame的video_format信息是否为OT_VIDEO_FORMAT_LINEAR
    if (g_frame.video_frame.video_format != OT_VIDEO_FORMAT_LINEAR) {
        printf("only support linear frame dump!\n");
        ss_mpi_vpss_release_chn_frame(grp, chn, &g_frame);
        g_frame.pool_id = OT_VB_INVALID_POOL_ID;
        return TD_FAILURE;
    }
	//根据输入参数grp chn frame_cnt信息，得到保存的文件名字./vpss_grp0_chn1_720x576_P420_100.yuv，并打开得到描述符g_pfd
    if (vpss_chn_dump_make_yuv_file_name(grp, chn, frame_cnt) != TD_SUCCESS) {
        ss_mpi_vpss_release_chn_frame(grp, chn, &g_frame);
        g_frame.pool_id = OT_VB_INVALID_POOL_ID;
        return TD_FAILURE;
    }
	//调用ss_mpi_vpss_release_chn_frame释放grp的chn
    ret = ss_mpi_vpss_release_chn_frame(grp, chn, &g_frame);
    if (ret != TD_SUCCESS) {
        printf("Release frame error ,now exit !!!\n");
        return TD_FAILURE;
    }
    g_frame.pool_id = OT_VB_INVALID_POOL_ID;
    return TD_SUCCESS;
}
//调用ss_mpi_vpss_set_chn_attr设置depth为2，chn_mode为OT_VPSS_CHN_MODE_USER
//然后调用vpss_chn_dump_try_get_frame和vpss_chn_dump_get_frame获取图像
//td_void vpsschn_dump_frame(ot_vpss_grp grp, ot_vpss_chn chn, td_u32 frame_cnt)
td_void vpsschn_dump_frame()
{
	ot_vpss_grp grp=0;//vpss grp0
	ot_vpss_chn chn=1; //vpss ch1
	td_u32 frame_cnt=10;

    const td_u32 depth = 2;
    td_s32 ret;
    ot_vpss_chn_attr chn_attr;
    ot_vpss_ext_chn_attr ext_chn_attr;
	vpss_chn_dump_init_global_params();//swann 20241120
	//如果传入的chn超出vpss的物理通道范围，则配置扩展通道
    if (chn >= OT_VPSS_MAX_PHYS_CHN_NUM) {
        ret = ss_mpi_vpss_get_ext_chn_attr(grp, chn, &ext_chn_attr);
        if (ret != TD_SUCCESS) {
            printf("get ext chn attr error!!!\n");
            return;
        }
		//仅修改depth信息，其他不变
        g_orig_depth = ext_chn_attr.depth;
        ext_chn_attr.depth = depth;
		//配置扩展通道属性
        if (ss_mpi_vpss_set_ext_chn_attr(grp, chn, &ext_chn_attr) != TD_SUCCESS) {
            printf("set ext chn attr error!!!\n");
            goto exit;
        }
//-----实际走的是这里----------------------------------------//
    } else {//如果传入的chn在vpss的物理通道范围，则配置普通通道
        ret = ss_mpi_vpss_get_chn_attr(grp, chn, &chn_attr);
        if (ret != TD_SUCCESS) {
            printf("get chn attr error!!!\n");
            return;
        }
		//仅修改depth和chn_mode信息，其他不变
        g_orig_depth = chn_attr.depth;
        g_orig_chn_mode = chn_attr.chn_mode;
        chn_attr.depth = depth;
        chn_attr.chn_mode = OT_VPSS_CHN_MODE_USER;
		////配置扩展普通通道属性
        if (ss_mpi_vpss_set_chn_attr(grp, chn, &chn_attr) != TD_SUCCESS) {
            printf("set chn attr error!!!\n");
            goto exit;
        }
    }

    g_vpss_depth_flag = 1;

    if (memset_s(&g_frame, sizeof(ot_video_frame_info), 0, sizeof(ot_video_frame_info)) != EOK) {
        printf("memset_s frame error!!!\n");
        goto exit;
    }
    g_frame.pool_id = OT_VB_INVALID_POOL_ID;
	//尝试得到一帧数据，但是没有保存
    if (vpss_chn_dump_try_get_frame(grp, chn, frame_cnt) != TD_SUCCESS) {
        goto exit;
    }
	//从grp的 chn获取frame_cnt帧并保存文件
    if (vpss_chn_dump_get_frame(grp, chn, frame_cnt) != TD_SUCCESS) {
        goto exit;
    }

exit:
    vpss_restore(grp, chn);
    return;
}

static td_void usage(td_void)
{
    printf("\n"
           "*************************************************\n"
           "Usage: ./vpss_chn_dump [VpssGrp] [VpssChn] [FrmCnt]\n"
           "1)VpssGrp: \n"
           "   Vpss group id\n"
           "2)VpssChn: \n"
           "   vpss chn id\n"
           "3)FrmCnt: \n"
           "   the count of frame to be dumped\n"
           "*)Example:\n"
           "   e.g : ./vpss_chn_dump 0 0 1\n"
           "   e.g : ./vpss_chn_dump 1 4 2\n"
           "*)set VPSS_GET_CHN_FRAME_CONTINUOUSLY to 1 to get continuous frame\n"
           "*************************************************\n"
           "\n");
}

td_void vpss_chn_dump_init_global_params(td_void)
{
    g_vpss_depth_flag = 0;
    g_signal_flag = 0;
    g_user_page_addr[0] = TD_NULL;
    g_user_page_addr[1] = TD_NULL;
    g_dump_mem.vb_blk = -1;
    g_dump_mem.vb_pool = OT_VB_INVALID_POOL_ID;
    g_dump_mem.phys_addr = 0;
    g_dump_mem.virt_addr = TD_NULL;
    g_frame.pool_id = OT_VB_INVALID_POOL_ID;
    g_orig_depth = 0;
    g_pool = OT_VB_INVALID_POOL_ID;
    g_handle = -1;
    g_blk_size = 0;
    g_size = 0;
    g_pfd = TD_NULL;
}

static td_bool vpss_chn_dump_is_valid_digit_str(const td_char *str)
{
    size_t i;
    size_t str_len;

    str_len = strlen(str);
    if (str_len > MAX_DIGIT_LEN) {
        return TD_FALSE;
    }

    for (i = 0; i < str_len; i++) {
        if (str[i] < '0' || str[i] > '9') {
            return TD_FALSE;
        }
    }
    return TD_TRUE;
}

static td_s32 vpss_get_argv_val(char *argv[], td_u32 index, td_slong *val)
{
    td_char *end_ptr = TD_NULL;
    td_slong result;

    errno = 0;
    result = strtol(argv[index], &end_ptr, VPSS_ATTR_ARGV_BASE);
    if ((end_ptr == argv[index]) || (*end_ptr != '\0')) {
        return TD_FAILURE;
    } else if (((result == LONG_MIN) || (result == LONG_MAX)) &&
        (errno == ERANGE)) {
        return TD_FAILURE;
    }

    *val = result;
    return TD_SUCCESS;
}

static td_s32 vpss_parse_input_param(char *argv[], td_s32 *frame_cnt)
{
    td_slong result = 0;
    td_s32 ret;

    ret = vpss_get_argv_val(argv, 1, &result);
    if (ret != TD_SUCCESS) {
        printf("get grp failed!\n");
        return TD_FAILURE;
    }
    g_vpss_grp = (ot_vpss_grp)result;
    if (!value_between(g_vpss_grp, 0, OT_VPSS_MAX_GRP_NUM - 1)) {
        printf("grp id must be [0,%d]!!!!\n\n", OT_VPSS_MAX_GRP_NUM - 1);
        return TD_FAILURE;
    }

    ret = vpss_get_argv_val(argv, 2, &result); /* chn: 2 */
    if (ret != TD_SUCCESS) {
        printf("get chn failed!\n");
        return TD_FAILURE;
    }
    g_vpss_chn = (ot_vpss_chn)result;
    if (!value_between(g_vpss_chn, 0, OT_VPSS_MAX_CHN_NUM - 1)) {
        printf("chn id must be [0,%d]!!!!\n\n", OT_VPSS_MAX_CHN_NUM - 1);
        return TD_FAILURE;
    }

    ret = vpss_get_argv_val(argv, 3, &result); /* frame cnt: 3 */
    if (ret != TD_SUCCESS) {
        printf("get frame cnt failed!\n");
        return TD_FAILURE;
    }
    *frame_cnt = (td_s32)result; /* 3 frame count, base 10 */
    if (*frame_cnt <= 0 || *frame_cnt > MAX_DUMP_FRAME_CNT) {
        printf("dump frame cnt %d is out of range [1, %d]!\n", *frame_cnt, MAX_DUMP_FRAME_CNT);
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

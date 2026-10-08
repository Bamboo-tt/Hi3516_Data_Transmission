/*
 * Copyright (c) Hisilicon Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include "securec.h"
//#include "ss_mpi_sys_mem.h"
#include "../../../cbb/isp/ext_inc/ot_mpi_sys_mem.h"

#include "hi_mpi_sys.h"
#include "hi_mpi_aidetect.h"
#include "ot_common_venc.h"
#include "hi_mpi_venc.h"

/////////////////////////////////////////
td_char *g_p_img_data = TD_NULL;
td_phys_addr_t u_phy_addr = 0;
td_void *p_vir_addr = TD_NULL;
ot_aidetect_result_array g_result;
td_u32 g_index_num = 0;
pthread_t g_cpu_mem_th;
/////////////////////////////////////////////////
#define OT_SAMPLE_AIDETECT_MAX_OUTPUT_RECT_NUM (20)
#define OT_SAMPLE_AIDETECT_DEMO_MAX_LEN (256)
#define OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN (256 + 1)
#define OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_LEN (256 * 2)
#define OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN (256 * 2 + 1)
#define OT_SAMPLE_AIDETECT_MODEL_PATH_INPUT (1)
#define OT_SAMPLE_AIDETECT_STATICS_CONVERT_NUM (100)
#define OT_SAMPLE_AIDETECT_SEC_TIME_CONVERT_NUM (1000000)
#define OT_SAMPLE_AIDETECT_MICROSEC_TIME_CONVERT_NUM (1000.f)
#define OT_SAMPLE_AIDETECT_FILE_SEEK_POSITION (-512)
#define OT_SAMPLE_AIDETECT_IS_VIDEO (1)
#define OT_SAMPLE_AIDETECT_CHN_0 (0)
#define OT_SAMPLE_AIDETECT_SHOW_GRAY_RECT (1)
#define OT_SAMPLE_AIDETECT_SHOW_GRAY_THICKNESS (2)
#define OT_SAMPLE_AIDETECT_INDEX_NUM (5)
#define SAMPLE_AIDETECT_SHOW_RESULT
td_u32 g_input_frame_width = 0;
td_u32 g_input_frame_height = 0;
static td_s32 g_video_flg =
    1; // 1: input video; other: input image, the format is NV21, and resolutin is got by "ss_mpi_aidetect_get_model_info" 
static td_s32 g_path_or_men = 1;           // 1: input model path ,other: input model memory
static td_u32 g_mmz_used_inited_value = 0; // the initial usage of mmz
static td_u32 g_model_class = 2;//1 hvf,2 pet,3 package
static td_char g_src_model_path[OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN] =
    "./model/det_pet_normal.bin";//"./model/det_hvf_normal.bin";//det_hvf_normal det_pet_normal det_package_normal
static td_char g_src_res_path[OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN] = "./vpss_grp0_chn1_1024x576_P420_10.yuv";//"./vpss_grp0_chn1_1024x576_P420_10.yuv";//./data/video/hvf.yuv";//hvf package pet
static td_char g_out_result_path[OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN] = "./output";//"./output";
static ot_aidetect_model_info g_model_info = { 0 };
FILE *fd_out_file = NULL;
static td_char class_types[OT_AIDETECT_CLASS_BUTT][OT_SAMPLE_AIDETECT_DEMO_MAX_LEN] = {
    "face",
    "human",
    "vehicle",
    "pet",
    "garbage",
    "bag",
    "wallet",
    "phone"
};

static td_char g_track_status[OT_AIDETECT_TRACK_STATUS_BUTT][OT_SAMPLE_AIDETECT_DEMO_MAX_LEN] = {
    "new",
    "update",
    "die",
    "valid"
};

static td_bool g_is_run_finsh = TD_FALSE;
static pthread_mutex_t g_lock;

static td_u32 sample_aidetect_min(td_u32 x, td_u32 y)
{
    return (x > y) ? y : x;
}

static td_u32 sample_aidetect_max(td_u32 x, td_u32 y)
{
    return (x > y) ? x : y;
}
typedef struct sample_aidetect_cpu_info_t {
    td_u64 user;
    td_u64 sys;
    td_u64 nice;
    td_u64 idle;
    td_u64 unknown1;
    td_u64 unknown2;
    td_u64 unknown3;
} sample_aidetect_cpu_info;
//将"/proc/stat"中的数据解析写入_st_cpu_info
//获取cpu信息
static td_void sample_aidetect_get_cpu_data(sample_aidetect_cpu_info *_st_cpu_info)
{
    td_char c_tmp[OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN] = {0};
    FILE *fd = TD_NULL;

    fd = fopen("/proc/stat", "r");
    if (fd == TD_NULL) {
        (td_void)printf("[%s:%d] fopen error.\n", __func__, __LINE__);
        return;
    }

    (td_void)memset_s(_st_cpu_info, sizeof(sample_aidetect_cpu_info), 0, sizeof(sample_aidetect_cpu_info));
    if (fscanf_s(fd, "%s %llu %llu %llu %llu %llu %llu %llu", c_tmp, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN,
        &(_st_cpu_info->user), &(_st_cpu_info->sys), &(_st_cpu_info->nice), &(_st_cpu_info->idle),
        &(_st_cpu_info->unknown1), &(_st_cpu_info->unknown2), &(_st_cpu_info->unknown3)) == -1) {
        (td_void)printf("[%s:%d] fscanf_s error.\n", __func__, __LINE__);
    }
    (td_void)fclose(fd);
    fd = TD_NULL;
}
//将"/proc/[pid]/stat"中的数据解析写入_st_cpu_info
//获取当前pid占用的cpu信息
static td_void sample_aidetect_get_cpu_data_pid(td_u32 pid, sample_aidetect_cpu_info *_st_cpu_info)
{
    td_char thread_stat_file[OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN] = {0};
    FILE *fd = TD_NULL;

    if (snprintf_s(thread_stat_file, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, OT_SAMPLE_AIDETECT_DEMO_MAX_LEN,
        "/proc/%u/stat", pid) == -1) {
        (td_void)printf("[%s:%d] snprintf_s error.\n", __func__, __LINE__);
        return;
    }

    fd = fopen(thread_stat_file, "r");
    if (fd == TD_NULL) {
        (td_void)printf("[%s:%d] fopen error.\n", __func__, __LINE__);
        return;
    }
    if (fscanf_s(fd, "%*d %*s %*s %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %llu %llu", &(_st_cpu_info->user),
        &(_st_cpu_info->sys)) == -1) {
        (td_void)printf("[%s:%d] fsanf error.\n", __func__, __LINE__);
    }
    (td_void)fclose(fd);
    fd = TD_NULL;
}
//返回cpu_used_tmp信息%，cpu used = 4.411765
//通过新旧CPU info data对比，计算差量
// get the cpu usage of the process
td_double get_pid_used_cpu(td_u32 pid)
{
    td_double d_user = 0.0f;
    td_double d_sys = 0.0f;
    td_double d_nice = 0.0f;
    td_double d_idle = 0.0f;
    td_double unknown1 = 0.0f;
    td_double unknown2 = 0.0f;
    td_double unknown3 = 0.0f;
    td_double d_total = 0.0f;
    td_double d_cpu_usage = 0.0f;
    sample_aidetect_cpu_info st_sys_total_old_cpu;
    sample_aidetect_cpu_info st_sys_total_new_cpu;
    sample_aidetect_cpu_info st_pid_total_old_cpu;
    sample_aidetect_cpu_info st_pid_total_new_cpu;

    sample_aidetect_get_cpu_data(&st_sys_total_old_cpu);
    sample_aidetect_get_cpu_data_pid(pid, &st_pid_total_old_cpu);
    (td_void)sleep(1);
    sample_aidetect_get_cpu_data(&st_sys_total_new_cpu);
    sample_aidetect_get_cpu_data_pid(pid, &st_pid_total_new_cpu);
    d_user = (td_double)(st_sys_total_new_cpu.user - st_sys_total_old_cpu.user);
    d_sys = (td_double)(st_sys_total_new_cpu.sys - st_sys_total_old_cpu.sys);
    d_nice = (td_double)(st_sys_total_new_cpu.nice - st_sys_total_old_cpu.nice);
    d_idle = (td_double)(st_sys_total_new_cpu.idle - st_sys_total_old_cpu.idle);
    unknown1 = (td_double)(st_sys_total_new_cpu.unknown1 - st_sys_total_old_cpu.unknown1);
    unknown2 = (td_double)(st_sys_total_new_cpu.unknown2 - st_sys_total_old_cpu.unknown2);
    unknown3 = (td_double)(st_sys_total_new_cpu.unknown3 - st_sys_total_old_cpu.unknown3);
    d_total = d_user + d_sys + d_nice + d_idle + unknown1 + unknown2 + unknown3;
    d_user = (td_double)(st_pid_total_new_cpu.user - st_pid_total_old_cpu.user);
    d_sys = (td_double)(st_pid_total_new_cpu.sys - st_pid_total_old_cpu.sys);
    d_cpu_usage += ((d_user + d_sys) * OT_SAMPLE_AIDETECT_STATICS_CONVERT_NUM / d_total);

    return d_cpu_usage;
}
//返回这个pid进程对应的 os_mem_used 信息
// get the os memory usage of the process
static td_s32 sample_aidetect_get_proc_meminfo_os_mem()
{
    td_char file_name[OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN] = {0};
    FILE *fd = TD_NULL;
    td_char line[OT_SAMPLE_AIDETECT_DEMO_MAX_LEN] = {0};
    td_s32 vmrss = 0;

    pid_t pid = getpid();
    if (sprintf_s(file_name, OT_SAMPLE_AIDETECT_DEMO_MAX_LEN, "/proc/%d/status", pid) == -1) {
        (td_void)printf("[%s:%d] spintf_s cmd  error.\n", __func__, __LINE__);
        return vmrss;
    }

    fd = fopen(file_name, "r");
    if (fd == TD_NULL) {
        (td_void)printf("[%s:%d] open file %s error.\n", __func__, __LINE__, file_name);
        return vmrss;
    }

    // read the file content and get the value of VmRSS
    while (fgets(line, sizeof(line), fd) != TD_NULL) {
        if (sscanf_s(line, "VmRSS: %d kB", &vmrss) == 1) {
            break;
        }
    }

    // close the file
    (td_void)fclose(fd);
    fd = TD_NULL;

    return vmrss;
}
//返回mmz mem used信息
//&mem="used=%dKB"中的%d,返回mem
static td_u32 sample_aidetect_mmz_mem()
{
    FILE *fd = TD_NULL;
    td_char buf[OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN] = {0};
    td_u32 mem = 0;
    td_char *p_content = TD_NULL;

    fd = fopen("/proc/umap/media-mem", "r");
    if (fd == TD_NULL) {
        return mem;
    }

    (td_void)fseek(fd, OT_SAMPLE_AIDETECT_FILE_SEEK_POSITION, SEEK_END);
    while (fgets(buf, sizeof(buf) - 1, fd) != TD_NULL) {
        if ((strstr(buf, "total size") != TD_NULL) && ((p_content = strstr(buf, "used=")) != TD_NULL)) {
            if (sscanf_s(p_content, "used=%dKB", &mem) == -1) {
                (td_void)printf("[%s:%d] sscanf file error.\n", __func__, __LINE__);
            }

            break;
        }
    }

    (td_void)fclose(fd);
    fd = TD_NULL;
    return mem;
}
//每秒打印一次[sample_aidetect_cpu_mem_calc:246] cpu used = 4.411765, os mem used = 1972 KB, mmz mem used = 2248 KB
//结束打印一次[sample_aidetect_cpu_mem_calc:255] cnt: 4, average cpu used = 5.187100, average os mem used = 1996.000000 KB.
static td_void *sample_aidetect_cpu_mem_calc(td_void *args)
{
    (td_void)printf("[%s:%d] start.\n", __func__, __LINE__);
    td_u64 loop_cnt = 0;
    td_double cpu_used = 0.0f;
    td_double vm_ress_used = 0.0f;
    td_s32 os_mem_use_tmp = 0;
    td_u32 mmz_mem_tmp = 0;
    td_double cpu_used_tmp = 0;
    while (1) {
        (td_void)pthread_mutex_lock(&g_lock);
        if (g_is_run_finsh) {
            break;
        }
        (td_void)pthread_mutex_unlock(&g_lock);

        os_mem_use_tmp = sample_aidetect_get_proc_meminfo_os_mem();
        mmz_mem_tmp = sample_aidetect_mmz_mem() - g_mmz_used_inited_value;
        cpu_used_tmp = get_pid_used_cpu((td_u32)getpid());
      //  (td_void)printf("[%s:%d] cpu used = %f, os mem used = %d KB, mmz mem used = %d KB\n", __func__, __LINE__,
          //  cpu_used_tmp, os_mem_use_tmp, mmz_mem_tmp);
        cpu_used += cpu_used_tmp;
        vm_ress_used += (td_double)os_mem_use_tmp;
        ++loop_cnt;
        sleep(1);
    }

    if (loop_cnt) {
        (td_void)printf("[%s:%d] cnt: %llu, average cpu used = %f, average os mem used = %f KB.\n", __func__, __LINE__,
            loop_cnt, cpu_used / (td_double)loop_cnt, vm_ress_used / (td_double)loop_cnt);
    }

    return TD_NULL;
}

//打印帮助信息
//  the parameters introduction of sample
static td_void sample_aidetect_usage(td_char **argv)
{
    (td_void)printf("usage: %s [i:f:v:o:m:] \n"
        "        -i   input file path, yuv name\n"
        "        -f   images or yuv\n"
        "        -v   input model file or memory\n"
        "        -m   model path\n"
        "        -o   output file path\n",
        argv[0]);

    (td_void)printf("for example:\n"
        "%s -i ./video.yuv -f 1 -v 1 -m ./models/det.bin -o ../output/results\n",
        argv[0]);
}

//根据g_out_result_path获取out_file路径result.txt，并fopen打开
static td_bool sample_aidetect_output_path()
{
    td_char out_file[OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN] = {0};

    if (strlen(g_out_result_path) <= 0) {
        return TD_TRUE;
    }

    if (access(g_out_result_path, F_OK) != 0) {
        if (mkdir(g_out_result_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
            (td_void)printf("[%s:%d]mkdir %s error", __func__, __LINE__, g_out_result_path);
            return TD_FALSE;
        }
    }

    if (snprintf_s(out_file, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_LEN,
        "%s/%s", g_out_result_path, "result.txt") == -1) {
        (td_void)printf("[%s:%d]snprintf_s error", __func__, __LINE__);
        return TD_FALSE;
    }
    fd_out_file = fopen(out_file, "w+");
	printf("fd_out_file ok ");
    return TD_TRUE;
}
// ./sample_aidetect -i ./video.yuv -f 1 -v 1 -m ./models/det.bin -o ../output/results
// 根据输入参数,配置g_src_res_path g_src_model_path g_out_result_path信息
static td_bool sample_aidetect_parase_param(td_s32 argc, td_char *argv[])
{
    td_s32 c = 0;
    // get the input param by user
    while ((c = getopt(argc, argv, "i:f:v:o:m:")) != EOF) {
        switch (c) {
            case 'i':
                (td_void)memset_s(g_src_res_path, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, 0,
                    OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN);
                (td_void)strncpy_s(g_src_res_path, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, optarg, strlen(optarg));
                break;
            case 'f':
                g_video_flg = atoi(optarg);
                break;
            case 'v':
                g_path_or_men = atoi(optarg);
                break;
            case 'm':
                (td_void)memset_s(g_src_model_path, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, 0,
                    OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN);
                (td_void)strncpy_s(g_src_model_path, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, optarg, strlen(optarg));
                break;
            case 'o':
                (td_void)memset_s(g_out_result_path, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, 0,
                    OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN);
                (td_void)strncpy_s(g_out_result_path, OT_SAMPLE_AIDETECT_DEMO_MAX_PLUS_ONE_LEN, optarg, strlen(optarg));
                break;
            default:
                sample_aidetect_usage(argv);
                return TD_FALSE;
        }
    }

    if (!sample_aidetect_output_path()) {
        (td_void)printf("[%s:%d]sample_aidetect_output_path error", __func__, __LINE__);
        return TD_FALSE;
    }

    return TD_TRUE;
}
//检查 g_src_model_path g_src_res_path g_out_result_path 是否真实存在
static td_bool sample_aidetect_check_input_param()
{
    if ((access(g_src_model_path, F_OK) != 0) || (access(g_src_res_path, F_OK) != 0) ||
        (access(g_out_result_path, F_OK) != 0)) {
        printf("model file path[%s] or res file path[%s] or out result path[%s] not exist.\n", g_src_model_path,
            g_src_res_path, g_out_result_path);
        return TD_FALSE;
    }
    return TD_TRUE;
}
//读取path计算长度len，然后fread(data, *len, 1, fd);返回data
//将路径为path的model_file读取，写入内存中的data地址并返回，以便程序调用
static td_char *sample_aidetect_read_model_file(const td_char *path, td_u32 *len)
{
    FILE *fd = TD_NULL;
    td_char *data = TD_NULL;

    fd = fopen(path, "rb");
    if (fd == TD_NULL) {
        return TD_NULL;
    }

    (td_void)fseek(fd, 0, SEEK_END);
    *len = (td_u32)ftell(fd);
    (td_void)fseek(fd, 0, SEEK_SET);
    data = (td_char *)malloc(*len);
    if (data == TD_NULL) {
        (td_void)fclose(fd);
        return TD_NULL;
    }
    (td_void)memset_s(data, *len, 0, *len);
    (td_void)fread(data, *len, 1, fd);
    (td_void)fclose(fd);
    fd = TD_NULL;

    return data;
}
/*
	sample_aidetect_proc_model_info: input image w:1024,h:576, class num: 3
	class type name:face[0]
	class type name:human[1]
	class type name:vehicle[2]
	sample_aidetect_proc_model_info: input image w:1024,h:576, class num: 1 
	class type name:pet[3]
	sample_aidetect_proc_model_info: input image w:1024,h:576, class num: 4
	class type name:garbage[4]
	class type name:bag[5]
	class type name:wallet[6]
	class type name:phone[7]
*/
//读取模型信息，打印，并写入chn_attr，然后调用 ss_mpi_aidetect_set_chn_attr 将chn_attr写入OT_SAMPLE_AIDETECT_CHN_0
static td_bool sample_aidetect_proc_model_info()
{
    td_s32 ret = TD_SUCCESS;
    ot_aidetect_chn_attr chn_attr;
    td_u32 i = 0;

    (td_void)memset_s(&g_model_info, sizeof(ot_aidetect_model_info), 0, sizeof(ot_aidetect_model_info));
	//读取通道 OT_SAMPLE_AIDETECT_CHN_0 的模型信息，写入 g_model_info
    ret = ss_mpi_aidetect_get_model_info(OT_SAMPLE_AIDETECT_CHN_0, &g_model_info);
    if (TD_SUCCESS != ret) {
        (td_void)printf("%s: ss_mpi_aidetect_get_model_info error: %X.\n", __func__, ret);
        return TD_FALSE;
    }
	//将上面得到的g_model_info中的帧尺寸信息写入g_input_frame_width g_input_frame_height，并打印
    g_input_frame_width = g_model_info.size.width;
    g_input_frame_height = g_model_info.size.height;
    (td_void)printf("%s: input image w:%u,h:%u, class num: %u\n", __func__, g_input_frame_width, g_input_frame_height,
        g_model_info.class_num);
    if (OT_SAMPLE_AIDETECT_IS_VIDEO == g_video_flg) { // input video  then enable the track，g_video_flg 在上面parase_param设置
        (td_void)memset_s(&chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));
        chn_attr.track_class_num = g_model_info.class_num;
        for (i = 0; i < g_model_info.class_num; i++) {
            (td_void)printf("class type name:%s[%d]\n", class_types[g_model_info.classes[i]],
                (td_s32)g_model_info.classes[i]);
            chn_attr.track_class_attr[i].class_type = g_model_info.classes[i];
            chn_attr.track_class_attr[i].track_en = TD_TRUE;
        }

        ret = ss_mpi_aidetect_set_chn_attr(OT_SAMPLE_AIDETECT_CHN_0, &chn_attr);
        (td_void)printf("[%s: %d]ss_mpi_aidetect_set_chn_attr ret:%X\n", __func__, __LINE__, ret);
    }

    return TD_TRUE;
}
/*
	class type:garbage,det threshold:0.300000, track miss num:8,model prority:[1,3,0,0]
	class type:bag,det threshold:0.350000, track miss num:8,model prority:[1,3,0,0]
	class type:wallet,det threshold:0.300000, track miss num:8,model prority:[1,3,0,0]
	class type:phone,det threshold:0.400000, track miss num:8,model prority:[1,3,0,0]
*/
//调用ss_mpi_aidetect_get_chn_param读取OT_SAMPLE_AIDETECT_CHN_0的信息到chn_param，并打印
static td_void sample_aidetect_show_chn_param()
{
    td_u32 i = 0;
    td_s32 ret = TD_SUCCESS;
    ot_aidetect_chn_param chn_param;

    memset_s(&chn_param, sizeof(ot_aidetect_chn_param), 0, sizeof(ot_aidetect_chn_param));
    ret = ss_mpi_aidetect_get_chn_param(OT_SAMPLE_AIDETECT_CHN_0, &chn_param);
    if (ret == TD_SUCCESS) {
        for (i = 0; i < chn_param.detect_threshold_num; i++) {
            (td_void)printf("class type:%s,det threshold:%f, track miss num:%u,model prority:[%d,%u,%u,%u]\n",
                class_types[chn_param.detect_threshold[i].class_type], chn_param.detect_threshold[i].detect_threshold,
                chn_param.detect_threshold[i].track_miss_frame_num, chn_param.model_priority.preemp_en,
                chn_param.model_priority.priority, chn_param.model_priority.priority_up_step_timeout,
                chn_param.model_priority.priority_up_top_timeout);
        }
    } else {
        (td_void)printf("%s ss_mpi_aidetect_get_chn_param error:%X\n", __func__, ret);
    }
}
//首先将model_load_mode      model_path path_size填充入t_input_model_info，
//然后调用 ss_mpi_aidetect_create_chn 将t_input_model_info和chn_attr写入OT_SAMPLE_AIDETECT_CHN_0
//然后调用 sample_aidetect_proc_model_info 读取模型信息，打印，并写入chn_attr，然后调用 ss_mpi_aidetect_set_chn_attr 将chn_attr写入OT_SAMPLE_AIDETECT_CHN_0
//最后调用 sample_aidetect_show_chn_param 读取OT_SAMPLE_AIDETECT_CHN_0的信息到chn_param，并打印
//总结：这个函数完成了 读取model、ss_mpi_aidetect_create_chn、ss_mpi_aidetect_set_chn_attr，并打印
static td_bool sample_aidetect_init_model()
{
    td_char *model_data = TD_NULL;
    td_u32 model_len = 0;
    ot_aidetect_input_model t_input_model_info;
    ot_aidetect_chn_attr chn_attr;
    td_s32 ret = TD_SUCCESS;
	//将model_load_mode      model_path path_size填充入t_input_model_info
    (td_void)memset_s(&t_input_model_info, sizeof(ot_aidetect_input_model), 0, sizeof(ot_aidetect_input_model));
    if (OT_SAMPLE_AIDETECT_MODEL_PATH_INPUT == g_path_or_men) {
        t_input_model_info.model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_PATH;//从本地路径载入
        t_input_model_info.model = (td_void *)g_src_model_path;//绑定model路径
        t_input_model_info.size = (td_u32)strlen(g_src_model_path);//model路径的长度
    } else {
        model_data = sample_aidetect_read_model_file(g_src_model_path, &model_len);
        if (TD_NULL == model_data) {
            (td_void)printf("%s: read model[%s] data error.\n", __func__, g_src_model_path);
            return TD_FALSE;
        }
        t_input_model_info.model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_MEMORY;
        t_input_model_info.model = model_data;
        t_input_model_info.size = model_len;
    }

    (td_void)memset_s(&chn_attr, sizeof(ot_aidetect_chn_attr), 0, sizeof(ot_aidetect_chn_attr));
	//然后调用 ss_mpi_aidetect_create_chn 将t_input_model_info和chn_attr写入OT_SAMPLE_AIDETECT_CHN_0
    ret = ss_mpi_aidetect_create_chn(OT_SAMPLE_AIDETECT_CHN_0, &t_input_model_info, &chn_attr);

    if (model_data != TD_NULL) {
        free(model_data);
        model_data = TD_NULL;
    }

    if (TD_SUCCESS != ret) {
        (td_void)printf("%s: ss_mpi_aidetect_create_chn error: %X.\n", __func__, ret);
        return TD_FALSE;
    }
	//内部调用ss_mpi_aidetect_set_chn_attr读取模型信息，打印，并写入chn_attr，然后调用 ss_mpi_aidetect_set_chn_attr 将chn_attr写入OT_SAMPLE_AIDETECT_CHN_0
    if (!sample_aidetect_proc_model_info()) {
        (td_void)ss_mpi_aidetect_destroy_chn(OT_SAMPLE_AIDETECT_CHN_0);
        return TD_FALSE;
    }
	//内部调用ss_mpi_aidetect_get_chn_param读取OT_SAMPLE_AIDETECT_CHN_0的信息到chn_param，并打印
    sample_aidetect_show_chn_param();

    return TD_TRUE;
}
/*
	result->class_num=g_model_info.class_num
    result->object_class[i].class_type = g_model_info.classes[i];
    result->object_class[i].object_capacity = OT_SAMPLE_AIDETECT_MAX_OUTPUT_RECT_NUM;
    result->object_class[i].objects =  malloc xxx
*/
//填充输入参数result内部的class_num object_class，并malloc object_class[i].objects
td_void sample_aidetect_result_init(ot_aidetect_result_array *result)
{
    td_u32 i = 0;

    (td_void)memset_s(result, sizeof(ot_aidetect_result_array), 0, sizeof(ot_aidetect_result_array));
    result->class_num =
        (g_model_info.class_num > OT_AIDETECT_CLASS_BUTT ? OT_AIDETECT_CLASS_BUTT : g_model_info.class_num);
    for (i = 0; i < result->class_num; ++i) {
        result->object_class[i].class_type = g_model_info.classes[i];
        result->object_class[i].object_capacity = OT_SAMPLE_AIDETECT_MAX_OUTPUT_RECT_NUM;
        result->object_class[i].objects =
            (ot_aidetect_object *)malloc(sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
        if (result->object_class[i].objects == TD_NULL) {
            continue;
        }
        (td_void)memset_s(result->object_class[i].objects,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
    }
}
//清零输入参数result
static td_void sample_aidetect_result_clear(ot_aidetect_result_array *result)
{
    td_u32 i = 0;
    for (i = 0; i < result->class_num; ++i) {
        result->object_class[i].object_num = 0;
        (td_void)memset_s(result->object_class[i].objects,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity, 0,
            sizeof(ot_aidetect_object) * result->object_class[i].object_capacity);
    }
}
//free输入参数result中的object_class[i].objects
static td_void sample_aidetect_result_free(ot_aidetect_result_array *result)
{
    td_u32 i = 0;
    if (result == TD_NULL)
        return;
    for (i = 0; i < result->class_num; ++i) {
        if (result->object_class[i].objects != TD_NULL) {
            free(result->object_class[i].objects);
            result->object_class[i].objects = TD_NULL;
        }
    }
}
//将全局变量g_input_frame_width g_input_frame_height赋值给src_frame->width     src_frame->height
//将输入参数u_phy_addr p_vir_addr赋值给src_frame->phys_addr、src_frame->virt_addr
//将输入参数p_img_data拷贝到src_frame->virt_add
//总结：将p_img_data u_phy_addr p_vir_addr填充到src_frame
static td_bool sample_aidetect_make_frame(td_char *p_img_data, td_phys_addr_t u_phy_addr, td_void *p_vir_addr,
    ot_video_frame *src_frame)
{
    td_s32 ret = TD_SUCCESS;
    td_u32 size = 0, size_half = 0;

    src_frame->width = g_input_frame_width;
    src_frame->height = g_input_frame_height;
    size = src_frame->width * src_frame->height;
    size_half = size / 2; // 2: divided by 2
    src_frame->phys_addr[0] = u_phy_addr;
    src_frame->phys_addr[1] = u_phy_addr + size;
    src_frame->virt_addr[0] = p_vir_addr;//装了size大小的完整的yuv数据的p_img_data图片
    src_frame->virt_addr[1] = p_vir_addr + size;//装了size/2大小的一半的yuv数据的p_img_data图片
    src_frame->stride[0] = g_input_frame_width;
    src_frame->stride[1] = g_input_frame_width;
    (td_void)memcpy_s((td_char *)src_frame->virt_addr[0], size, p_img_data, size);
    (td_void)memcpy_s((td_char *)src_frame->virt_addr[1], size_half, p_img_data, size_half);
    src_frame->pixel_format = OT_PIXEL_FORMAT_YVU_PLANAR_420;
    ret = ss_mpi_sys_flush_cache(u_phy_addr, p_vir_addr, size + size_half);
    if (ret != TD_SUCCESS) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_flush_cache error ret (%#x).\n", __func__, __LINE__, ret);
        return TD_FALSE;
    }

    return TD_TRUE;
}
//根据rect中的x,y,width,height信息在data上面画矩形框
static td_void sample_aidetect_draw_rect_to_gray(td_char *data, ot_rect *rect, td_u32 thickness)
{
    td_u32 left_top_x = 0, left_top_y = 0, right_bottom_x = 0, right_bottom_y = 0;
    td_u32 channel = OT_SAMPLE_AIDETECT_SHOW_GRAY_RECT;
    td_u8 color[OT_SAMPLE_AIDETECT_SHOW_GRAY_RECT] = {255};
    td_u32 stride = g_input_frame_width;
    td_u32 i = 0, j = 0, k = 0;

    left_top_x = sample_aidetect_min(sample_aidetect_max((td_u32)rect->x, 0), g_input_frame_width - 1);
    left_top_y = sample_aidetect_min(sample_aidetect_max((td_u32)rect->y, 0), g_input_frame_height - 1);
    right_bottom_x =
        sample_aidetect_min(sample_aidetect_max((td_u32)rect->x + rect->width, 0), g_input_frame_width - 1);
    right_bottom_y =
        sample_aidetect_min(sample_aidetect_max((td_u32)rect->y + rect->height, 0), g_input_frame_height - 1);
    if (left_top_x > right_bottom_x || left_top_x + thickness > right_bottom_x || left_top_y > right_bottom_y ||
        left_top_y + thickness > right_bottom_y)
        return;

    // draw top line
    for (i = left_top_y; i < left_top_y + thickness; ++i) {
        for (j = left_top_x; j < right_bottom_x; ++j) {
            for (k = 0; k < channel; ++k) {
                data[i * stride + j * channel + k] = color[k];
            }
        }
    }

    // draw bottom line
    for (i = right_bottom_y - thickness; i < right_bottom_y; ++i) {
        for (j = left_top_x; j < right_bottom_x; ++j) {
            for (k = 0; k < channel; ++k) {
                data[i * stride + j * channel + k] = color[k];
            }
        }
    }

    // draw left and right line
    for (i = left_top_y + thickness; i < right_bottom_y - thickness; ++i) {
        for (j = left_top_x; j < left_top_x + thickness; ++j) {
            for (k = 0; k < channel; ++k) {
                data[i * stride + j * channel + k] = color[k];
            }
        }

        for (j = right_bottom_x - thickness; j < right_bottom_x; ++j) {
            for (k = 0; k < channel; ++k) {
                data[i * stride + j * channel + k] = color[k];
            }
        }
    }
}
//rst=./output/frame_0_wh[1024_576].gray		...etc
//将img_data拷贝到gray_data，然后根据&result->object_class[i].objects[j].detect_rect在gray_data画矩形框
//最后将gray_data写入rst文件
//总结：将检测结果detect_rect画矩形框在gray_data中，并保存文件
static td_void sample_aidetect_show_result(ot_aidetect_result_array *result, td_u32 frameid, td_char *img_data)
{
    td_char *gray_data = TD_NULL;
    td_u32 i = 0, j = 0;
    FILE *fd = TD_NULL;
    td_char rst[OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN] = {0};
	//rst=./output/frame_0_wh[1024_576].gray		...etc
    if (snprintf_s(rst, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_LEN,
        "%s/frame_%u_wh[%u_%u].gray", g_out_result_path, frameid, g_input_frame_width, g_input_frame_height) == -1) {
        (td_void)printf("%s: snprintf_s error.\n", __func__);
        return;
    }
    gray_data = (td_char *)malloc(g_input_frame_width * g_input_frame_height);
    if (gray_data == TD_NULL) {
        return;
    }
	//将img_data拷贝到gray_data
    (td_void)memcpy_s(gray_data, g_input_frame_width * g_input_frame_height, img_data,
        g_input_frame_width * g_input_frame_height);
    for (i = 0; i < result->class_num; ++i) {
        for (j = 0; j < result->object_class[i].object_num; j++) {
            if (result->object_class[i].objects[j].track_status == OT_AIDETECT_TRACK_STATUS_DIE) {
                continue;
            }
			//根据detect_rect中的x,y,width,height信息在gray_data上面画矩形框
            sample_aidetect_draw_rect_to_gray(gray_data, &result->object_class[i].objects[j].detect_rect,
                OT_SAMPLE_AIDETECT_SHOW_GRAY_THICKNESS);
        }
    }
	//将上面画好矩形框的gray_data写入rst文件并保存
    fd = fopen(rst, "w+");
    if (fd == TD_NULL) {
        free(gray_data);
        gray_data = TD_NULL;
        return;
    }
    (td_void)fwrite(gray_data, g_input_frame_width * g_input_frame_height, 1, fd);
    (td_void)fclose(fd);
    free(gray_data);
    gray_data = TD_NULL;
}

//rst=./output/frame_0_wh[1024_576].gray		...etc
//将img_data拷贝到gray_data，然后根据&result->object_class[i].objects[j].detect_rect在gray_data画矩形框
//最后将gray_data写入rst文件
//总结：将检测结果detect_rect画矩形框在gray_data中，并保存文件
static td_void sample_aidetect_show_result_new(ot_aidetect_result_array *result, td_u32 frameid, td_char *img_data, td_char *gray_data)
{

    td_u32 i = 0, j = 0;
    FILE *fd = TD_NULL;
    td_char rst[OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN] = {0};
	//rst=./output/frame_0_wh[1024_576].gray		...etc
    if (snprintf_s(rst, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_LEN,
        "%s/frame_%u_wh[%u_%u].gray", g_out_result_path, frameid, g_input_frame_width, g_input_frame_height) == -1) {
        (td_void)printf("%s: snprintf_s error.\n", __func__);
        return;
    }

	//将img_data拷贝到gray_data
    (td_void)memcpy_s(gray_data, g_input_frame_width * g_input_frame_height, img_data,
        g_input_frame_width * g_input_frame_height);
    for (i = 0; i < result->class_num; ++i) {		//标记所有类目 face human vehicle
        for (j = 0; j < result->object_class[i].object_num; j++) {	//每个类目标记object_num个框
            if (result->object_class[i].objects[j].track_status == OT_AIDETECT_TRACK_STATUS_DIE) {
                continue;
            }
			//根据detect_rect中的x,y,width,height信息在gray_data上面画矩形框
            sample_aidetect_draw_rect_to_gray(gray_data, &result->object_class[i].objects[j].detect_rect,
                OT_SAMPLE_AIDETECT_SHOW_GRAY_THICKNESS);
        }
    }

}
/*
	{class type: wallet, rect[369,88,67,85], track id: 1, track status: new[0], conf: 0.607430} 
	{class type: wallet, rect[330,223,103,94], track id: 2, track status: new[0], conf: 0.512159} 
	{class type: wallet, rect[258,122,81,92], track id: 3, track status: new[0], conf: 0.373990} 
*/
//将result、frameid信息打印，并写入fd_out_file
//总结：打印aidetect结果，并保存到./result.txt
static td_void sample_aidetect_save_result(ot_aidetect_result_array *result, td_u32 frameid)
{
    td_char rst[OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN] = {0};
    td_u32 i = 0, j = 0;

    if (fd_out_file == TD_NULL)
        return;

    if (snprintf_s(rst, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_LEN,
        "frame id: %u: ", frameid) == -1) {
        (td_void)printf("[%s: %d]: snprintf_s error.\n", __func__, __LINE__);
        return;
    }

    (td_void)fwrite(rst, strlen(rst), 1, fd_out_file);
    for (i = 0; i < result->class_num; ++i) {
        for (j = 0; j < result->object_class[i].object_num; j++) {
            if (result->object_class[i].objects[j].track_status == OT_AIDETECT_TRACK_STATUS_DIE) {
                (td_void)printf("class type: %s, track id :%u die,rect:[%u,%u,%u,%u]\n",
                    class_types[result->object_class[i].class_type], result->object_class[i].objects[j].track_id,
                    result->object_class[i].objects[j].detect_rect.x, result->object_class[i].objects[j].detect_rect.y,
                    result->object_class[i].objects[j].detect_rect.width,
                    result->object_class[i].objects[j].detect_rect.height);
                continue;
            }
            (td_void)printf("{class type: %s, rect[%u,%u,%u,%u], track id: %u, track status: %s[%d], conf: %f} \n",
                class_types[result->object_class[i].class_type], result->object_class[i].objects[j].detect_rect.x,
                result->object_class[i].objects[j].detect_rect.y, result->object_class[i].objects[j].detect_rect.width,
                result->object_class[i].objects[j].detect_rect.height, result->object_class[i].objects[j].track_id,
                g_track_status[result->object_class[i].objects[j].track_status],
                result->object_class[i].objects[j].track_status, result->object_class[i].objects[j].detect_confidence);

            (td_void)memset_s(rst, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN, 0,
                OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN);
            if (snprintf_s(rst, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_PLUS_ONE_LEN, OT_SAMPLE_AIDETECT_DEMO_TWO_MAX_LEN,
                "{class type: %s, rect[%u,%u,%u,%u], track id: %u, track status: %s[%d], conf: %f}%s",
                class_types[result->object_class[i].class_type], result->object_class[i].objects[j].detect_rect.x,
                result->object_class[i].objects[j].detect_rect.y, result->object_class[i].objects[j].detect_rect.width,
                result->object_class[i].objects[j].detect_rect.height, result->object_class[i].objects[j].track_id,
                g_track_status[result->object_class[i].objects[j].track_status],
                result->object_class[i].objects[j].track_status, result->object_class[i].objects[j].detect_confidence,
                ((i != result->class_num - 1) ? ", " : "")) == -1) {
                (td_void)printf("[%s: %d]: snprintf_s error.\n", __func__, __LINE__);
                return;
            }

            (td_void)fwrite(rst, strlen(rst), 1, fd_out_file);
        }
    }

    (td_void)fwrite("\n", strlen("\n"), 1, fd_out_file);
}

//sample_aidetect_proc_one_frame(g_index_num, p_img_data, data_img_size, fd, u_phy_addr, p_vir_addr, &result)
//g_index_num表示当前处理第几帧，用于保存结果写文件
//从fd中读取1个data_img_size大小（即w*h*3/2,YUV）到p_img_data，然后将p_img_data和u_phy_addr p_vir_addr等填充到src_frame
//然后调用ss_mpi_aidetect_process对src_frame侦测，将结果写入result。根据result保持到./result.txt，并画矩形框保存到gray文件中
//总结：调用ss_mpi_aidetect_process处理1帧图片,画矩形框，并保存framexxx.gray，result.txt，同时printf打印
td_bool sample_aidetect_proc_one_frame_new(td_u32 index_num, td_char *p_img_data, td_u32 data_img_size,
    td_phys_addr_t u_phy_addr, td_void *p_vir_addr, ot_aidetect_result_array *result, ot_video_frame *frame)
{
    td_u32 n_read_size = 0;
    td_s32 ret_sys = TD_SUCCESS;
    ot_aidetect_chn_status ch_status;
    struct timeval start, end;
    ot_video_frame src_frame;
	//[sample_aidetect_proc_one_frame: 716]: frame:5,avg frame rate:78, frame rate:0
	//每处理5帧调用ss_mpi_aidetect_query_status一次并打印ch_status
    if (0 == (index_num % OT_SAMPLE_AIDETECT_INDEX_NUM)) {
        (td_void)ss_mpi_aidetect_query_status(OT_SAMPLE_AIDETECT_CHN_0, &ch_status);
       // (td_void)printf("[%s: %d]: frame:%u,avg frame rate:%u, frame rate:%u\n", __func__, __LINE__,
         //   ch_status.recv_frames, ch_status.avg_frame_rate, ch_status.frame_rate);
    }
	
    (td_void)memset_s(p_img_data, data_img_size, 0, data_img_size);
/*
	//读取fd文件的 1个data_img_size字节大小 到p_img_data，并且指针后移至 1个data_img_size字节大小后。
	//这保证了循环读取时，每次读一帧,含YUV数据，往后依次逐帧读取
	//返回值为读取的size大小
    n_read_size = fread(p_img_data, 1, data_img_size, fd);
	//如果读取到的不是完整的 1个data_img_size大小 ，则结束
    if (n_read_size != data_img_size) {
        (td_void)printf("[%s: %d]: read file[%s] over!!\n", __func__, __LINE__, g_src_res_path);
        return TD_FALSE;
    }
*/
	//////////////////////////////////////这里改为拷贝
	td_char *addr_y=NULL;td_char *addr_uv=NULL;td_u32 yuv_size = 1024*576;
	td_u32 size = 1024*576;
    td_u32 size_half = size / 2; // 2: divided by 2
	addr_y = (td_char *)ss_mpi_sys_mmap(frame->phys_addr[0] , yuv_size);
	addr_uv = (td_char *)ss_mpi_sys_mmap(frame->phys_addr[1] , yuv_size/2);
    (td_void)memcpy_s(p_img_data, size, addr_y, size);
	(td_void)memcpy_s(p_img_data + size, size_half, addr_uv , size_half);


	//////////////////////////////////////////////////
	
	//将p_img_data 中的Y拷贝到src_frame，UV拷贝到src_frame，将u_phy_addr p_vir_addr等数据填充到src_frame
    // make the input frame
    if (!sample_aidetect_make_frame(p_img_data, u_phy_addr, p_vir_addr, &src_frame)) {
        (td_void)printf("[%s: %d]:sample_aidetect_make_frame fail!!\n", __func__, __LINE__);
        return TD_FALSE;
    }
	//调用ss_mpi_aidetect_process将src_frame交给OT_SAMPLE_AIDETECT_CHN_0处理，得到result。同时记录处理时间
    (td_void)gettimeofday(&start, TD_NULL);
	ret_sys = ss_mpi_aidetect_process(OT_SAMPLE_AIDETECT_CHN_0, &src_frame, result);
    (td_void)gettimeofday(&end, TD_NULL);
    if (TD_SUCCESS != ret_sys) {
        (td_void)printf("[%s: %d]: ss_mpi_aidetect_process error:%X!!\n", __func__, __LINE__, ret_sys);
        return TD_FALSE;
    }
	//[sample_aidetect_proc_one_frame:741] frame: 5, ss_mpi_aidetect_process ok and cost time: 13.157000 ms.
	//计算花费时间，并打印
    td_u64 timeuse =
        OT_SAMPLE_AIDETECT_SEC_TIME_CONVERT_NUM * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
   // (td_void)printf("[%s:%d] frame: %u, ss_mpi_aidetect_process ok and cost time: %f ms.\n", __func__, __LINE__,
   //     index_num, timeuse / OT_SAMPLE_AIDETECT_MICROSEC_TIME_CONVERT_NUM);
	///////////// 暂时不要打印 ///////////////////
	//打印aidetect结果result，并保存到./result.txt
    //sample_aidetect_save_result(result, g_index_num);
#ifdef SAMPLE_AIDETECT_SHOW_RESULT
	//将检测结果detect_rect画矩形框在gray_data中，并保存文件
    //sample_aidetect_show_result(result, g_index_num, p_img_data);
	td_char *gray_data = TD_NULL;
    gray_data = (td_char *)malloc(g_input_frame_width * g_input_frame_height);//仅操作Y分量
    if (gray_data == TD_NULL) {
        return;
    }
    sample_aidetect_show_result_new(result, index_num, p_img_data, gray_data);
#endif
	//清零输入参数result
    sample_aidetect_result_clear(result);


/////////////////////////////////////////////////这里添加 ss_mpi_venc_send_frame 发送数据给venc ch编码
//td_s32 ss_mpi_venc_send_frame(ot_venc_chn chn, const ot_video_frame_info *frame, td_s32 milli_sec);
	//frame修改替换Y分量，其他不变
	(td_void)memcpy_s(addr_y, size, gray_data, size);//frame只修改替换Y分量，其他不变
	ss_mpi_venc_send_frame(1, frame, 0);//向venc通道1发送标记后的frame数据
	if(g_model_class==1){
		usleep(5000);	//swann :hvf usleep(5000),pet usleep(20000),pack usleep(15000)
	}else if(g_model_class==2){
		usleep(20000);
	}else{
		usleep(15000);
	}
	
	//收尾工作
	ss_mpi_sys_munmap(addr_y, yuv_size);
	ss_mpi_sys_munmap(addr_uv, size_half);

	free(gray_data);
	gray_data = TD_NULL;

/////////////////////////////////////////////////////
    return TD_TRUE;
}
//sample_aidetect_proc_one_frame(g_index_num, p_img_data, data_img_size, fd, u_phy_addr, p_vir_addr, &result)
//g_index_num表示当前处理第几帧，用于保存结果写文件
//从fd中读取1个data_img_size大小（即w*h*3/2,YUV）到p_img_data，然后将p_img_data和u_phy_addr p_vir_addr等填充到src_frame
//然后调用ss_mpi_aidetect_process对src_frame侦测，将结果写入result。根据result保持到./result.txt，并画矩形框保存到gray文件中
//总结：调用ss_mpi_aidetect_process处理1帧图片,画矩形框，并保存framexxx.gray，result.txt，同时printf打印
static td_bool sample_aidetect_proc_one_frame(td_u32 g_index_num, td_char *p_img_data, td_u32 data_img_size, FILE *fd,
    td_phys_addr_t u_phy_addr, td_void *p_vir_addr, ot_aidetect_result_array *result)
{
    td_u32 n_read_size = 0;
    td_s32 ret_sys = TD_SUCCESS;
    ot_aidetect_chn_status ch_status;
    struct timeval start, end;
    ot_video_frame src_frame;
	//[sample_aidetect_proc_one_frame: 716]: frame:5,avg frame rate:78, frame rate:0
	//每处理5帧调用ss_mpi_aidetect_query_status一次并打印ch_status
    if (0 == (g_index_num % OT_SAMPLE_AIDETECT_INDEX_NUM)) {
        (td_void)ss_mpi_aidetect_query_status(OT_SAMPLE_AIDETECT_CHN_0, &ch_status);
       // (td_void)printf("[%s: %d]: frame:%u,avg frame rate:%u, frame rate:%u\n", __func__, __LINE__,
         //   ch_status.recv_frames, ch_status.avg_frame_rate, ch_status.frame_rate);
    }
	
    (td_void)memset_s(p_img_data, data_img_size, 0, data_img_size);
	//读取fd文件的 1个data_img_size字节大小 到p_img_data，并且指针后移至 1个data_img_size字节大小后。
	//这保证了循环读取时，每次读一帧,含YUV数据，往后依次逐帧读取
	//返回值为读取的size大小
    n_read_size = fread(p_img_data, 1, data_img_size, fd);
	//如果读取到的不是完整的 1个data_img_size大小 ，则结束
    if (n_read_size != data_img_size) {
        (td_void)printf("[%s: %d]: read file[%s] over!!\n", __func__, __LINE__, g_src_res_path);
        return TD_FALSE;
    }
	//将p_img_data 中的Y拷贝到src_frame，UV拷贝到src_frame，将u_phy_addr p_vir_addr等数据填充到src_frame
    // make the input frame
    if (!sample_aidetect_make_frame(p_img_data, u_phy_addr, p_vir_addr, &src_frame)) {
        (td_void)printf("[%s: %d]:sample_aidetect_make_frame fail!!\n", __func__, __LINE__);
        return TD_FALSE;
    }
	//调用ss_mpi_aidetect_process将src_frame交给OT_SAMPLE_AIDETECT_CHN_0处理，得到result。同时记录处理时间
    (td_void)gettimeofday(&start, TD_NULL);
	ret_sys = ss_mpi_aidetect_process(OT_SAMPLE_AIDETECT_CHN_0, &src_frame, result);
    (td_void)gettimeofday(&end, TD_NULL);
    if (TD_SUCCESS != ret_sys) {
        (td_void)printf("[%s: %d]: ss_mpi_aidetect_process error:%X!!\n", __func__, __LINE__, ret_sys);
        return TD_FALSE;
    }
	//[sample_aidetect_proc_one_frame:741] frame: 5, ss_mpi_aidetect_process ok and cost time: 13.157000 ms.
	//计算花费时间，并打印
    td_u64 timeuse =
        OT_SAMPLE_AIDETECT_SEC_TIME_CONVERT_NUM * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
  //  (td_void)printf("[%s:%d] frame: %u, ss_mpi_aidetect_process ok and cost time: %f ms.\n", __func__, __LINE__,
   //     g_index_num, timeuse / OT_SAMPLE_AIDETECT_MICROSEC_TIME_CONVERT_NUM);
	//打印aidetect结果result，并保存到./result.txt
    sample_aidetect_save_result(result, g_index_num);
#ifdef SAMPLE_AIDETECT_SHOW_RESULT
	//将检测结果detect_rect画矩形框在gray_data中，并保存文件
    sample_aidetect_show_result(result, g_index_num, p_img_data);
#endif
	//清零输入参数result
    sample_aidetect_result_clear(result);
    return TD_TRUE;
}
//调用ss_mpi_sys_mmz_free释放mmz内存
static td_bool sample_aidetect_free_mmz(td_phys_addr_t phys_addr, td_void *virt_addr)
{
    td_s32 ret_sys = ss_mpi_sys_mmz_free(phys_addr, virt_addr);
    if (TD_SUCCESS != ret_sys) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_mmz_free error: (%#x)!!\n", __func__, __LINE__, ret_sys);
        return TD_FALSE;
    }

    return TD_TRUE;
}
//首先调用ss_mpi_sys_mmz_alloc_cached申请内存，得到 u_phy_addr，p_vir_addr
//然后调用sample_aidetect_result_init初始化result结构体
//然后创建线程执行sample_aidetect_cpu_mem_calc，用以打印cpu used, os mem used, mmz mem used等信息
//最后while(1)循环调用sample_aidetect_proc_one_frame，逐帧处理，打印，保存文件。将处理结果填充到result参数
//总结：循环调用sample_aidetect_proc_one_frame逐帧处理
static td_bool sample_aidetect_video_proc()
{
    td_phys_addr_t u_phy_addr = 0;
    td_void *p_vir_addr = TD_NULL;
    td_char *p_img_data = TD_NULL;
    ot_aidetect_result_array result;
    td_u32 g_index_num = 0;
    td_bool b_ret = TD_TRUE;
    pthread_t g_cpu_mem_th;
	//data_img_size为一张半图片，和make_frame的处理对应
	//调用ss_mpi_sys_mmz_alloc_cached申请data_img_size大小的内存，得到 u_phy_addr，p_vir_addr
    td_u32 data_img_size = g_input_frame_width * g_input_frame_height * 3 / 2;
    td_s32 ret_sys = ss_mpi_sys_mmz_alloc_cached((td_phys_addr_t *)&u_phy_addr, (td_void **)&p_vir_addr, TD_NULL,
        TD_NULL, data_img_size);
    if (TD_SUCCESS != ret_sys) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_mmz_alloc_cached error: (%#x)!!\n", __func__, __LINE__, ret_sys);
        return TD_FALSE;
    }
	//打开g_src_res_path的yuv输入文件
    FILE *fd = fopen(g_src_res_path, "rb");
    if (fd == TD_NULL) {
        (td_void)printf("[%s: %d]: yuv file read fail!!\n", __func__, __LINE__);
        (td_void)sample_aidetect_free_mmz(u_phy_addr, p_vir_addr);
        return TD_FALSE;
    }
	//调用malloc申请data_img_size大小的内存给p_img_data
    p_img_data = (td_char *)malloc(data_img_size);
    if (p_img_data == TD_NULL) {
        (td_void)printf("[%s: %d]: malloc fail!!\n", __func__, __LINE__);
        (td_void)sample_aidetect_free_mmz(u_phy_addr, p_vir_addr);
        (td_void)fclose(fd);
        return TD_FALSE;
    }
	//初始化result并填充+malloc分配内存
    sample_aidetect_result_init(&result);
	//创建线程执行sample_aidetect_cpu_mem_calc，打印cpu used, os mem used, mmz mem used，cnt: 4, average cpu used , average os mem used 等信息
    (td_void)pthread_mutex_init(&g_lock, TD_NULL);
    (td_void)pthread_create(&g_cpu_mem_th, TD_NULL, sample_aidetect_cpu_mem_calc, TD_NULL);
    while (1) {
		//处理1帧图片,画矩形框，并保存framexxx.gray，result.txt，同时printf打印
        if (!sample_aidetect_proc_one_frame(g_index_num, p_img_data, data_img_size, fd, u_phy_addr, p_vir_addr,
            &result)) {
            break;
        }
        ++g_index_num;
    }
	//处理结束，收尾工作。
    (td_void)pthread_mutex_lock(&g_lock);
    g_is_run_finsh = TD_TRUE;
    (td_void)pthread_mutex_unlock(&g_lock);
    (td_void)pthread_join(g_cpu_mem_th, TD_NULL);
    (td_void)pthread_mutex_destroy(&g_lock);
    sample_aidetect_result_free(&result);
    free(p_img_data);
    p_img_data = TD_NULL;
    (td_void)fclose(fd);
    b_ret = sample_aidetect_free_mmz(u_phy_addr, p_vir_addr);
    return b_ret;
}
//初始化aidetect模块
//思路：aidetect while1之前的是初始化，放在dump前面，while1中的内容和放在dump的while1中
int sample_aidetect_init()
{
	td_s32 ret=0;
///////////////////////////以下是 sample_aidetect_main中sample_aidetect_video_proc之前的初始化工作
	
	//[main: 826]: mmz used init size:624 KB
	//获取used=%dKB"中的%d,返回给g_mmz_used_inited_value
    g_mmz_used_inited_value = sample_aidetect_mmz_mem();
    (td_void)printf("[%s: %d]: mmz used init size:%u KB \n", __func__, __LINE__, g_mmz_used_inited_value);
	//打开./output/result.txt
	if (!sample_aidetect_output_path()) {
        (td_void)printf("[%s:%d]sample_aidetect_output_path error", __func__, __LINE__);
        return TD_FALSE;
    }

	//检查 g_src_model_path g_src_res_path g_out_result_path 是否真实存在
    if (!sample_aidetect_check_input_param()) {
        (td_void)printf("[%s: %d]: sample_aidetect_check_input_param error, please check your param!\n", __func__, __LINE__);
        return TD_FAILURE;
    }
	//读取model、ss_mpi_aidetect_create_chn、ss_mpi_aidetect_set_chn_attr，并打印
    if (!sample_aidetect_init_model()) {
        (td_void)printf("[%s: %d]: sample_aidetect_init_model error!\n", __func__, __LINE__);
        return TD_FAILURE;
    }
/////////////////以上是 sample_aidetect_main中sample_aidetect_video_proc之前的初始化工作
//////////////	sample_aidetect_video_proc中 while1之前的部分	//////////////
#if 1	
	//data_img_size为一张半图片，和make_frame的处理对应
	//调用ss_mpi_sys_mmz_alloc_cached申请data_img_size大小的内存，得到 u_phy_addr，p_vir_addr
    td_u32 data_img_size = g_input_frame_width * g_input_frame_height * 3 / 2;
    td_s32 ret_sys = ss_mpi_sys_mmz_alloc_cached((td_phys_addr_t *)&u_phy_addr, (td_void **)&p_vir_addr, TD_NULL,
        TD_NULL, data_img_size);
    if (TD_SUCCESS != ret_sys) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_mmz_alloc_cached error: (%#x)!!\n", __func__, __LINE__, ret_sys);
        return TD_FALSE;
    }
	//打开g_src_res_path的yuv输入文件
    FILE *fd = fopen(g_src_res_path, "rb");
    if (fd == TD_NULL) {
        (td_void)printf("[%s: %d]: yuv file read fail!!\n", __func__, __LINE__);
        (td_void)sample_aidetect_free_mmz(u_phy_addr, p_vir_addr);
        return TD_FALSE;
    }
	//调用malloc申请data_img_size大小的内存给p_img_data
    g_p_img_data = (td_char *)malloc(data_img_size);
    if (g_p_img_data == TD_NULL) {
        (td_void)printf("[%s: %d]: malloc fail!!\n", __func__, __LINE__);
        (td_void)sample_aidetect_free_mmz(u_phy_addr, p_vir_addr);
        (td_void)fclose(fd);
        return TD_FALSE;
    }
	//初始化result并填充+malloc分配内存
    sample_aidetect_result_init(&g_result);
	//创建线程执行sample_aidetect_cpu_mem_calc，打印cpu used, os mem used, mmz mem used，cnt: 4, average cpu used , average os mem used 等信息
    (td_void)pthread_mutex_init(&g_lock, TD_NULL);
    (td_void)pthread_create(&g_cpu_mem_th, TD_NULL, sample_aidetect_cpu_mem_calc, TD_NULL);
#endif
//////////////	sample_aidetect_video_proc while1之前的部分	//////////////
	return TD_SUCCESS;
}
int sample_aidetect_deinit()
{
/////////////////////////////以下是sample_aidetect_video_proc中while1之后的收尾工作
	//处理结束，收尾工作。
    (td_void)pthread_mutex_lock(&g_lock);
    g_is_run_finsh = TD_TRUE;
    (td_void)pthread_mutex_unlock(&g_lock);
    (td_void)pthread_join(g_cpu_mem_th, TD_NULL);
    (td_void)pthread_mutex_destroy(&g_lock);
    sample_aidetect_result_free(&g_result);
    free(g_p_img_data);
    g_p_img_data = TD_NULL;
    //(td_void)fclose(fd);
   td_s32  b_ret = sample_aidetect_free_mmz(u_phy_addr, p_vir_addr);
//////////////////////////////////
//以下是	sample_aidetect_main中的收尾工作
	td_s32 ret=0;
	//处理结束，销毁OT_SAMPLE_AIDETECT_CHN_0释放资源
    ret = ss_mpi_aidetect_destroy_chn(OT_SAMPLE_AIDETECT_CHN_0);
    if (ret != TD_SUCCESS) {
        (td_void)printf("[%s: %d]: ss_mpi_aidetect_destroy_chn error: (%#x)!!\n", __func__, __LINE__, ret);
        return ret;
    }
	//关闭fd_out_file即./result.txt
    if (fd_out_file != TD_NULL) {
        (td_void)fclose(fd_out_file);
        fd_out_file = TD_NULL;
    }
/*不需要退出 swann
	//退出 mpi_sys
    ret = ss_mpi_sys_exit();
    if (ret != TD_SUCCESS) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_exit error: (%#x)!!\n", __func__, __LINE__, ret);
    }
*/
	return ret;
}

int sample_aidetect_main()
{
	td_s32 ret=0;
/*	不需要初始化 swann
	//初始化 mpi_sys
    td_s32 ret = ss_mpi_sys_init();
    if (ret != TD_SUCCESS) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_init error: (%#x)!!\n", __func__, __LINE__, ret);
        return ret;
    }
*/
	//[main: 826]: mmz used init size:624 KB
	//获取used=%dKB"中的%d,返回给g_mmz_used_inited_value
    g_mmz_used_inited_value = sample_aidetect_mmz_mem();
    (td_void)printf("[%s: %d]: mmz used init size:%u KB \n", __func__, __LINE__, g_mmz_used_inited_value);
	//打开./output/result.txt
	if (!sample_aidetect_output_path()) {
        (td_void)printf("[%s:%d]sample_aidetect_output_path error", __func__, __LINE__);
        return TD_FALSE;
    }
/*	不需要解析 swann
	//根据输入参数argc argv,配置g_src_res_path g_src_model_path g_out_result_path信息
    if (!sample_aidetect_parase_param(argc, argv)) {
        (td_void)printf("[%s: %d]: sample_aidetect_parase_param error , please check your param!\n", __func__, __LINE__);
        return TD_FAILURE;
    }
*/
	//检查 g_src_model_path g_src_res_path g_out_result_path 是否真实存在
    if (!sample_aidetect_check_input_param()) {
        (td_void)printf("[%s: %d]: sample_aidetect_check_input_param error, please check your param!\n", __func__, __LINE__);
        return TD_FAILURE;
    }
	//读取model、ss_mpi_aidetect_create_chn、ss_mpi_aidetect_set_chn_attr，并打印
    if (!sample_aidetect_init_model()) {
        (td_void)printf("[%s: %d]: sample_aidetect_init_model error!\n", __func__, __LINE__);
        return TD_FAILURE;
    }
	//内部while(1)循环调用sample_aidetect_proc_one_frame逐帧处理,打印结果，保存文件。将处理结果填充到result参数
    // video and image are supported by sample，when intput image , please convert the image to NV21
    if (!sample_aidetect_video_proc()) {
        (td_void)printf("[%s: %d]: sample_aidetect_video_proc error!\n", __func__, __LINE__);
        return TD_FAILURE;
    }
	//处理结束，销毁OT_SAMPLE_AIDETECT_CHN_0释放资源
    ret = ss_mpi_aidetect_destroy_chn(OT_SAMPLE_AIDETECT_CHN_0);
    if (ret != TD_SUCCESS) {
        (td_void)printf("[%s: %d]: ss_mpi_aidetect_destroy_chn error: (%#x)!!\n", __func__, __LINE__, ret);
        return ret;
    }
	//关闭fd_out_file即./result.txt
    if (fd_out_file != TD_NULL) {
        (td_void)fclose(fd_out_file);
        fd_out_file = TD_NULL;
    }
/*不需要退出 swann
	//退出 mpi_sys
    ret = ss_mpi_sys_exit();
    if (ret != TD_SUCCESS) {
        (td_void)printf("[%s: %d]: ss_mpi_sys_exit error: (%#x)!!\n", __func__, __LINE__, ret);
    }
*/
    return ret;
}

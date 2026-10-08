/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */
#include "sample_svp_svc_process.h"
#include <pthread.h>
#include <sys/prctl.h>
#include <unistd.h>
#include "svp_acl_rt.h"
#include "svp_acl.h"
#include "svp_acl_ext.h"
#include "sample_common_svp.h"
#include "sample_common_svp_npu.h"
#include "sample_common_svp_npu_model.h"

#define SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM 1
#define SAMPLE_SVP_NPU_LSTM_INPUT_FILE_NUM     2
#define SAMPLE_SVP_NPU_SHAERD_WORK_BUF_NUM     1
#define SAMPLE_SVP_NPU_YOLO_TYPE_NUM           12
#define SAMPLE_SVP_NPU_YOLO_THRESHOLD_NUM      1
#define SAMPLE_SVP_NPU_PREEMPTION_SLEEP_SECOND    30
#define SAMPLE_SVP_NPU_PATH_LEN 0x100

#define SVP_NPU_VO_WIDTH    1920
#define SVP_NPU_VO_HEIGHT   1080
#define ENTER_ASCII 10


static hi_vb_pool_info g_svp_npu_vb_pool_info;
static hi_void *g_svp_npu_vb_virt_addr = HI_NULL;
static hi_sample_svp_coords_info g_svp_npu_coords_info = {0};
static hi_bool g_svp_npu_is_thread_start = HI_FALSE;
static hi_bool g_svp_npu_terminate_signal = HI_FALSE;
static sample_svp_npu_shared_work_buf g_svp_npu_shared_work_buf[SAMPLE_SVP_NPU_SHAERD_WORK_BUF_NUM] = {0};
static pthread_t g_svp_npu_thread = 0;
static sample_svp_npu_task_info g_svp_npu_task[SAMPLE_SVP_NPU_MAX_TASK_NUM] = {0};

static sample_svp_npu_threshold g_svp_npu_yolo_threshold[SAMPLE_SVP_NPU_YOLO_TYPE_NUM] = {
    {0.0, 0.0, 0.0, 0.0, "rpn_data"},  // reserve
    {0.5, 0.2, 1.0, 1.0, "rpn_data"},  // yolov1
    {0.3, 0.5, 1.0, 1.0, "rpn_data"},  // yolov2
    {0.45, 0.5, 1.0, 1.0, "rpn_data"}, // yolov3
    {0.45, 0.5, 1.0, 1.0, "rpn_data"}, // yolov4
    {0.8, 0.8, 1.0, 1.0, "rpn_data"}, // yolov5
    {0.0, 0.0, 0.0, 0.0, "rpn_data"},  // reserve
    {0.45, 0.5, 1.0, 1.0, "rpn_data"}, // yolov7
    {0.8, 0.15, 1.0, 1.0, "rpn_data"},  // yolov8
    {0.0, 0.0, 0.0, 0.0, "rpn_data"}   // reserve
    };

static sample_svp_npu_roi_info g_svp_npu_yolo_roi_info[SAMPLE_SVP_NPU_YOLO_TYPE_NUM] = {
    {"reserve", "reserve"},  // reserve
    {"yolov1_nms", "yolov1_nms_"},  // yolov1
    {"DetectionOut0_nms_3", "DetectionOut0_nms_3_"},  // yolov2
    {"detection_nms_2", "detection_nms_2_"},  // yolov3
    {"detection_nms_2", "detection_nms_2_"},  // yolov4
    {"output0_0", "output0_"},  // yolov5
    {"reserve", "reserve"},  // reserve
    {"output", "output_"},  // yolov7
    {"output0_", "output0_0"},  // yolov8
    {"reserve", "reserve"},  // reserve
    };

static hi_s32 g_svp_npu_dev_id = 0;
static sample_vi_cfg g_vi_config;
static hi_sample_svp_rect_info g_svp_npu_rect_info = {0};
static hi_bool g_svp_npu_thread_stop = HI_FALSE;

static hi_sample_svp_media_cfg g_svp_npu_media_cfg = {
    .svp_switch = {HI_TRUE},
    .pic_type = {PIC_1080P, PIC_CIF},
    .chn_num = HI_SVP_MAX_VPSS_CHN_NUM,
};
static hi_venc_svc_roi_type g_svc_mode;


hi_sample_svp_media_cfg *sample_svp_npu_get_media_cfg(hi_void)
{
    return &g_svp_npu_media_cfg;
}

hi_s32 *sample_svp_npu_get_device_id(hi_void)
{
    return &g_svp_npu_dev_id;
}

hi_sample_svp_rect_info *sample_svp_npu_get_svp_rect_info(hi_void)
{
    return &g_svp_npu_rect_info;
}

hi_bool *sample_svp_npu_get_thread_stop(hi_void)
{
    return &g_svp_npu_thread_stop;
}

sample_svp_npu_task_info *sample_svp_npu_get_task_info(int task_idx)
{
    return &g_svp_npu_task[task_idx];
}

static hi_void sample_svp_npu_acl_terminate(hi_void)
{
    if (g_svp_npu_terminate_signal == HI_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
}

/* function : svp npu signal handle */
hi_void sample_svp_npu_acl_handle_sig(hi_void)
{
    g_svp_npu_terminate_signal = HI_TRUE;
}

static hi_void sample_svp_npu_acl_deinit(hi_void)
{
    svp_acl_error ret;

    ret = svp_acl_rt_reset_device(g_svp_npu_dev_id);
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("reset device fail\n");
    }
    sample_svp_trace_info("end to reset device is %d\n", g_svp_npu_dev_id);

    ret = svp_acl_finalize();
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("finalize acl fail\n");
    }
    sample_svp_trace_info("end to finalize acl\n");
    (hi_void)sample_common_svp_check_sys_exit();
}

static hi_s32 sample_svp_npu_acl_init(const hi_char *acl_config_path)
{
    /* svp acl init */
    svp_acl_rt_run_mode run_mode;
    svp_acl_error ret;
    hi_bool is_mpi_init;

    is_mpi_init = sample_common_svp_check_sys_init();
    sample_svp_check_exps_return(is_mpi_init != HI_TRUE, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "mpi init failed!\n");

    ret = svp_acl_init(acl_config_path);
    sample_svp_check_exps_return(ret != SVP_ACL_SUCCESS, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "acl init failed!\n");

    sample_svp_trace_info("svp acl init success!\n");

    /* open device */
    ret = svp_acl_rt_set_device(g_svp_npu_dev_id);
    if (ret != SVP_ACL_SUCCESS) {
        (hi_void)svp_acl_finalize();
        sample_svp_trace_err("svp acl open device %d failed!\n", g_svp_npu_dev_id);
        return HI_FAILURE;
    }
    sample_svp_trace_info("open device %d success!\n", g_svp_npu_dev_id);

    /* get run mode */
    ret = svp_acl_rt_get_run_mode(&run_mode);
    if ((ret != SVP_ACL_SUCCESS) || (run_mode != SVP_ACL_DEVICE)) {
        (hi_void)svp_acl_rt_reset_device(g_svp_npu_dev_id);
        (hi_void)svp_acl_finalize();
        sample_svp_trace_err("acl get run mode failed!\n");
        return HI_FAILURE;
    }
    sample_svp_trace_info("get run mode success!\n");

    return HI_SUCCESS;
}

static hi_s32 sample_svp_npu_acl_dataset_init(hi_u32 task_idx)
{
    hi_s32 ret = sample_common_svp_npu_create_input(&g_svp_npu_task[task_idx]);
    sample_svp_check_exps_return(ret != HI_SUCCESS, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR, "create input failed!\n");

    ret = sample_common_svp_npu_create_output(&g_svp_npu_task[task_idx]);
    if (ret != HI_SUCCESS) {
        sample_common_svp_npu_destroy_input(&g_svp_npu_task[task_idx]);
        sample_svp_trace_err("execute create output fail.\n");
        return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_void sample_svp_npu_acl_dataset_deinit(hi_u32 task_idx)
{
    (hi_void)sample_common_svp_npu_destroy_input(&g_svp_npu_task[task_idx]);
    (hi_void)sample_common_svp_npu_destroy_output(&g_svp_npu_task[task_idx]);
}

static hi_void *sample_svp_npu_acl_thread_multi_model_process(hi_void *args)
{
    hi_s32 ret;
    hi_u32 task_idx = *(hi_u32 *)args;
    hi_u32 proc_cnt = 0;
    svp_acl_rt_context context = {0};

    ret = svp_acl_rt_create_context(&context, g_svp_npu_dev_id);
    sample_svp_check_exps_return(ret != HI_SUCCESS, HI_NULL, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "create context failed!\n");

    ret = svp_acl_rt_set_current_context(context);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "set context failed!\n");

    while (g_svp_npu_is_thread_start == HI_TRUE && g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[task_idx]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR, "proc sync failed!\n");
        sample_svp_trace_info("model_index:%u complete cnt:%u\n", g_svp_npu_task[task_idx].cfg.model_idx, ++proc_cnt);
    }

process_end0:
    ret = svp_acl_rt_destroy_context(context);
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("task[%u] destroy context failed!\n", task_idx);
    }
    return HI_NULL;
}

static hi_void *sample_svp_npu_acl_thread_execute(hi_void *args)
{
    hi_s32 ret;
    hi_u32 task_idx = *(hi_u32 *)args;

    ret = svp_acl_rt_set_device(g_svp_npu_dev_id);
    sample_svp_check_exps_return(ret != HI_SUCCESS, HI_NULL, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "open device %d failed!\n", g_svp_npu_dev_id);

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[task_idx]);
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("execute inference failed of task[%u]!\n", task_idx);
    }

    ret = svp_acl_rt_reset_device(g_svp_npu_dev_id);
    if (ret != SVP_ACL_SUCCESS) {
        sample_svp_trace_err("task[%u] reset device failed!\n", task_idx);
    }
    return HI_NULL;
}

static hi_void sample_svp_npu_acl_model_execute_multithread(hi_void)
{
    pthread_t execute_threads[SAMPLE_SVP_NPU_MAX_THREAD_NUM] = {0};
    hi_u32 idx[SAMPLE_SVP_NPU_MAX_THREAD_NUM] = {0};
    hi_u32 task_idx;

    for (task_idx = 0; task_idx < SAMPLE_SVP_NPU_MAX_THREAD_NUM; task_idx++) {
        idx[task_idx] = task_idx;
        pthread_create(&execute_threads[task_idx], NULL, sample_svp_npu_acl_thread_execute, &idx[task_idx]);
    }

    hi_void *waitret[SAMPLE_SVP_NPU_MAX_THREAD_NUM];
    for (task_idx = 0; task_idx < SAMPLE_SVP_NPU_MAX_THREAD_NUM; task_idx++) {
        pthread_join(execute_threads[task_idx], &waitret[task_idx]);
    }

    for (task_idx = 0; task_idx < SAMPLE_SVP_NPU_MAX_THREAD_NUM; task_idx++) {
        sample_svp_trace_info("output %u-th task data\n", task_idx);
        sample_common_svp_npu_output_classification_result(&g_svp_npu_task[task_idx]);
    }
}

static hi_void sample_svp_npu_acl_deinit_task(hi_u32 task_num, hi_u32 shared_work_buf_idx)
{
    hi_u32 task_idx;

    for (task_idx = 0; task_idx < task_num; task_idx++) {
        (hi_void)sample_common_svp_npu_destroy_work_buf(&g_svp_npu_task[task_idx]);
        (hi_void)sample_common_svp_npu_destroy_task_buf(&g_svp_npu_task[task_idx]);
        (hi_void)sample_svp_npu_acl_dataset_deinit(task_idx);
        (hi_void)memset_s(&g_svp_npu_task[task_idx], sizeof(sample_svp_npu_task_cfg), 0,
            sizeof(sample_svp_npu_task_cfg));
    }
    if (g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_ptr != HI_NULL) {
        (hi_void)svp_acl_rt_free(g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_ptr);
        g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_ptr = HI_NULL;
        g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_size = 0;
        g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_stride = 0;
    }
}

static hi_s32 sample_svp_npu_acl_create_shared_work_buf(hi_u32 task_num, hi_u32 shared_work_buf_idx)
{
    hi_u32 task_idx, work_buf_size, work_buf_stride;
    hi_s32 ret;

    for (task_idx = 0; task_idx < task_num; task_idx++) {
        ret = sample_common_svp_npu_get_work_buf_info(&g_svp_npu_task[task_idx], &work_buf_size, &work_buf_stride);
        sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "get %u-th task work buf info failed!\n", task_idx);

        if (g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_size < work_buf_size) {
            g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_size = work_buf_size;
            g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_stride = work_buf_stride;
        }
    }
    ret = svp_acl_rt_malloc_cached(&g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_ptr,
        g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_size, SVP_ACL_MEM_MALLOC_NORMAL_ONLY);
    sample_svp_check_exps_return(ret != HI_SUCCESS, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "malloc %u-th shared work buf failed!\n", shared_work_buf_idx);

    (hi_void)svp_acl_rt_mem_flush(g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_ptr,
        g_svp_npu_shared_work_buf[shared_work_buf_idx].work_buf_size);
    return HI_SUCCESS;
}

static hi_s32 sample_svp_npu_acl_init_task(hi_u32 task_num, hi_bool is_share_work_buf,
    hi_u32 shared_work_buf_idx)
{
    hi_u32 task_idx;
    hi_s32 ret;

    if (is_share_work_buf == HI_TRUE) {
        ret = sample_svp_npu_acl_create_shared_work_buf(task_num, shared_work_buf_idx);
        sample_svp_check_exps_return(ret != HI_SUCCESS, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "create shared work buf failed!\n");
    }

    for (task_idx = 0; task_idx < task_num; task_idx++) {
        ret = sample_svp_npu_acl_dataset_init(task_idx);
        if (ret != HI_SUCCESS) {
            goto task_init_end_0;
        }
        ret = sample_common_svp_npu_create_task_buf(&g_svp_npu_task[task_idx]);
        if (ret != HI_SUCCESS) {
            sample_svp_trace_err("create task buf failed.\n");
            goto task_init_end_0;
        }
        if (is_share_work_buf == HI_FALSE) {
            ret = sample_common_svp_npu_create_work_buf(&g_svp_npu_task[task_idx]);
        } else {
            /* if all tasks are on the same stream, work buf can be shared */
            ret = sample_common_svp_npu_share_work_buf(&g_svp_npu_shared_work_buf[shared_work_buf_idx],
                &g_svp_npu_task[task_idx]);
        }
        if (ret != HI_SUCCESS) {
            sample_svp_trace_err("create work buf failed.\n");
            goto task_init_end_0;
        }
    }
    return HI_SUCCESS;

task_init_end_0:
    (hi_void)sample_svp_npu_acl_deinit_task(task_num, shared_work_buf_idx);
    return ret;
}

static hi_s32 sample_svp_npu_acl_load_multi_model(const hi_char *model_path,
    hi_u32 model_num, hi_bool is_with_config, const hi_char *src_file[], hi_u32 file_num)
{
    hi_u32 i, j;
    hi_s32 ret;

    for (i = 0; i < model_num; i++) {
        if (is_with_config == HI_TRUE) {
            ret = sample_common_svp_npu_load_model_with_config(model_path, i, HI_FALSE);
        } else {
            ret = sample_common_svp_npu_load_model(model_path, i, HI_FALSE);
        }
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "load model failed!\n");

        /* set task cfg */
        g_svp_npu_task[i].cfg.max_batch_num = 1;
        g_svp_npu_task[i].cfg.dynamic_batch_num = 1;
        g_svp_npu_task[i].cfg.total_t = 0;
        g_svp_npu_task[i].cfg.is_cached = HI_FALSE;
        g_svp_npu_task[i].cfg.model_idx = i;
    }

    ret = sample_svp_npu_acl_init_task(model_num, HI_FALSE, 0);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR, "init task failed!\n");

    for (i = 0; i < model_num; i++) {
        ret = sample_common_svp_npu_get_input_data(src_file, file_num, &g_svp_npu_task[i]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR, "get data failed!\n");
    }

    return HI_SUCCESS;

process_end1:
    (hi_void)sample_svp_npu_acl_deinit_task(model_num, 0);

process_end0:
    for (j = 0; j < i; j++) {
        (hi_void)sample_common_svp_npu_unload_model(j);
    }

    return ret;
}

static hi_void sample_svp_npu_acl_unload_multi_model(hi_u32 model_num)
{
    hi_u32 i;

    (hi_void)sample_svp_npu_acl_deinit_task(model_num, 0);

    for (i = 0; i < model_num; i++) {
        (hi_void)sample_common_svp_npu_unload_model(i);
    }
}
static hi_s32 sample_svp_svc_set_rect(const hi_video_frame_info *frame_info, hi_sample_svp_rect_info *rect_info,
    hi_venc_chn venc_chn)
{
    hi_s32 ret;
    hi_s32 m = 0;
    hi_u32 width, height;
    hi_venc_svc_rect_info rect;
    hi_venc_svc_param_ex svc_param;
    hi_mpi_venc_enable_svc(venc_chn, 1);

    rect.rect_num = rect_info->num;
    rect.base_resolution.width = frame_info->video_frame.width;
    rect.base_resolution.height = frame_info->video_frame.height;
    for (m = 0; m < rect_info->num; m++) {
        width = (hi_u32)(rect_info->rect[m].point[1].x - rect_info->rect[m].point[0].x);
        height = (hi_u32)(rect_info->rect[m].point[2].y - rect_info->rect[m].point[0].y); /* 2: algn */
        rect.rect_attr[m].x = rect_info->rect[m].point[0].x;
        rect.rect_attr[m].y = rect_info->rect[m].point[0].y;
        rect.rect_attr[m].width = width;
        rect.rect_attr[m].height = height;
        rect.detect_type[m] = 0;
    }
    rect.pts = frame_info->video_frame.pts;
    ret = hi_mpi_venc_send_svc_region(venc_chn, &rect);
    sample_svp_check_exps_return(ret != HI_SUCCESS,  ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),hi_mpi_venc_send_svc_region failed!\n", ret);

    ret = hi_mpi_venc_get_svc_param_ex(venc_chn, &svc_param);
    sample_svp_check_exps_return(ret != HI_SUCCESS,  ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),hi_mpi_venc_get_svc_param_ex failed!\n", ret);

    svc_param.svc_version = HI_VENC_SVC_V2;
    svc_param.svc_param_v2.max_ref_num = 2; /* 2: algn */
    svc_param.svc_param_v2.refresh_interval = 2; /* 2: algn */
    for (m = 0; m < 16; m++) {  /* 16: algn */
        svc_param.svc_param_v2.qp_delta[m] = 0;
    }
    svc_param.svc_param_v2.qp_delta[0] = 10; /* 10: algn */
    svc_param.svc_param_v2.qp_delta[1] = -10; /* -10: algn */
    svc_param.svc_param_v2.qp_delta[15] = 1; /* 15: algn */
    hi_mpi_venc_set_svc_param_ex(venc_chn, &svc_param);
    sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),hi_mpi_venc_set_svc_param_ex failed!\n", ret);
    return HI_SUCCESS;
}

static hi_s32 test_send_mask(hi_venc_chn chn, hi_sample_svp_rect_info *rect_info, td_u64 pts,
    hi_u32 base_width, hi_u32 base_height)
{
    hi_vb_blk blk;
    hi_phys_addr_t phys_addr;
    hi_u8 *virt_addr;
    hi_u8 *temp_virt;
    hi_u32 size = 640 * 480 / 2; /* 640, 480, 2: algn */
    int i, j;
    td_s32 ret;
    hi_s32 m, top_x, top_y, width, height, top_x_scale, top_y_scale, width_scale, height_scale;
    hi_venc_svc_mask_info  svc_mask;

    hi_venc_get_svc_mask_size(640, 480); /* 640, 480: algn */
    if (size < hi_venc_get_svc_mask_size(640, 480)) { /* 640, 480: algn */
        sample_svp_check_exps_return(1, 0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),hi_venc_get_svc_mask_size failed!\n", 0);
        return HI_SUCCESS;
    }
    blk = hi_mpi_vb_get_blk(g_svp_npu_media_cfg.mask_vb_pool, size, HI_NULL);
    if (blk == HI_VB_INVALID_HANDLE) {
        sample_svp_check_exps_return(blk == HI_VB_INVALID_HANDLE, 0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),hi_mpi_vb_get_blk failed!\n", 0);
        return HI_SUCCESS;
    }

    phys_addr = hi_mpi_vb_handle_to_phys_addr(blk);
    virt_addr = (hi_u8 *)hi_mpi_sys_mmap(phys_addr, size);
    if (virt_addr == HI_NULL) {
        sample_svp_check_exps_return(1, 0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),hi_mpi_sys_mmap failed!\n", 0);
        hi_mpi_vb_release_blk(blk);
    }
    (hi_void)memset_s(virt_addr, size, 0xff, size);

    for (m = 0; m < rect_info->num; m++) {
        width_scale = rect_info->rect[m].point[1].x - rect_info->rect[m].point[0].x;
        height_scale = rect_info->rect[m].point[2].y - rect_info->rect[m].point[0].y; /* 2: algn */
        top_y_scale = rect_info->rect[m].point[0].y;
        top_x_scale = rect_info->rect[m].point[0].x;
        width = (hi_s32)(((double)width_scale / (double)base_width) * (double)640); /* 640: algn */
        height = (hi_s32)(((double)height_scale /(double) base_height) *(double) 480); /* 480: algn */
        top_x = (hi_s32)(((double)top_x_scale / (double)base_width) *(double) 640); /* 640: algn */
        top_y = (hi_s32)(((double)top_y_scale / (double)base_height) * (double)480); /* 480: algn */
        for (j = top_y; j < top_y + height; j++) {
            for (i = top_x; i < top_x + width; i++) {
                temp_virt = virt_addr + (j * 640 + i) / 2; /* 640, 2: algn */
                *temp_virt = 0;
            }
        }
    }
    svc_mask.base_resolution.width = 640; /* 640: algn */
    svc_mask.base_resolution.height = 480; /* 480: algn */
    svc_mask.mask_blk = blk;
    svc_mask.pts = pts;

    ret = hi_mpi_venc_send_svc_mask(chn, &svc_mask);
    sample_svp_check_exps_trace(ret != HI_SUCCESS,  ret,
        "Error(%#x),hi_mpi_venc_send_svc_mask failed!\n", ret);
    hi_mpi_sys_munmap(virt_addr, size);
    hi_mpi_vb_release_blk(blk);
    return ret;
}

static hi_s32 sample_svp_svc_set_mask(const hi_video_frame_info *frame_info, hi_sample_svp_rect_info *rect_info,
    hi_venc_chn venc_chn)
{
    hi_s32 ret;
    hi_s32 m = 0;
    hi_venc_svc_param_ex svc_param;
    hi_u32 width = frame_info->video_frame.width;
    hi_u32 height = frame_info->video_frame.height;
    hi_mpi_venc_enable_svc(venc_chn, 1);

    ret = hi_mpi_venc_get_svc_param_ex(venc_chn, &svc_param);
    sample_svp_check_exps_return(ret != HI_SUCCESS,  ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),hi_mpi_venc_get_svc_param_ex failed!\n", ret);

    svc_param.svc_version = HI_VENC_SVC_V2;
    svc_param.svc_param_v2.max_ref_num = 2; /* 2: algn */
    svc_param.svc_param_v2.refresh_interval = 2; /* 2: algn */
    svc_param.svc_param_v2.roi_type = HI_VENC_SVC_ROI_TYPE_MASK;
    for (m = 0; m < 16; m++) {  /* 16: algn */
        svc_param.svc_param_v2.qp_delta[m] = 0;
    }
    svc_param.svc_param_v2.qp_delta[0] = 15; /* 15: algn */
    svc_param.svc_param_v2.qp_delta[1] = -10; /* -10: algn */
    svc_param.svc_param_v2.qp_delta[15] = 1; /* -15: algn */
    hi_mpi_venc_set_svc_param_ex(venc_chn, &svc_param);
    sample_svp_check_exps_return(ret != HI_SUCCESS,  ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),hi_mpi_venc_set_svc_param_ex failed!\n", ret);

    ret = test_send_mask(venc_chn, rect_info, frame_info->video_frame.pts, width, height);
    sample_svp_check_exps_return(ret != HI_SUCCESS,  ret, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),hi_mpi_venc_get_svc_param_ex failed!\n", ret);

    return TD_SUCCESS;
}


static hi_s32 sample_svp_npu_acl_frame_proc(const hi_video_frame_info *ext_frame,
    const hi_video_frame_info *base_frame, hi_void *args)
{
    hi_s32 ret;
    hi_void *virt_addr = HI_NULL;
    sample_svp_npu_detection_info *detection_info = HI_NULL;
    sample_svp_npu_thread_args *thread_args = (sample_svp_npu_thread_args *)args;
    hi_u32 size = (hi_u32)(ext_frame->video_frame.height * ext_frame->video_frame.stride[0] *
        SAMPLE_SVP_NPU_IMG_THREE_CHN / SAMPLE_SVP_NPU_DOUBLE);

    virt_addr = g_svp_npu_vb_virt_addr +
        (ext_frame->video_frame.phys_addr[0] - g_svp_npu_vb_pool_info.pool_phy_addr);
    ret = sample_common_svp_npu_update_input_data_buffer_info(virt_addr, size,
        ext_frame->video_frame.stride[0], 0, &g_svp_npu_task[0]);
    sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR, "update data buffer failed!\n");

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
    sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR, "model execute failed!\n");

    if (thread_args->model_name == SAMPLE_SVP_NPU_RFCN || thread_args->model_name == SAMPLE_SVP_NPU_YOLO) {
        detection_info = thread_args->detection_info;
        ret = sample_common_svp_npu_roi_to_rect(&g_svp_npu_task[0], detection_info, ext_frame, base_frame,
            &g_svp_npu_rect_info);
        sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR, "roi to rect failed!\n");
    } else if (thread_args->model_name == SAMPLE_SVP_NPU_HRNET) {
        ret = sample_common_svp_npu_get_joint_coords(&g_svp_npu_task[0], ext_frame, base_frame, &g_svp_npu_coords_info);
        sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR, "roi to joint failed!\n");

        ret = sample_common_svp_vgs_fill_joint_coords_lines(base_frame, &g_svp_npu_coords_info);
        sample_svp_check_exps_return(ret != HI_SUCCESS, ret, SAMPLE_SVP_ERR_LEVEL_ERROR, "vgs fill line failed!\n");
    }
    return ret;
}

static hi_s32 sample_svp_npu_acl_vb_map(hi_u32 vb_pool_idx)
{
    hi_s32 ret;

    if (g_svp_npu_vb_virt_addr == HI_NULL) {
        ret = hi_mpi_vb_get_pool_info(g_svp_npu_media_cfg.vb_pool[vb_pool_idx], &g_svp_npu_vb_pool_info);
        sample_svp_check_exps_return(ret != HI_SUCCESS, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "get pool info failed!\n");
        g_svp_npu_vb_virt_addr = hi_mpi_sys_mmap(g_svp_npu_vb_pool_info.pool_phy_addr,
            g_svp_npu_vb_pool_info.pool_size);
        sample_svp_check_exps_return(g_svp_npu_vb_virt_addr == HI_NULL, HI_FAILURE, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "map vb pool failed!\n");
    }
    return HI_SUCCESS;
}

static hi_void *sample_svp_npu_acl_to_venc(hi_void *args)
{
    hi_s32 ret;
    hi_video_frame_info base_frame, ext_frame;
    const hi_s32 milli_sec = SAMPLE_SVP_NPU_MILLIC_SEC;
    const hi_s32 vpss_grp = 0;
    hi_s32 vpss_chn[] = { HI_VPSS_CHN0, HI_VPSS_CHN1 };
    hi_u32 size, stride;
    hi_u8 *data = HI_NULL;
    hi_s32 frame_index = 0;

    (hi_void)prctl(PR_SET_NAME, "svp_npu_to_venc", 0, 0, 0);

    ret = svp_acl_rt_set_device(g_svp_npu_dev_id);
    sample_svp_check_exps_return(ret != HI_SUCCESS, HI_NULL, SAMPLE_SVP_ERR_LEVEL_ERROR, "open device failed!\n");

    ret = sample_svp_npu_acl_vb_map(HI_VPSS_CHN1);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, fail_0, SAMPLE_SVP_ERR_LEVEL_ERROR, "map vb pool failed!\n");

    ret = sample_common_svp_npu_get_input_data_buffer_info(&g_svp_npu_task[0], 0, &data, &size, &stride);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, fail_1, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "Error(%#x),get_input_data_buffer_info failed!\n", ret);

    while (g_svp_npu_thread_stop == HI_FALSE) {
        frame_index++;

        ret = hi_mpi_vpss_get_chn_frame(vpss_grp, vpss_chn[1], &ext_frame, milli_sec);
        sample_svp_check_exps_continue(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),hi_mpi_vpss_get_chn_frame failed, vpss_grp(%d), vpss_chn(%d)!\n", ret, vpss_grp, vpss_chn[1]);

        ret = hi_mpi_vpss_get_chn_frame(vpss_grp, vpss_chn[0], &base_frame, milli_sec);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, ext_release, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),hi_mpi_vpss_get_chn_frame failed, VPSS_GRP(%d), VPSS_CHN(%d)!\n", ret, vpss_grp, vpss_chn[0]);

        ret = sample_svp_npu_acl_frame_proc(&ext_frame, &base_frame, args);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, base_release, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),sample_svp_npu_acl_frame_proc failed!\n", ret);

        ret = sample_common_svp_venc_send_stream(&g_svp_npu_media_cfg.svp_switch, 0, &base_frame);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, base_release, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),sample_common_svp_venc_send_stream failed!\n", ret);
        /* svc get rect -> venc */
        if (ext_frame.video_frame.frame_flag & HI_FRAME_FLAG_SVC_FLAG) {
            if (g_svc_mode == 0) {
            ret = sample_svp_svc_set_rect(&base_frame, &g_svp_npu_rect_info, 0);
            sample_svp_check_exps_goto(ret != HI_SUCCESS, base_release, SAMPLE_SVP_ERR_LEVEL_ERROR,
                "Error(%#x),sample_svp_svc_set_rect failed!\n", ret);
            } else if (g_svc_mode == 1) {
            ret = sample_svp_svc_set_mask(&base_frame, &g_svp_npu_rect_info, 0);
            sample_svp_check_exps_goto(ret != HI_SUCCESS, base_release, SAMPLE_SVP_ERR_LEVEL_ERROR,
                "Error(%#x),sample_svp_svc_set_mask failed!\n", ret);
            }
        }

base_release:
        ret = hi_mpi_vpss_release_chn_frame(vpss_grp, vpss_chn[0], &base_frame);
        sample_svp_check_exps_trace(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),release_frame failed,grp(%d) chn(%d)!\n", ret, vpss_grp, vpss_chn[0]);
ext_release:
        ret = hi_mpi_vpss_release_chn_frame(vpss_grp, vpss_chn[1], &ext_frame);
        sample_svp_check_exps_trace(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "Error(%#x),release_frame failed,grp(%d) chn(%d)!\n", ret, vpss_grp, vpss_chn[1]);
    }
    ret = sample_common_svp_npu_update_input_data_buffer_info(data, size, stride, 0, &g_svp_npu_task[0]);
    sample_svp_check_exps_trace(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "update buffer failed!\n");
fail_1:
    (hi_void)hi_mpi_sys_munmap(g_svp_npu_vb_virt_addr, g_svp_npu_vb_pool_info.pool_size);
fail_0:
    (hi_void)svp_acl_rt_reset_device(g_svp_npu_dev_id);
    return HI_NULL;
}

static hi_void sample_svp_npu_acl_pause(hi_void)
{
    printf("---------------press Enter key to exit!---------------\n");
    if (g_svp_npu_terminate_signal == HI_TRUE) {
        return;
    }
    (hi_void)getchar();
    if (g_svp_npu_terminate_signal == HI_TRUE) {
        return;
    }
}

/* function : show the sample of svp npu resnet50 */
hi_void sample_svp_npu_acl_resnet50(hi_void)
{
    hi_s32 ret;
    const hi_u32 model_idx = 0;
    const hi_char *acl_config_path = "";
    const hi_char *src_file[SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM] = {"./data/image/3_224_224_batch_1.bgr"};
    const hi_char *om_model_path = "./data/model/resnet50.om";

    g_svp_npu_terminate_signal = HI_FALSE;

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        /* init acl */
        ret = sample_svp_npu_acl_init(acl_config_path);
        sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

        /* load model */
        ret = sample_common_svp_npu_load_model(om_model_path, model_idx, HI_FALSE);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "load model failed!\n");

        /* set task cfg */
        g_svp_npu_task[0].cfg.max_batch_num = 1;
        g_svp_npu_task[0].cfg.dynamic_batch_num = 1;
        g_svp_npu_task[0].cfg.total_t = 0;
        g_svp_npu_task[0].cfg.is_cached = HI_FALSE;
        g_svp_npu_task[0].cfg.model_idx = model_idx;

        ret = sample_svp_npu_acl_init_task(1, HI_FALSE, 0);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "init task failed!\n");
    }

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_get_input_data(src_file, SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM,
            &g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "get data failed!\n");
    }
    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "execute failed!\n");
        (hi_void)sample_common_svp_npu_output_classification_result(&g_svp_npu_task[0]);
    }

process_end2:
    (hi_void)sample_svp_npu_acl_deinit_task(1, 0);
process_end1:
    (hi_void)sample_common_svp_npu_unload_model(model_idx);
process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

hi_void sample_svp_npu_acl_resnet50_dynamic_batch(hi_void)
{
    hi_s32 ret;
    const hi_u32 model_idx = 0;
    const hi_char *acl_config_path = "";
    const hi_char *src_file[SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM] = {"./data/image/3_224_224_batch_8.bgr"};
    const hi_char *om_model_path = "./data/model/resnet50.om";

    g_svp_npu_terminate_signal = HI_FALSE;

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        /* init acl */
        ret = sample_svp_npu_acl_init(acl_config_path);
        sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

        /* load model */
        ret = sample_common_svp_npu_load_model(om_model_path, model_idx, HI_TRUE);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "load model failed!\n");

        /* set task cfg */
        g_svp_npu_task[0].cfg.max_batch_num = 8; /* 8 is max batch num, it can't be less than dynamic_batch_num */
        g_svp_npu_task[0].cfg.dynamic_batch_num = 8; /* 8 is batch num of task to be processed */
        g_svp_npu_task[0].cfg.total_t = 0;
        g_svp_npu_task[0].cfg.is_cached = HI_TRUE;
        g_svp_npu_task[0].cfg.model_idx = model_idx;
        ret = sample_svp_npu_acl_init_task(1, HI_FALSE, 0);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR, "init task failed!\n");
    }

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_get_input_data(src_file, SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM,
            &g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "get input data failed!\n");
        ret = sample_common_svp_npu_set_dynamic_batch(&g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "set dynamic batch failed!\n");
    }
    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "execute failed!\n");
        (hi_void)sample_common_svp_npu_output_classification_result(&g_svp_npu_task[0]);
    }

process_end2:
    (hi_void)sample_svp_npu_acl_deinit_task(1, 0);
process_end1:
    (hi_void)sample_common_svp_npu_unload_model(model_idx);
process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

/* function : show the sample of svp npu resnet50 multi thread */
hi_void sample_svp_npu_acl_resnet50_multi_thread(hi_void)
{
    hi_u32 i;
    hi_s32 ret;
    const hi_u32 model_idx = 0;
    const hi_char *acl_config_path = "";
    const hi_char *src_file[SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM] = {"./data/image/3_224_224_batch_1.bgr"};
    const hi_char *om_model_path = "./data/model/resnet50.om";

    g_svp_npu_terminate_signal = HI_FALSE;

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        /* init acl */
        ret = sample_svp_npu_acl_init(acl_config_path);
        sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

        /* load model */
        ret = sample_common_svp_npu_load_model(om_model_path, model_idx, HI_FALSE);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "load model failed!\n");

        /* set cfg */
        for (i = 0; i < SAMPLE_SVP_NPU_MAX_TASK_NUM; i++) {
            g_svp_npu_task[i].cfg.max_batch_num = 1;
            g_svp_npu_task[i].cfg.dynamic_batch_num = 1;
            g_svp_npu_task[i].cfg.total_t = 0;
            g_svp_npu_task[i].cfg.is_cached = HI_FALSE;
            g_svp_npu_task[i].cfg.model_idx = model_idx;
        }

        ret = sample_svp_npu_acl_init_task(SAMPLE_SVP_NPU_MAX_TASK_NUM, HI_TRUE, 0);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR, "init task failed!\n");
    }

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        for (i = 0; i < SAMPLE_SVP_NPU_MAX_TASK_NUM; i++) {
            ret = sample_common_svp_npu_get_input_data(src_file, SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM,
                &g_svp_npu_task[i]);
            sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2,
                SAMPLE_SVP_ERR_LEVEL_ERROR, "get %u-th input failed!\n", i);
        }
    }

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        (hi_void)sample_svp_npu_acl_model_execute_multithread();
    }

process_end2:
    (hi_void)sample_svp_npu_acl_deinit_task(SAMPLE_SVP_NPU_MAX_TASK_NUM, 0);
process_end1:
    (hi_void)sample_common_svp_npu_unload_model(model_idx);
process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

hi_void sample_svp_npu_acl_lstm(hi_void)
{
    hi_s32 ret;
    const hi_u32 model_idx = 0;
    const hi_char *src_file[SAMPLE_SVP_NPU_LSTM_INPUT_FILE_NUM] = {"./data/vector/xt.txt.bin",
        "./data/vector/cont.txt.bin"};
    const hi_char *om_model_path = "./data/model/lstm.om";

    g_svp_npu_terminate_signal = HI_FALSE;

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        /* init acl */
        ret = sample_svp_npu_acl_init(HI_NULL);
        sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

        /* load model */
        ret = sample_common_svp_npu_load_model(om_model_path, model_idx, HI_TRUE);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "load model failed!\n");

        /* set task cfg */
        g_svp_npu_task[0].cfg.max_batch_num = 1;
        g_svp_npu_task[0].cfg.dynamic_batch_num = 1;
        g_svp_npu_task[0].cfg.total_t = 3; /* 3 is total t */
        g_svp_npu_task[0].cfg.is_cached = HI_TRUE;
        g_svp_npu_task[0].cfg.model_idx = model_idx;
        ret = sample_svp_npu_acl_init_task(1, HI_FALSE, 0);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR, "init task failed!\n");
    }

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_get_input_data(src_file, SAMPLE_SVP_NPU_LSTM_INPUT_FILE_NUM, &g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "get input data failed!\n");
        ret = sample_common_svp_npu_set_dynamic_batch(&g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "set dynamic batch failed!\n");
    }
    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "execute failed!\n");
    }

process_end2:
    (hi_void)sample_svp_npu_acl_deinit_task(1, 0);
process_end1:
    (hi_void)sample_common_svp_npu_unload_model(model_idx);
process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

static hi_void sample_svp_npu_acl_set_task_info(hi_u32 task_idx, hi_u32 model_idx)
{
    g_svp_npu_task[task_idx].cfg.max_batch_num = 1;
    g_svp_npu_task[task_idx].cfg.dynamic_batch_num = 1;
    g_svp_npu_task[task_idx].cfg.total_t = 0;
    g_svp_npu_task[task_idx].cfg.is_cached = HI_TRUE;
    g_svp_npu_task[task_idx].cfg.model_idx = model_idx;
}

static hi_s32 sample_svp_npu_acl_set_input_aipp_info(hi_u32 model_index, hi_u32 index)
{
    const sample_svp_npu_model_aipp model_aipp = {
        {
            SVP_ACL_YUV420SP_U8,
            1, 1, 0,
            720, 576,       /* src image size */
            1192, 2166, 0,  /* csc coef: yuv420 -> BGR */
            1192, -218, -547,
            1192, 0, 1836,
            0, 0, 0,
            16, 128, 128
        }, {
            1, AIPP_PADDING_SWITCH_REPLICATION, AIPP_RESIZE_SWITCH_BILINEAR, 1,
            0, 0, 716, 572, /* crop param */
            2, 2, 2, 2, /* padding param */
            720, 576, 1920, 1080, /* resize param */
            1, 1, 1, 1, 0, 0, 0, 0, -32768, -32768, -32768, -32768 /* preprocess param */
        }
    };
    return sample_common_svp_npu_set_input_aipp_info(model_index, index, &model_aipp, &g_svp_npu_task[model_index]);
}

static hi_s32 sample_svp_npu_acl_set_aipp_info(hi_u32 model_index)
{
    hi_s32 ret;

    ret = sample_svp_npu_acl_set_input_aipp_info(model_index, 0);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, fail, SAMPLE_SVP_ERR_LEVEL_ERROR, "set input aipp failed!\n");

    return HI_SUCCESS;

fail:
    return ret;
}

hi_void sample_svp_npu_acl_aipp(hi_void)
{
    hi_s32 ret;
    const hi_u32 model_idx = 0;
    const hi_char *acl_config_path = "";
    const hi_char *src_file[SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM] = {"./data/image/st_lk_720x576_420sp.yuv"};
    const hi_char *om_model_path = "./data/model/aipp.om";

    /* init acl */
    ret = sample_svp_npu_acl_init(acl_config_path);
    sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

    /* load model */
    ret = sample_common_svp_npu_load_model(om_model_path, model_idx, HI_FALSE);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "load model failed!\n");

    /* set task cfg */
    g_svp_npu_task[0].cfg.max_batch_num = 1;
    g_svp_npu_task[0].cfg.dynamic_batch_num = 1;
    g_svp_npu_task[0].cfg.total_t = 0;
    g_svp_npu_task[0].cfg.is_cached = HI_FALSE;
    g_svp_npu_task[0].cfg.model_idx = model_idx;

    ret = sample_svp_npu_acl_init_task(1, HI_FALSE, 0);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "init task failed!\n");

    ret = sample_svp_npu_acl_set_aipp_info(model_idx);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR,
        "set aipp info failed!\n");

    ret = sample_common_svp_npu_get_input_data(src_file, SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM,
        &g_svp_npu_task[0]);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "get data failed!\n");

    ret = sample_common_svp_npu_model_execute(&g_svp_npu_task[0]);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "execute failed!\n");

process_end2:
    (hi_void)sample_svp_npu_acl_deinit_task(1, 0);
process_end1:
    (hi_void)sample_common_svp_npu_unload_model(model_idx);
process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

hi_void sample_svp_npu_acl_preemption(hi_void)
{
    hi_s32 ret;
    const hi_u32 model_num = 2;
    const hi_char *acl_config_path = "";
    const hi_char *src_file[SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM] = {"./data/image/3_224_224_batch_1.bgr"};
    const hi_char *om_model_path = "./data/model/resnet50_with_preemption.om";
    hi_u32 i, j;
    hi_u32 idx[SAMPLE_SVP_NPU_MAX_MODEL_NUM] = {0};
    pthread_t svp_npu_multi_thread[SAMPLE_SVP_NPU_MAX_MODEL_NUM] = {0};

    sample_svp_npu_model_prior_cfg prior_cfg[] = {
        { SAMPLE_SVP_NPU_PRIORITY_2, 1, 0, 0},
        { SAMPLE_SVP_NPU_PRIORITY_4, 1, SAMPLE_SVP_NPU_PRIOR_TIMEOUT, 0 }
    };

    /* init acl */
    ret = sample_svp_npu_acl_init(acl_config_path);
    sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

    for (i = 0; i < model_num; i++) {
        sample_svp_npu_set_model_prior_info(i, &prior_cfg[i]);
    }

    /* load model */
    ret = sample_svp_npu_acl_load_multi_model(om_model_path, model_num, HI_TRUE, src_file,
        SAMPLE_SVP_NPU_RESNET50_INPUT_FILE_NUM);
    sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR, "load model failed!\n");

    g_svp_npu_is_thread_start = HI_TRUE;

    for (i = 0; i < model_num; i++) {
        idx[i] = i;
        ret = pthread_create(&svp_npu_multi_thread[i], NULL, sample_svp_npu_acl_thread_multi_model_process, &idx[i]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, TEST_PICO_ERR_LEVEL_ERROR,
            "pthread_create failed!\n");
    }

    sleep(SAMPLE_SVP_NPU_PREEMPTION_SLEEP_SECOND);

process_end1:
    g_svp_npu_is_thread_start = HI_FALSE;
    for (j = 0; j < i; j++) {
        pthread_join(svp_npu_multi_thread[j], NULL);
    }

    (hi_void)sample_svp_npu_acl_unload_multi_model(model_num);

process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

static hi_void sample_svp_npu_acl_set_detection_info(sample_svp_npu_detection_info *detection_info, hi_u32 index)
{
    detection_info->num_name = g_svp_npu_yolo_roi_info[index].roi_num_name;
    detection_info->roi_name = g_svp_npu_yolo_roi_info[index].roi_class_name;
    detection_info->has_background = HI_FALSE;
    /* use PIC_BUTT to be a flag, get the input resolution form om */
    g_svp_npu_media_cfg.pic_type[1] = PIC_BUTT;
    g_svp_npu_terminate_signal = HI_FALSE;
}
static td_void print_roitype_mode()
{
    printf("please input choose roitype mode!\n");
    printf("\t (0) RECT type.\n");
    printf("\t (1) MASK type.\n");
}

static hi_s32 sample_getchar()
{
    hi_s32 c;
    if (g_svp_npu_terminate_signal == HI_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        return 'e';
    }

    c = getchar();

    if (g_svp_npu_terminate_signal == HI_TRUE) {
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
        return 'e';
    }

    return c;
}
static hi_s32 sample_clear_invalid_ch()
{
    hi_s32 c;

    while ((c = sample_getchar()) != ENTER_ASCII) {
    }
    return HI_SUCCESS;
}
static hi_s32 set_roitype_mode(hi_s32 c, hi_venc_svc_roi_type *roi_type)
{
    switch (c) {
        case '0':
            *roi_type = HI_VENC_SVC_ROI_TYPE_RECT;
            break;

        case '1':
            *roi_type = HI_VENC_SVC_ROI_TYPE_MASK;
            break;

        default:
            return HI_FAILURE;
    }
    return HI_SUCCESS;
}

static hi_s32 get_roitype_mode(hi_venc_svc_roi_type *roi_type)
{
    hi_s32 c;

    while (HI_TRUE) {
        print_roitype_mode();
        c = sample_getchar();
        if (g_svp_npu_terminate_signal) {
            return HI_FAILURE;
        }

        if (c == ENTER_ASCII) {
            sample_print("invalid input! please try again.\n");
            continue;
        } else if (set_roitype_mode(c, roi_type) == HI_SUCCESS && sample_getchar() == ENTER_ASCII) {
            return HI_SUCCESS;
        }
        sample_clear_invalid_ch();
        sample_print("invalid input! please try again.\n");
    }

    return HI_SUCCESS;
}

/* function : show the sample of e2e(vi -> vpss -> npu -> venc) yolo */
hi_void sample_svp_npu_acl_e2e_yolo(hi_u32 index)
{
    hi_s32 ret;
    const hi_u32 model_idx = 0;
    hi_char om_model_path[SAMPLE_SVP_NPU_PATH_LEN] = {0};
    sample_svp_npu_detection_info detection_info = {0};
    sample_svp_npu_thread_args args;
    ret = get_roitype_mode(&g_svc_mode);
    sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "get_roitype_mode failed!\n");

    args.model_name = SAMPLE_SVP_NPU_YOLO;
    args.detection_info = &detection_info;

    ret = sprintf_s(om_model_path, SAMPLE_SVP_NPU_PATH_LEN - 1, "%s%u%s", "./data/model/yolov", index, ".om");
    sample_svp_check_exps_return_void(ret <= 0, SAMPLE_SVP_ERR_LEVEL_ERROR, "sprintf_s failed!\n");

    sample_svp_npu_acl_set_detection_info(&detection_info, index);

    if (g_svp_npu_terminate_signal == HI_FALSE) {
        /* init acl */
        ret = sample_svp_npu_acl_init(HI_NULL);
        sample_svp_check_exps_return_void(ret != HI_SUCCESS, SAMPLE_SVP_ERR_LEVEL_ERROR, "init failed!\n");

        /* load model */
        ret = sample_common_svp_npu_load_model(om_model_path, model_idx, HI_FALSE);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end0, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "load model failed!\n");

        /* get input resolution */
        ret = sample_common_svp_npu_get_input_resolution(model_idx, 0, &g_svp_npu_media_cfg.pic_size[1]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "get pic size failed!\n");

        /* start vi vpss venc */
        ret = sample_common_svp_npu_start_vi_vpss(&g_vi_config, &g_svp_npu_media_cfg);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end1, SAMPLE_SVP_ERR_LEVEL_DEBUG,
            "init media failed!\n");

        /* set cfg */
        sample_svp_npu_acl_set_task_info(0, model_idx);

        ret = sample_svp_npu_acl_init_task(1, HI_FALSE, 0);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end2, SAMPLE_SVP_ERR_LEVEL_ERROR, "init task failed!\n");
    }
    /* process */
    if (g_svp_npu_terminate_signal == HI_FALSE) {
        ret = sample_common_svp_npu_set_threshold(&g_svp_npu_yolo_threshold[index], SAMPLE_SVP_NPU_YOLO_THRESHOLD_NUM,
            &g_svp_npu_task[0]);
        sample_svp_check_exps_goto(ret != HI_SUCCESS, process_end3, SAMPLE_SVP_ERR_LEVEL_ERROR,
            "set threshold failed!\n");

        g_svp_npu_thread_stop = HI_FALSE;
        ret = pthread_create(&g_svp_npu_thread, 0, sample_svp_npu_acl_to_venc, (hi_void*)&args);
        sample_svp_check_exps_goto(ret != 0, process_end3, SAMPLE_SVP_ERR_LEVEL_ERROR, "create thread failed!\n");

        (hi_void)sample_svp_npu_acl_pause();

        g_svp_npu_thread_stop = HI_TRUE;
        pthread_join(g_svp_npu_thread, HI_NULL);
    }

process_end3:
    (hi_void)sample_svp_npu_acl_deinit_task(1, 0);
process_end2:
    (hi_void)sample_common_svp_destroy_vb_stop_vi_vpss(&g_vi_config, &g_svp_npu_media_cfg);
process_end1:
    (hi_void)sample_common_svp_npu_unload_model(model_idx);
process_end0:
    (hi_void)sample_svp_npu_acl_deinit();
    (hi_void)sample_svp_npu_acl_terminate();
}

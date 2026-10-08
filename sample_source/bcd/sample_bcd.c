/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hi_audio_bcd.h"
#include <limits.h>
#include "hi_common.h"

#define HOUR_TO_MINUTE_RATIO 60
#define MINUTE_TO_SECOND_RATIO 60
#define SECOND_TO_MS_RATIO 1000

#define SAMPLE_BCD_ALARM_PERIOD 3000 /* detected period is 3000ms. */
#define SAMPLE_BCD_ALARM_COUNT_THRESHOLD 1 /* at least one crying detected in alarm period. */
#define SAMPLE_BCD_ALARM_SENSITIVITY 80 /* detected sensitivity is 80. */
#define SAMPLE_BCD_ALARM_INTERVAL 0 /* detected interval is 0ms. */

#define SAMPLE_BCD_FRAME_SIZE_MAX 4096
#define SAMPLE_BCD_FRAME_TIME 10 /* 10ms */
#define SAMPLE_BCD_FRAME_NUM_IN_SECOND (SECOND_TO_MS_RATIO / SAMPLE_BCD_FRAME_TIME)
#define SAMPLE_BCD_SAMPLE_RATE 16000

#define FILE_PATH_ARGV_ID   1
#define SAMPLE_BCD_ARGC_NUM 2

static hi_s32 g_cry_cnt = 0;
static hi_s32 g_cry_time = 0;

static hi_s16 g_sin_buf[SAMPLE_BCD_FRAME_SIZE_MAX] = { 0 };
static hi_s16 g_sou_buf[SAMPLE_BCD_FRAME_SIZE_MAX] = { 0 };

static hi_void show_time(hi_s32 time)
{
    hi_s32 hour = 0;
    hi_s32 min = 0;
    hi_s32 sec = time / SECOND_TO_MS_RATIO;

    if (sec != 0) {
        min = sec / MINUTE_TO_SECOND_RATIO;
        sec = sec % MINUTE_TO_SECOND_RATIO;
    }

    if (min != 0) {
        hour = min / HOUR_TO_MINUTE_RATIO;
        min = min % HOUR_TO_MINUTE_RATIO;
    }

    printf("[%d:%02d:%02d:%d]", hour, min, sec, time % SECOND_TO_MS_RATIO);
}

static hi_s32 sample_bcd_callback(hi_void *ctx)
{
    hi_unused(ctx);
    show_time(g_cry_time);
    printf("sample_bcd_callback now, id = %d.\n", g_cry_cnt++);
    return HI_SUCCESS;
}

static hi_void sample_bcd_usage(hi_void)
{
    printf("\n\nUsage:./sample_bcd <file_path>\n");
    printf("\tfile format: 16kHz 16bit 1chn.\n");
}

static hi_s32 sample_bcd_core(hi_s32 sample_rate, FILE *fd_in)
{
    hi_s32 ret;
    hi_bcd_handle bcd = HI_NULL;
    hi_u32 frame_sample = 0;
    hi_bcd_config bcd_cfg;
    hi_bcd_process_data input_data = { 0 };
    hi_bcd_process_data output_data = { 0 };

    /* create bcd handle. */
    bcd_cfg.usr_mode = HI_TRUE;
    bcd_cfg.bypass = HI_FALSE;
    bcd_cfg.alarm_sensitivity = SAMPLE_BCD_ALARM_SENSITIVITY;
    bcd_cfg.alarm_period = SAMPLE_BCD_ALARM_PERIOD;
    bcd_cfg.alarm_count_threshold = SAMPLE_BCD_ALARM_COUNT_THRESHOLD;
    bcd_cfg.alarm_interval = SAMPLE_BCD_ALARM_INTERVAL;
    bcd_cfg.callback = (fn_bcd_callback)sample_bcd_callback;
    ret = hi_baby_crying_detection_init(&bcd, sample_rate, &bcd_cfg);
    if (ret != HI_SUCCESS) {
        printf("init bcd failed with 0x%x.\n", ret);
        return ret;
    }

    /* cry detection loop. */
    frame_sample = (hi_u32)(sample_rate / SAMPLE_BCD_FRAME_NUM_IN_SECOND);
    input_data.data_size = (hi_s32)(frame_sample * sizeof(hi_s16));
    output_data.data_size = (hi_s32)(frame_sample * sizeof(hi_s16));
    input_data.data = g_sin_buf;
    output_data.data = g_sou_buf;
    while (1) {
        if (frame_sample != fread(input_data.data, sizeof(hi_s16), frame_sample, fd_in)) {
            break;
        }

        ret = hi_baby_crying_detection_process(bcd, &input_data, &output_data);
        if (ret != HI_SUCCESS) {
            printf("bcd process fail with 0x%x.\n", ret);
            break;
        }

        g_cry_time += SAMPLE_BCD_FRAME_TIME;
    }

    show_time(g_cry_time);
    printf("## result ## bcd detect over, total_count = %d.\n", g_cry_cnt);

    (td_void)hi_baby_crying_detection_deinit(bcd);

    return ret;
}

#ifdef __LITEOS__
hi_s32 app_main(int argc, char *argv[])
#else
hi_s32 main(int argc, char *argv[])
#endif
{
    hi_s32 ret = HI_SUCCESS;
    FILE *fd_in = HI_NULL;
    char *cry_file_path = HI_NULL;
#ifndef __LITEOS__
    char path[PATH_MAX] = {0};
#endif

    if (argc != SAMPLE_BCD_ARGC_NUM) {
        goto invalid_param;
    }

    if (strcmp(argv[1], "-h") == HI_SUCCESS) {
        goto invalid_param;
    }

    cry_file_path = argv[FILE_PATH_ARGV_ID];
    printf("Notes: The BCD only supports processing mono audio data with sampling rate of 16kHz.\n");

    g_cry_cnt = 0;
    g_cry_time = 0;

#ifndef __LITEOS__
    if (realpath(cry_file_path, path) == HI_NULL) {
        printf("[func]:%s [line]:%d [info]:%s.\n", __FUNCTION__, __LINE__, "realpath file failed");
        return HI_FAILURE;
    }

    fd_in = fopen(path, "rb");
#else
    fd_in = fopen(cry_file_path, "rb");
#endif
    if (fd_in == HI_NULL) {
        printf("[func]:%s [line]:%d [info]:%s.\n", __FUNCTION__, __LINE__, "open in file failed");
        return HI_FAILURE;
    }

    ret = sample_bcd_core(SAMPLE_BCD_SAMPLE_RATE, fd_in);
    (hi_void)fclose(fd_in);
    return ret;

invalid_param:
    sample_bcd_usage();
    return HI_FAILURE;
}

/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */
#include "sample_ive_main.h"

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

#include "sample_common_svp.h"

#define HI_SAMPLE_IVE_ARG_NUM_THREE    3
#define HI_SAMPLE_IVE_ARG_NUM_TWO      2
#define HI_SAMPLE_IVE_ARG_IDX_TWO      2
#define HI_SAMPLE_IVE_CMP_STR_NUM      2

static char **g_cmd_argv = HI_NULL;
/*
 * function : to process abnormal case
 */
#ifndef __LITEOS__
static hi_void sample_ive_handle_sig(hi_s32 singal)
{
    if (singal == SIGINT || singal == SIGTERM) {
        switch (*g_cmd_argv[1]) {
            case '0': {
                sample_ive_canny_handle_sig();
                break;
                }
            case '1': {
                sample_ive_gmm2_handle_sig();
                break;
                }
            case '2': {
                sample_ive_test_memory_handle_sig();
                break;
                }
            case '3': {
                sample_ive_sobel_handle_sig();
                break;
                }
            case '4': {
                sample_ive_st_lk_handle_sig();
                break;
                }
            case '5': {
                sample_ive_warp_persp_trans_handle_sig();
                break;
                }
            case '6': {
                sample_ive_od_handle_sig();
                break;
                }
            case '7': {
                sample_ive_md_handle_sig();
                break;
                }
            case '8': {
                sample_ive_resize_handle_sig();
                break;
                }
            default: {
                break;
                }
        }
    }
}
#endif

/*
 * function : show usage
 */
static hi_void sample_ive_usage(const hi_char *name)
{
    printf("Usage : %s <index> [complete] \n", name);
    printf("index:\n");
    printf("\t 0)Canny,<complete>:0, part canny;1,complete canny.(FILE->IVE->FILE).\n");
    printf("\t 1)Gmm2.(FILE->IVE->FILE).\n");
    printf("\t 2)MemoryTest.(FILE->IVE->FILE).\n");
    printf("\t 3)Sobel.(FILE->IVE->FILE).\n");
    printf("\t 4)St Lk.(FILE->IVE->FILE).\n");
    printf("\t 5)PerspTrans.(FILE->IVE->FILE).\n");
    printf("\t 6)Occlusion detected.(VI->VPSS->IVE->VENC).\n");
    printf("\t 7)Motion detected.(VI->VPSS->IVE->VGS->VENC).\n");
    printf("\t 8)Resize.(FILE->IVE->FILE).\n");
}

/*
 * run the selected function
 */
static hi_s32 sample_ive_case_with_two_arg(char *argv[])
{
    hi_s32 ret = HI_SUCCESS;
    hi_s32 len = (hi_s32)strlen(argv[HI_SAMPLE_IVE_ARG_IDX_TWO]);
    switch (*argv[1]) {
        case '0': {
            if (((*argv[HI_SAMPLE_IVE_ARG_IDX_TWO] != '0') && (*argv[HI_SAMPLE_IVE_ARG_IDX_TWO] != '1')) ||
                (len != 1)) {
                return HI_FAILURE;
            }
            sample_ive_canny(*argv[HI_SAMPLE_IVE_ARG_IDX_TWO]);
            break;
            }
        default: {
            ret = HI_FAILURE;
            break;
            }
    }

    return ret;
}
static hi_s32 sample_ive_case_with_one_arg(char *argv[])
{
    hi_s32 ret = HI_SUCCESS;
    switch (*argv[1]) {
        case '1': {
            sample_ive_gmm2();
            break;
            }
        case '2': {
            sample_ive_test_memory();
            break;
            }
        case '3': {
            sample_ive_sobel();
            break;
            }
        case '4': {
            sample_ive_st_lk();
            break;
            }
        case '5': {
            sample_ive_warp_persp_trans();
            break;
            }
        case '6': {
            sample_ive_od();
            break;
            }
        case '7': {
            sample_ive_md();
            break;
            }
        case '8': {
            sample_ive_resize();
            break;
            }
        default: {
            ret = HI_FAILURE;
            break;
            }
    }
    return ret;
}
/*
 * function : ive sample
 */
#ifdef __LITEOS__
int app_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    hi_s32 ret;
    hi_s32 idx_len;
#ifndef __LITEOS__
    struct sigaction sa;
#endif
    if ((argc < HI_SAMPLE_IVE_ARG_NUM_TWO) || (argc > HI_SAMPLE_IVE_ARG_NUM_THREE)) {
        sample_ive_usage(argv[0]);
        return HI_FAILURE;
    }
    if (!strncmp(argv[1], "-h", HI_SAMPLE_IVE_CMP_STR_NUM)) {
        sample_ive_usage(argv[0]);
        return HI_SUCCESS;
    }

    g_cmd_argv = argv;
#ifndef __LITEOS__
    (hi_void)memset_s(&sa, sizeof(struct sigaction), 0, sizeof(struct sigaction));
    sa.sa_handler = sample_ive_handle_sig;
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif

    idx_len = (hi_s32)strlen(argv[1]);
    if (idx_len != 1) {
        sample_ive_usage(argv[0]);
        return HI_FAILURE;
    }
    if (argc == HI_SAMPLE_IVE_ARG_NUM_THREE) {
        ret = sample_ive_case_with_two_arg(argv);
    } else {
        ret = sample_ive_case_with_one_arg(argv);
    }
    if (ret != HI_SUCCESS) {
        sample_ive_usage(argv[0]);
        return HI_FAILURE;
    }

    return 0;
}

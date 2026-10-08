/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "sample_utils.h"
#include "hi_mpi_cipher.h"
#include "sample_func.h"

#define GENERATE_TIMES  10

hi_s32 sample_trng(hi_void)
{
    sample_log("************ test trng ************\n");
    hi_s32 ret;
    hi_s32 index;
    hi_u32 random_number = 0;

    /* 1. cipher init */
    for (index = 0; index < GENERATE_TIMES; index++) {
        ret = hi_mpi_cipher_trng_get_random(&random_number);
        if (ret != HI_SUCCESS) {
            sample_err("hi_mpi_cipher_trng_get_random failed!\n");
            goto __EXIT__;
        }
    }
    sample_log("************ test trng success ************\n");
__EXIT__:
    return ret;
}


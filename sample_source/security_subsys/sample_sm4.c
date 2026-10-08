/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include "securec.h"

#include "sample_utils.h"
#include "hi_mpi_cipher.h"
#include "hi_mpi_km.h"
#include "hi_mpi_otp.h"
#include "sample_func.h"

#define MAX_DATA_LEN        128
#define TEST_DATA_LEN       32
#define MAX_SALT_LEN        28

typedef struct {
    const hi_char *name;
    hi_u8 key[MAX_KEY_LEN];
    hi_u32 key_len;
    hi_u8 src_data[MAX_DATA_LEN];
    hi_u32 data_len;
    hi_u8 salt[MAX_SALT_LEN];
    hi_u32 salt_len;
    hi_u32 rootkey_type;
    hi_bool key_parity;
    hi_bool oneway;
    km_crypto_alg engine;
    crypto_kdf_hard_alg kdf_hard_alg;
    crypto_klad_key_size key_size;
    crypto_symc_attr symc_attr;
    crypto_symc_ctrl_t symc_ctrl;
} symc_data_t;

/* sm4 cbc/ctr */
static symc_data_t g_sm4_data[] = {
    {
        .name = "SM4-CBC-128BITS",
        .key_len  = SM4_KEY_LEN, .data_len = TEST_DATA_LEN,
        .rootkey_type = CRYPTO_KDF_HARD_KEY_TYPE_ERK_REE, .salt_len = 28,
        .key_parity = 0, .oneway = 0, .engine = KM_CRYPTO_ALG_SM4,
        .kdf_hard_alg = CRYPTO_KDF_HARD_ALG_SHA256,
        .key_size = CRYPTO_KLAD_KEY_SIZE_128BIT,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_SM4,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CBC,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
    {
        .name = "SM4-CTR-128BITS",
        .key_len = SM4_KEY_LEN, .data_len = TEST_DATA_LEN,
        .rootkey_type = CRYPTO_KDF_HARD_KEY_TYPE_ERK_REE, .salt_len = 28,
        .key_parity = 0, .oneway = 0, .engine = KM_CRYPTO_ALG_SM4,
        .kdf_hard_alg = CRYPTO_KDF_HARD_ALG_SM3,
        .key_size = CRYPTO_KLAD_KEY_SIZE_128BIT,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_SM4,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CTR,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
};

static hi_s32 cipher_set_clear_key(crypto_handle keyslot_handle, hi_u8 *key, hi_u32 keylen)
{
    hi_s32 ret = HI_SUCCESS;
    crypto_handle klad_handle = 0;
    td_u32 offset = 0x12;
    td_u8 tee_enable = 0;
    km_klad_attr klad_attr = {
        .key_cfg = {
            .engine = KM_CRYPTO_ALG_SM4,
            .decrypt_support = HI_TRUE,
            .encrypt_support = HI_TRUE
        }
    };
    km_klad_clear_key clear_key = {
        .key = key,
        .key_size = keylen
    };
    (td_void)hi_mpi_otp_init();
    (td_void)hi_mpi_otp_read_byte(offset, &tee_enable);
    (td_void)hi_mpi_otp_deinit();
    if (tee_enable == 0x42) {
        klad_attr.key_sec_cfg.key_sec = KM_KLAD_SEC_ENABLE;
        klad_attr.key_sec_cfg.master_only_enable = HI_TRUE;
        klad_attr.key_sec_cfg.dest_buf_sec_support = HI_TRUE;
        klad_attr.key_sec_cfg.src_buf_sec_support = HI_TRUE;
        klad_attr.key_sec_cfg.src_buf_non_sec_support = HI_FALSE;
        klad_attr.key_sec_cfg.dest_buf_non_sec_support = HI_FALSE;
    } else {
        klad_attr.key_sec_cfg.key_sec = KM_KLAD_SEC_DISABLE;
        klad_attr.key_sec_cfg.master_only_enable = HI_FALSE;
        klad_attr.key_sec_cfg.dest_buf_sec_support = HI_FALSE;
        klad_attr.key_sec_cfg.dest_buf_non_sec_support = HI_TRUE;
        klad_attr.key_sec_cfg.src_buf_sec_support = HI_FALSE;
        klad_attr.key_sec_cfg.src_buf_non_sec_support = HI_TRUE;
    }

    /* 1. klad create handle */
    sample_chk_expr_return(hi_mpi_klad_create(&klad_handle), HI_SUCCESS);

    /* 2. klad set attr for clear key */
    sample_chk_expr_goto(hi_mpi_klad_set_attr(klad_handle, &klad_attr), HI_SUCCESS, __KLAD_DESTORY__);

    /* 3. attach klad handle & kslot handle */
    sample_chk_expr_goto(hi_mpi_klad_attach(klad_handle, KM_KLAD_DEST_TYPE_MCIPHER, keyslot_handle),
        HI_SUCCESS, __KLAD_DESTORY__);

    /* 4. set clear key */
    sample_chk_expr_goto(hi_mpi_klad_set_clear_key(klad_handle, &clear_key), HI_SUCCESS, __KLAD_DETACH__);

__KLAD_DETACH__:
    hi_mpi_klad_detach(klad_handle, KM_KLAD_DEST_TYPE_MCIPHER, keyslot_handle);
__KLAD_DESTORY__:
    hi_mpi_klad_destroy(klad_handle);
    return ret;
}

/* phy address crypto data using specific chn */
static hi_s32 sample_one_pack_crypto_clearkey(symc_data_t *data)
{
    hi_s32 ret = HI_SUCCESS;
    hi_handle symc_handle = 0;
    crypto_handle keyslot_handle = 0;
    crypto_buf_attr src_buf = {0};
    crypto_buf_attr dst_buf = {0};
    hi_u32 length = data->data_len;
    hi_void *src_virt_addr = HI_NULL;
    hi_void *dst_virt_addr = HI_NULL;

    sample_chk_expr_goto(cipher_alloc(&src_buf, (hi_void **)&src_virt_addr, length), HI_SUCCESS, CIPHER_FREE);
    sample_chk_expr_goto(cipher_alloc(&dst_buf, (hi_void **)&dst_virt_addr, length), HI_SUCCESS, CIPHER_FREE);

    /* 1. cipher init */
    sample_chk_expr_goto(hi_mpi_cipher_symc_init(), HI_SUCCESS, CIPHER_FREE);

    /* 2. km init */
    sample_chk_expr_goto(hi_mpi_km_init(), HI_SUCCESS, CIPHER_DEINIT);

    /* 3. cipher create handle */
    sample_chk_expr_goto(hi_mpi_cipher_symc_create(&symc_handle, &data->symc_attr), HI_SUCCESS, KM_DEINIT);

    /* 4. create keyslot handle */
    sample_chk_expr_goto(hi_mpi_keyslot_create(&keyslot_handle, KM_KEYSLOT_TYPE_MCIPHER), HI_SUCCESS,
        CIPHER_DESTROY);

    /* 5. attach cipher handle & kslot handle */
    sample_chk_expr_goto(hi_mpi_cipher_symc_attach(symc_handle, (hi_handle)keyslot_handle), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 6. set clear key */
    sample_chk_expr_goto(cipher_set_clear_key(keyslot_handle, data->key, data->key_len), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 7. encrypt */
    /* 7.1 set config for encrypt */
    sample_chk_expr_goto(hi_mpi_cipher_symc_set_config(symc_handle, &data->symc_ctrl), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 7.2. encrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, data->src_data, length),
        EOK, ret, HI_FAILURE, KEYSLOT_DESTROY);
    (hi_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(hi_mpi_cipher_symc_encrypt(symc_handle, &src_buf, &dst_buf, length), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 8. decrypt */
    /* 8.1 set config for decrypt */
    sample_chk_expr_goto(hi_mpi_cipher_symc_set_config(symc_handle, &data->symc_ctrl), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 8.2. decrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, dst_virt_addr, length), EOK, ret,
        HI_FAILURE, KEYSLOT_DESTROY);
    (hi_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(hi_mpi_cipher_symc_decrypt(symc_handle, &src_buf, &dst_buf, length), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 9. compare */
    sample_chk_expr_goto_with_ret(memcmp(dst_virt_addr, data->src_data, length),
        0, ret, HI_FAILURE, KEYSLOT_DESTROY);

KEYSLOT_DESTROY:
    hi_mpi_keyslot_destroy(keyslot_handle);
CIPHER_DESTROY:
    hi_mpi_cipher_symc_destroy(symc_handle);
KM_DEINIT:
    hi_mpi_km_deinit();
CIPHER_DEINIT:
    hi_mpi_cipher_symc_deinit();
CIPHER_FREE:
    cipher_free(&src_buf, src_virt_addr);
    cipher_free(&dst_buf, dst_virt_addr);
    return ret;
}

static hi_s32 cipher_set_rootkey(crypto_handle keyslot_handle, symc_data_t *data)
{
    hi_s32 ret = HI_SUCCESS;
    crypto_handle klad_handle = 0;
    td_u32 offset = 0x12;
    td_u8 tee_enable = 0;
    km_klad_attr klad_attr = {
        .klad_cfg = {
            .rootkey_type = data->rootkey_type
        },
        .key_cfg = {
            .engine = data->engine,
            .decrypt_support = TD_TRUE,
            .encrypt_support = TD_TRUE
        }
    };
    km_klad_effective_key klad_effective_key = {
        .salt = (hi_u8 *)data->salt,
        .salt_length = data->salt_len,
        .kdf_hard_alg = data->kdf_hard_alg,
        .key_size = data->key_size,
        .key_parity = data->key_parity,
        .oneway = data->oneway,
    };

    (td_void)hi_mpi_otp_init();
    (td_void)hi_mpi_otp_read_byte(offset, &tee_enable);
    (td_void)hi_mpi_otp_deinit();
    if (tee_enable == 0x42) {
        klad_attr.key_sec_cfg.key_sec = KM_KLAD_SEC_ENABLE;
        klad_attr.key_sec_cfg.master_only_enable = HI_TRUE;
        klad_attr.key_sec_cfg.dest_buf_sec_support = HI_TRUE;
        klad_attr.key_sec_cfg.src_buf_sec_support = HI_TRUE;
        klad_attr.key_sec_cfg.src_buf_non_sec_support = HI_FALSE;
        klad_attr.key_sec_cfg.dest_buf_non_sec_support = HI_FALSE;
    } else {
        klad_attr.key_sec_cfg.key_sec = KM_KLAD_SEC_DISABLE;
        klad_attr.key_sec_cfg.master_only_enable = HI_FALSE;
        klad_attr.key_sec_cfg.dest_buf_sec_support = HI_FALSE;
        klad_attr.key_sec_cfg.dest_buf_non_sec_support = HI_TRUE;
        klad_attr.key_sec_cfg.src_buf_sec_support = HI_FALSE;
        klad_attr.key_sec_cfg.src_buf_non_sec_support = HI_TRUE;
    }

    /* 1. klad create handle */
    sample_chk_expr_return(hi_mpi_klad_create(&klad_handle), HI_SUCCESS);
    /* 2. klad set attr for rootkey */
    sample_chk_expr_goto(hi_mpi_klad_set_attr(klad_handle, &klad_attr), HI_SUCCESS, __KLAD_DESTORY__);
    /* 3. attach klad handle & kslot handle */
    sample_chk_expr_goto(hi_mpi_klad_attach(klad_handle, KM_KLAD_DEST_TYPE_MCIPHER, keyslot_handle),
        HI_SUCCESS, __KLAD_DESTORY__);
    /* 4. set effective key */
    sample_chk_expr_goto(hi_mpi_klad_set_effective_key(klad_handle, &klad_effective_key), HI_SUCCESS, __KLAD_DETACH__);

__KLAD_DETACH__:
    hi_mpi_klad_detach(klad_handle, KM_KLAD_DEST_TYPE_MCIPHER, keyslot_handle);
__KLAD_DESTORY__:
    hi_mpi_klad_destroy(klad_handle);
    return ret;
}

/* phy address crypto data using specific chn */
static hi_s32 sample_one_pack_crypto_rootkey(symc_data_t *data)
{
    hi_s32 ret = HI_SUCCESS;
    hi_handle symc_handle = 0;
    crypto_handle keyslot_handle = 0;
    crypto_buf_attr src_buf = {0};
    crypto_buf_attr dst_buf = {0};
    hi_u32 length = data->data_len;
    hi_void *src_virt_addr = HI_NULL;
    hi_void *dst_virt_addr = HI_NULL;

    sample_chk_expr_goto(cipher_alloc(&src_buf, (hi_void **)&src_virt_addr, length), HI_SUCCESS, CIPHER_FREE);
    sample_chk_expr_goto(cipher_alloc(&dst_buf, (hi_void **)&dst_virt_addr, length), HI_SUCCESS, CIPHER_FREE);

    /* 1. cipher init */
    sample_chk_expr_goto(hi_mpi_cipher_symc_init(), HI_SUCCESS, CIPHER_FREE);

    /* 2. km init */
    sample_chk_expr_goto(hi_mpi_km_init(), HI_SUCCESS, CIPHER_DEINIT);

    /* 3. cipher create handle */
    sample_chk_expr_goto(hi_mpi_cipher_symc_create(&symc_handle, &data->symc_attr), HI_SUCCESS, KM_DEINIT);

    /* 4. create keyslot handle */
    sample_chk_expr_goto(hi_mpi_keyslot_create(&keyslot_handle, KM_KEYSLOT_TYPE_MCIPHER), HI_SUCCESS,
        CIPHER_DESTROY);

    /* 5. attach cipher handle & kslot handle */
    sample_chk_expr_goto(hi_mpi_cipher_symc_attach(symc_handle, (hi_handle)keyslot_handle), HI_SUCCESS,
        KEYSLOT_DESTROY);
    
    /* 6. set root key */
    sample_chk_expr_goto(cipher_set_rootkey(keyslot_handle, data), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 7. encrypt */
    /* 7.1 set config for encrypt */
    sample_chk_expr_goto(hi_mpi_cipher_symc_set_config(symc_handle, &data->symc_ctrl), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 7.2. encrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, data->src_data, length),
        EOK, ret, HI_FAILURE, KEYSLOT_DESTROY);
    (hi_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(hi_mpi_cipher_symc_encrypt(symc_handle, &src_buf, &dst_buf, length), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 8. decrypt */
    /* 8.1 set config for decrypt */
    sample_chk_expr_goto(hi_mpi_cipher_symc_set_config(symc_handle, &data->symc_ctrl), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 8.2. decrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, dst_virt_addr, length), EOK, ret,
        HI_FAILURE, KEYSLOT_DESTROY);
    (hi_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(hi_mpi_cipher_symc_decrypt(symc_handle, &src_buf, &dst_buf, length), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 9. compare */
    sample_chk_expr_goto_with_ret(memcmp(dst_virt_addr, data->src_data, length),
        0, ret, HI_FAILURE, KEYSLOT_DESTROY);

KEYSLOT_DESTROY:
    hi_mpi_keyslot_destroy(keyslot_handle);
CIPHER_DESTROY:
    hi_mpi_cipher_symc_destroy(symc_handle);
KM_DEINIT:
    hi_mpi_km_deinit();
CIPHER_DEINIT:
    hi_mpi_cipher_symc_deinit();
CIPHER_FREE:
    cipher_free(&src_buf, src_virt_addr);
    cipher_free(&dst_buf, dst_virt_addr);
    return ret;
}

static hi_s32 sample_sm4_cal(hi_void)
{
    hi_u32 i;
    hi_s32 ret;
    hi_u32 num = (hi_u32)(sizeof(g_sm4_data) / sizeof(g_sm4_data[0]));
    sample_log("************ test SM4 clearkey start ************\n");
    for (i = 0; i < num; i++) {
        ret = sample_one_pack_crypto_clearkey(&g_sm4_data[i]);
        if (ret != HI_SUCCESS) {
            sample_err("************ test SM4 one pack %s failed ************\n", g_sm4_data[i].name);
            return ret;
        }
        sample_log("************ test SM4 one pack %s success ************\n", g_sm4_data[i].name);
    }
    sample_log("************ test SM4 clearkey done ************\n");

    num = (hi_u32)(sizeof(g_sm4_data) / sizeof(g_sm4_data[0]));
    sample_log("************ test SM4 rootkey start ************\n");
    for (i = 0; i < num; i++) {
        ret = sample_one_pack_crypto_rootkey(&g_sm4_data[i]);
        if (ret != HI_SUCCESS) {
            sample_err("************ test SM4 one pack %s failed ************\n", g_sm4_data[i].name);
            return ret;
        }
        sample_log("************ test SM4 one pack %s success ************\n", g_sm4_data[i].name);
    }
    sample_log("************ test SM4 rootkey done ************\n");
    return HI_SUCCESS;
}

static hi_s32 data_init(hi_void)
{
    hi_u32 i;
    hi_u32 num = 0;
    /* 1. init g_sm4_data */
    num = (hi_u32)(sizeof(g_sm4_data) / sizeof(g_sm4_data[0]));
    for (i = 0; i < num; i++) {
        sample_chk_expr_return(get_random_data(g_sm4_data[i].key, sizeof(g_sm4_data[i].key)), HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_sm4_data[i].symc_ctrl.iv, sizeof(g_sm4_data[i].symc_ctrl.iv)),
            HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_sm4_data[i].src_data, sizeof(g_sm4_data[i].src_data)), HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_sm4_data[i].salt, sizeof(g_sm4_data[i].salt)), HI_SUCCESS);
    }
    return HI_SUCCESS;
}

static hi_void data_deinit(hi_void)
{
    hi_u32 i;
    hi_u32 num = 0;
    /* 1. clear the data in g_sm4_data */
    num = (hi_u32)(sizeof(g_sm4_data) / sizeof(g_sm4_data[0]));
    for (i = 0; i < num; i++) {
        memset_s(g_sm4_data[i].key, sizeof(g_sm4_data[i].key), 0, sizeof(g_sm4_data[i].key));
        memset_s(g_sm4_data[i].salt, sizeof(g_sm4_data[i].salt), 0, sizeof(g_sm4_data[i].salt));
    }
}

hi_s32 sample_sm4(hi_void)
{
    hi_s32 ret;
    sample_chk_expr_return(data_init(), HI_SUCCESS);
    sample_log("************ test SM4 ************\n");
    ret = sample_sm4_cal();
    if (ret != HI_SUCCESS) {
        return ret;
    }
    sample_log("************ test SM4 succeed ************\n");
    data_deinit();
    return HI_SUCCESS;
}

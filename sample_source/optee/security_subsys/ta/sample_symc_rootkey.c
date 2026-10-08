/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "securec.h"
#include "sample_utils.h"
#include "ot_mpi_cipher.h"
#include "ot_mpi_km.h"
#include "ot_mpi_otp.h"
#include "sample_func.h"

#define MAX_DATA_LEN        128
#define TEST_DATA_LEN       32
#define MAX_SALT_LEN        28

typedef struct {
    const td_char *name;
    td_u8 src_data[MAX_DATA_LEN];
    td_u32 data_len;
    td_u8 salt[MAX_SALT_LEN];
    td_u32 salt_len;
    td_u32 rootkey_type;
    td_bool key_parity;
    td_bool oneway;
    km_crypto_alg engine;
    crypto_kdf_hard_alg kdf_hard_alg;
    crypto_klad_key_size key_size;
    crypto_symc_attr symc_attr;
    crypto_symc_ctrl_t symc_ctrl;
} symc_data_root_key_t;

/* aes ecb/cbc/cfb/ofb/ctr */
static symc_data_root_key_t g_aes_data[] = {
    {
        .name = "AES-CBC-128BITS", .data_len = TEST_DATA_LEN,
        .rootkey_type = CRYPTO_KDF_HARD_KEY_TYPE_ERK_REE, .salt_len = 28,
        .key_parity = 0, .oneway = 0, .engine = KM_CRYPTO_ALG_AES,
        .kdf_hard_alg = CRYPTO_KDF_HARD_ALG_SHA256,
        .key_size = CRYPTO_KLAD_KEY_SIZE_128BIT,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = TD_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CBC,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
    {
        .name = "AES-OFB-128BITS", .data_len = TEST_DATA_LEN,
        .rootkey_type = CRYPTO_KDF_HARD_KEY_TYPE_ERK_TEE, .salt_len = 28,
        .key_parity = 0, .oneway = 0, .engine = KM_CRYPTO_ALG_AES,
        .kdf_hard_alg = CRYPTO_KDF_HARD_ALG_SM3,
        .key_size = CRYPTO_KLAD_KEY_SIZE_128BIT,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = TD_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .work_mode = CRYPTO_SYMC_WORK_MODE_OFB,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
    {
        .name = "AES-CFB-128BITS", .data_len = TEST_DATA_LEN,
        .rootkey_type = CRYPTO_KDF_HARD_KEY_TYPE_ERK1_REE, .salt_len = 28,
        .key_parity = 0, .oneway = 0, .engine = KM_CRYPTO_ALG_AES,
        .kdf_hard_alg = CRYPTO_KDF_HARD_ALG_SHA256,
        .key_size = CRYPTO_KLAD_KEY_SIZE_128BIT,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = TD_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CFB,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
    {
        .name = "AES-CTR-128BITS", .data_len = TEST_DATA_LEN,
        .rootkey_type = CRYPTO_KDF_HARD_KEY_TYPE_ERK2_REE, .salt_len = 28,
        .key_parity = 0, .oneway = 0, .engine = KM_CRYPTO_ALG_AES,
        .kdf_hard_alg = CRYPTO_KDF_HARD_ALG_SHA256,
        .key_size = CRYPTO_KLAD_KEY_SIZE_128BIT,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = TD_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CTR,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
};

static td_s32 cipher_set_rootkey(crypto_handle keyslot_handle, symc_data_root_key_t *data)
{
    td_s32 ret = TD_SUCCESS;
    crypto_handle klad_handle = 0;
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
        .salt = (td_u8 *)data->salt,
        .salt_length = data->salt_len,
        .kdf_hard_alg = data->kdf_hard_alg,
        .key_size = data->key_size,
        .key_parity = data->key_parity,
        .oneway = data->oneway,
    };

    klad_attr.key_sec_cfg.key_sec = KM_KLAD_SEC_ENABLE;
    klad_attr.key_sec_cfg.master_only_enable = TD_TRUE;
    klad_attr.key_sec_cfg.dest_buf_sec_support = TD_TRUE;
    klad_attr.key_sec_cfg.src_buf_sec_support = TD_TRUE;
    klad_attr.key_sec_cfg.src_buf_non_sec_support = TD_FALSE;
    klad_attr.key_sec_cfg.dest_buf_non_sec_support = TD_FALSE;

    /* 1. klad create handle */
    sample_chk_expr_return(ot_mpi_klad_create(&klad_handle), TD_SUCCESS);

    /* 2. klad set attr for rootkey */
    sample_chk_expr_goto(ot_mpi_klad_set_attr(klad_handle, &klad_attr), TD_SUCCESS, __KLAD_DESTORY__);

    /* 3. attach klad handle & kslot handle */
    sample_chk_expr_goto(ot_mpi_klad_attach(klad_handle, KM_KLAD_DEST_TYPE_MCIPHER, keyslot_handle),
        TD_SUCCESS, __KLAD_DESTORY__);

    /* 4. set effective key */
    sample_chk_expr_goto(ot_mpi_klad_set_effective_key(klad_handle, &klad_effective_key), TD_SUCCESS, __KLAD_DETACH__);

__KLAD_DETACH__:
    ot_mpi_klad_detach(klad_handle, KM_KLAD_DEST_TYPE_MCIPHER, keyslot_handle);
__KLAD_DESTORY__:
    ot_mpi_klad_destroy(klad_handle);
    return ret;
}

/* phy address crypto data using specific chn */
static td_s32 sample_one_pack_crypto(symc_data_root_key_t *data)
{
    td_s32 ret = TD_SUCCESS;
    td_handle symc_handle = 0;
    crypto_handle keyslot_handle = 0;
    crypto_buf_attr src_buf = {0};
    crypto_buf_attr dst_buf = {0};
    td_u32 length = data->data_len;
    td_void *src_virt_addr = TD_NULL;
    td_void *dst_virt_addr = TD_NULL;

    sample_chk_expr_goto(cipher_alloc(&src_buf, (td_void **)&src_virt_addr, length), TD_SUCCESS, CIPHER_FREE);
    sample_chk_expr_goto(cipher_alloc(&dst_buf, (td_void **)&dst_virt_addr, length), TD_SUCCESS, CIPHER_FREE);

    /* 1. cipher init */
    sample_chk_expr_goto(ot_mpi_cipher_symc_init(), TD_SUCCESS, CIPHER_FREE);

    /* 2. km init */
    sample_chk_expr_goto(ot_mpi_km_init(), TD_SUCCESS, CIPHER_DEINIT);

    /* 3. cipher create handle */
    sample_chk_expr_goto(ot_mpi_cipher_symc_create(&symc_handle, &data->symc_attr), TD_SUCCESS, KM_DEINIT);

    /* 4. create keyslot handle */
    sample_chk_expr_goto(ot_mpi_keyslot_create(&keyslot_handle, KM_KEYSLOT_TYPE_MCIPHER), TD_SUCCESS,
        CIPHER_DESTROY);

    /* 5. attach cipher handle & kslot handle */
    sample_chk_expr_goto(ot_mpi_cipher_symc_attach(symc_handle, (td_handle)keyslot_handle), TD_SUCCESS,
        KEYSLOT_DESTROY);
    
    /* 6. set root key */
    sample_chk_expr_goto(cipher_set_rootkey(keyslot_handle, data), TD_SUCCESS, KEYSLOT_DESTROY);

    /* 7. encrypt */
    /* 7.1 set config for encrypt */
    sample_chk_expr_goto(ot_mpi_cipher_symc_set_config(symc_handle, &data->symc_ctrl), TD_SUCCESS, KEYSLOT_DESTROY);

    /* 7.2. encrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, data->src_data, length),
        EOK, ret, TD_FAILURE, KEYSLOT_DESTROY);
    (td_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(ot_mpi_cipher_symc_encrypt(symc_handle, &src_buf, &dst_buf, length), TD_SUCCESS,
        KEYSLOT_DESTROY);

    /* 8. decrypt */
    /* 8.1 set config for decrypt */
    sample_chk_expr_goto(ot_mpi_cipher_symc_set_config(symc_handle, &data->symc_ctrl), TD_SUCCESS, KEYSLOT_DESTROY);

    /* 8.2. decrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, dst_virt_addr, length), EOK, ret,
        TD_FAILURE, KEYSLOT_DESTROY);
    (td_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(ot_mpi_cipher_symc_decrypt(symc_handle, &src_buf, &dst_buf, length), TD_SUCCESS,
        KEYSLOT_DESTROY);

    /* 9. compare */
    sample_chk_expr_goto_with_ret(memcmp(dst_virt_addr, data->src_data, length),
        0, ret, TD_FAILURE, KEYSLOT_DESTROY);

KEYSLOT_DESTROY:
    ot_mpi_keyslot_destroy(keyslot_handle);
CIPHER_DESTROY:
    ot_mpi_cipher_symc_destroy(symc_handle);
KM_DEINIT:
    ot_mpi_km_deinit();
CIPHER_DEINIT:
    ot_mpi_cipher_symc_deinit();
CIPHER_FREE:
    cipher_free(&src_buf, src_virt_addr);
    cipher_free(&dst_buf, dst_virt_addr);
    return ret;
}

static td_s32 sample_aes(td_void)
{
    td_u32 i;
    td_s32 ret;
    td_u32 num = (td_u32)(sizeof(g_aes_data) / sizeof(g_aes_data[0]));
    for (i = 0; i < num; i++) {
        ret = sample_one_pack_crypto(&g_aes_data[i]);
        if (ret != TD_SUCCESS) {
            sample_err("************ test symc rootkey %s failed ************\n", g_aes_data[i].name);
            return ret;
        }
        sample_log("************ test symc rootkey %s success ************\n", g_aes_data[i].name);
    }
    return TD_SUCCESS;
}

static td_s32 data_init(td_void)
{
    td_u32 i;
    td_u32 num = 0;
    /* 1. init g_aes_data */
    num = (td_u32)(sizeof(g_aes_data) / sizeof(g_aes_data[0]));
    for (i = 0; i < num; i++) {
        sample_chk_expr_return(get_random_data(g_aes_data[i].src_data, sizeof(g_aes_data[i].src_data)), TD_SUCCESS);
        sample_chk_expr_return(get_random_data(g_aes_data[i].salt, sizeof(g_aes_data[i].salt)), TD_SUCCESS);
    }
    return TD_SUCCESS;
}

static td_void data_deinit(td_void)
{
    td_u32 i;
    td_u32 num = 0;
    /* 1. clear the data in g_aes_data */
    num = (td_u32)(sizeof(g_aes_data) / sizeof(g_aes_data[0]));
    for (i = 0; i < num; i++) {
        memset_s(g_aes_data[i].src_data, sizeof(g_aes_data[i].src_data), 0, sizeof(g_aes_data[i].src_data));
        memset_s(g_aes_data[i].salt, sizeof(g_aes_data[i].salt), 0, sizeof(g_aes_data[i].salt));
    }
}

td_s32 sample_symc_rootkey(td_void)
{
    td_s32 ret;
    sample_chk_expr_return(data_init(), TD_SUCCESS);
    sample_log("************ test symc rootkey ************\n");
    ret = sample_aes();
    if (ret != TD_SUCCESS) {
        return ret;
    }
    sample_log("************ test symc rootkey succeed ************\n");
    data_deinit();
    return TD_SUCCESS;
}

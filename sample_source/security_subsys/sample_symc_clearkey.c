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
#include "hi_mpi_sys_mem.h"
#include "sample_func.h"

#define MAX_DATA_LEN        128
#define TEST_DATA_LEN       32

typedef struct {
    const hi_char *name;
    hi_u8 key[MAX_KEY_LEN];
    hi_u32 key_len;
    hi_u8 src_data[MAX_DATA_LEN];
    hi_u32 data_len;
    crypto_symc_attr symc_attr;
    crypto_symc_ctrl_t symc_ctrl;
} symc_data_t;

typedef struct {
    symc_data_t data;
    hi_u8 aad[MAX_AAD_LEN];
    hi_u32 aad_len;
    hi_u32 tag_len;
} symc_data_ex_t;

/* aes ecb/cbc/cfb/ofb/ctr */
static symc_data_t g_aes_data[] = {
    {
        .name = "AES-CBC-128BITS",
        .key_len  = AES128_KEY_LEN, .data_len = TEST_DATA_LEN,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
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
        .name = "AES-CBC-256BITS",
        .key_len = AES256_KEY_LEN, .data_len = TEST_DATA_LEN,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
        },
        .symc_ctrl = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CBC,
            .symc_key_length = CRYPTO_SYMC_KEY_256BIT,
            .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
            .iv_length = IV_LEN,
        },
    },
    {
        .name = "AES-OFB-128BITS",
        .key_len = AES128_KEY_LEN, .data_len = TEST_DATA_LEN,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
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
        .name = "AES-CFB-128BITS",
        .key_len = AES128_KEY_LEN, .data_len = TEST_DATA_LEN,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
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
        .name = "AES-CTR-128BITS",
        .key_len = AES128_KEY_LEN, .data_len = TEST_DATA_LEN,
        .symc_attr = {
            .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
            .is_long_term = HI_FALSE,
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

/* aes ccm/gcm data */
static symc_data_ex_t g_aes_data_ex[] = {
    {
        .data = {
            .name = "AES-CCM-128BITS",
            .key_len = AES128_KEY_LEN, .data_len = TEST_DATA_LEN,
            .symc_attr = {
                .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
                .is_long_term = HI_FALSE,
            },
            .symc_ctrl = {
                .symc_alg = CRYPTO_SYMC_ALG_AES,
                .work_mode = CRYPTO_SYMC_WORK_MODE_CCM,
                .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
                .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
                .iv_change_flag = CRYPTO_SYMC_CCM_IV_CHANGE_START,
                .iv_length = CCM_IV_LEN,
            },
        },
        .aad_len = TEST_AAD_LEN, .tag_len = CCM_TAG_LEN,
    },
    {
        .data = {
            .name = "AES-GCM-128BITS",
            .key_len = AES128_KEY_LEN, .data_len = TEST_DATA_LEN,
            .symc_attr = {
                .symc_type = CRYPTO_SYMC_TYPE_NORMAL,
                .is_long_term = HI_FALSE,
            },
            .symc_ctrl = {
                .symc_alg = CRYPTO_SYMC_ALG_AES,
                .work_mode = CRYPTO_SYMC_WORK_MODE_GCM,
                .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
                .symc_bit_width = CRYPTO_SYMC_BIT_WIDTH_128BIT,
                .iv_change_flag = CRYPTO_SYMC_GCM_IV_CHANGE_START,
                .iv_length = GCM_IV_LEN,
            },
        },
        .aad_len = TEST_AAD_LEN, .tag_len = GCM_TAG_LEN,
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
            .engine = KM_CRYPTO_ALG_AES,
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
static hi_s32 sample_one_pack_crypto(symc_data_t *data)
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

/* sample aes ccm/gcm */
static hi_s32 sample_one_pack_crypto_ex(symc_data_ex_t *data_ex)
{
    hi_s32 ret = HI_SUCCESS;
    hi_handle symc_handle = 0;
    crypto_handle keyslot_handle = 0;
    crypto_buf_attr src_buf = {0};
    crypto_buf_attr dst_buf = {0};
    crypto_symc_config_aes_ccm_gcm ccm_gcm_config = {
        .aad_len = data_ex->aad_len,
        .data_len = data_ex->data.data_len,
        .tag_len = data_ex->tag_len,
        .total_aad_len = data_ex->aad_len
    };
    hi_u32 length = data_ex->data.data_len;
    hi_void *src_virt_addr = HI_NULL;
    hi_void *dst_virt_addr = HI_NULL;
    hi_void *aad_virt_addr = HI_NULL;
    hi_u8 enc_tag[TAG_LEN] = {0};
    hi_u8 dec_tag[TAG_LEN] = {0};
    data_ex->data.symc_ctrl.param = (hi_void *)&ccm_gcm_config;

    sample_chk_expr_goto(cipher_alloc(&ccm_gcm_config.aad_buf, (hi_void **)&aad_virt_addr, data_ex->aad_len),
        HI_SUCCESS, CIPHER_FREE);
    sample_chk_expr_goto(cipher_alloc(&src_buf, (hi_void **)&src_virt_addr, length), HI_SUCCESS, CIPHER_FREE);
    sample_chk_expr_goto(cipher_alloc(&dst_buf, (hi_void **)&dst_virt_addr, length), HI_SUCCESS, CIPHER_FREE);

    ccm_gcm_config.aad_buf.virt_addr = aad_virt_addr;
    memcpy_s(aad_virt_addr, data_ex->aad_len, data_ex->aad, data_ex->aad_len);

    /* 1. cipher init */
    sample_chk_expr_goto(hi_mpi_cipher_symc_init(), HI_SUCCESS, CIPHER_FREE);

    /* 2. km init */
    sample_chk_expr_goto(hi_mpi_km_init(), HI_SUCCESS, CIPHER_DEINIT);

    /* 3. cipher create handle */
    sample_chk_expr_goto(hi_mpi_cipher_symc_create(&symc_handle, &data_ex->data.symc_attr), HI_SUCCESS, KM_DEINIT);

    /* 4. create keyslot handle */
    sample_chk_expr_goto(hi_mpi_keyslot_create(&keyslot_handle, KM_KEYSLOT_TYPE_MCIPHER), HI_SUCCESS,
        CIPHER_DESTROY);

    /* 5. attach cipher handle & kslot handle */
    sample_chk_expr_goto(hi_mpi_cipher_symc_attach(symc_handle, (hi_handle)keyslot_handle), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 6. set clear key */
    sample_chk_expr_goto(cipher_set_clear_key(keyslot_handle, data_ex->data.key, data_ex->data.key_len),
        HI_SUCCESS, KEYSLOT_DESTROY);

    /* 7. encrypt */
    /* 7.1 set config for encrypt */
    sample_chk_expr_goto(hi_mpi_cipher_symc_set_config(symc_handle, &data_ex->data.symc_ctrl), HI_SUCCESS,
        KEYSLOT_DESTROY);
    /* 7.2. encrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, data_ex->data.src_data, length),
        EOK, ret, HI_FAILURE, KEYSLOT_DESTROY);
    (hi_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(hi_mpi_cipher_symc_encrypt(symc_handle, &src_buf, &dst_buf, length), HI_SUCCESS,
        KEYSLOT_DESTROY);
    /* 7.3 get encrypt tag */
    sample_chk_expr_goto(hi_mpi_cipher_symc_get_tag(symc_handle, enc_tag, data_ex->tag_len), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 8. decrypt */
    /* 8.1 set config for decrypt */
    sample_chk_expr_goto(hi_mpi_cipher_symc_set_config(symc_handle, &data_ex->data.symc_ctrl), HI_SUCCESS,
        KEYSLOT_DESTROY);
    /* 8.2. decrypt */
    sample_chk_expr_goto_with_ret(memcpy_s(src_virt_addr, length, dst_virt_addr, length), EOK, ret,
        HI_FAILURE, KEYSLOT_DESTROY);
    (hi_void)memset_s(dst_virt_addr, length, 0, length);
    sample_chk_expr_goto(hi_mpi_cipher_symc_decrypt(symc_handle, &src_buf, &dst_buf, length), HI_SUCCESS,
        KEYSLOT_DESTROY);
    /* 8.3 get decrypt tag */
    sample_chk_expr_goto(hi_mpi_cipher_symc_get_tag(symc_handle, dec_tag, data_ex->tag_len), HI_SUCCESS,
        KEYSLOT_DESTROY);

    /* 9. compare encrypt and decrypt result & tag */
    sample_chk_expr_goto_with_ret(memcmp(dst_virt_addr, data_ex->data.src_data, length),
        0, ret, HI_FAILURE, KEYSLOT_DESTROY);
    sample_chk_expr_goto_with_ret(memcmp(enc_tag, dec_tag, data_ex->tag_len),
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
    cipher_free(&ccm_gcm_config.aad_buf, aad_virt_addr);
    return ret;
}

static hi_s32 sample_aes(hi_void)
{
    hi_u32 i;
    hi_s32 ret;
    hi_u32 num = (hi_u32)(sizeof(g_aes_data) / sizeof(g_aes_data[0]));
    for (i = 0; i < num; i++) {
        ret = sample_one_pack_crypto(&g_aes_data[i]);
        if (ret != HI_SUCCESS) {
            sample_err("************ test one pack %s failed ************\n", g_aes_data[i].name);
            return ret;
        }
        sample_log("************ test one pack %s success ************\n", g_aes_data[i].name);
    }
    
    num = (hi_u32)(sizeof(g_aes_data_ex) / sizeof(g_aes_data_ex[0]));
    for (i = 0; i < num; i++) {
        ret = sample_one_pack_crypto_ex(&g_aes_data_ex[i]);
        if (ret != HI_SUCCESS) {
            sample_err("************ test one pack ccm gcm %s failed ************\n", g_aes_data_ex[i].data.name);
            return ret;
        }
        sample_log("************ test one pack ccm gcm %s success ************\n", g_aes_data_ex[i].data.name);
    }
    return HI_SUCCESS;
}

static hi_s32 data_init(hi_void)
{
    hi_u32 i;
    hi_u32 num = 0;
    /* 1. init g_aes_data */
    num = (hi_u32)(sizeof(g_aes_data) / sizeof(g_aes_data[0]));
    for (i = 0; i < num; i++) {
        sample_chk_expr_return(get_random_data(g_aes_data[i].key, sizeof(g_aes_data[i].key)), HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_aes_data[i].symc_ctrl.iv, sizeof(g_aes_data[i].symc_ctrl.iv)),
            HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_aes_data[i].src_data, sizeof(g_aes_data[i].src_data)), HI_SUCCESS);
    }

    /* 2. init g_aes_data_ex */
    num = (hi_u32)(sizeof(g_aes_data_ex) / sizeof(g_aes_data_ex[0]));
    for (i = 0; i < num; i++) {
        sample_chk_expr_return(get_random_data(g_aes_data_ex[i].data.key, sizeof(g_aes_data_ex[i].data.key)),
            HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_aes_data_ex[i].data.symc_ctrl.iv,
            sizeof(g_aes_data_ex[i].data.symc_ctrl.iv)), HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_aes_data_ex[i].data.src_data, sizeof(g_aes_data_ex[i].data.src_data)),
            HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_aes_data_ex[i].aad, sizeof(g_aes_data_ex[i].aad)), HI_SUCCESS);
    }
    return HI_SUCCESS;
}

static hi_void data_deinit(hi_void)
{
    hi_u32 i;
    hi_u32 num = 0;
    /* 1. clear the key in g_aes_data */
    num = (hi_u32)(sizeof(g_aes_data) / sizeof(g_aes_data[0]));
    for (i = 0; i < num; i++) {
        memset_s(g_aes_data[i].key, sizeof(g_aes_data[i].key), 0, sizeof(g_aes_data[i].key));
    }

    /* 2. clear the key in g_aes_data_ex */
    num = (hi_u32)(sizeof(g_aes_data_ex) / sizeof(g_aes_data_ex[0]));
    for (i = 0; i < num; i++) {
        memset_s(g_aes_data_ex[i].data.key, sizeof(g_aes_data_ex[i].data.key), 0, sizeof(g_aes_data_ex[i].data.key));
    }
}

hi_s32 sample_symc_clearkey(hi_void)
{
    hi_s32 ret;
    sample_chk_expr_return(data_init(), HI_SUCCESS);
    sample_log("************ test symc clearkey ************\n");
    ret = sample_aes();
    if (ret != HI_SUCCESS) {
        return ret;
    }
    sample_log("************ test symc clearkey succeed ************\n");
    data_deinit();
    return HI_SUCCESS;
}

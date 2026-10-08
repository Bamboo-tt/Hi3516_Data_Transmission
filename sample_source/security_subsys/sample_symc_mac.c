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

typedef struct {
    const hi_char *mac_name;
    hi_u8 message[SYMC_MAC_DATA_LEN];
    hi_u32 message_length;
    hi_u8 key[MAX_KEY_LEN];
    hi_u32 key_length;
    crypto_symc_mac_attr mac_attr;
} symc_mac_data_t;

static symc_mac_data_t g_mac_data[] = {
    {
        .mac_name = "AES-128-CBC-MAC", .message_length = SYMC_MAC_DATA_LEN, .key_length = AES128_KEY_LEN,
        .mac_attr = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CBC_MAC,
            .is_long_term = HI_FALSE
        },
    },
    {
        .mac_name = "AES-128-CMAC", .message_length = SYMC_MAC_DATA_LEN, .key_length = AES128_KEY_LEN,
        .mac_attr = {
            .symc_alg = CRYPTO_SYMC_ALG_AES,
            .symc_key_length = CRYPTO_SYMC_KEY_128BIT,
            .work_mode = CRYPTO_SYMC_WORK_MODE_CMAC,
            .is_long_term = HI_TRUE
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
static hi_s32 sample_symc_mac_run(symc_mac_data_t *data)
{
    hi_s32 ret = HI_SUCCESS;
    hi_handle symc_handle;
    crypto_handle keyslot_handle;
    crypto_buf_attr src_buf = {0};
    hi_u8 mac[SYMC_MAC_LEN] = {0};
    hi_u32 mac_length = SYMC_MAC_LEN;

    /* 1. cipher init */
    sample_chk_expr_return(hi_mpi_cipher_symc_init(), HI_SUCCESS);

    /* 2. km init */
    sample_chk_expr_goto(hi_mpi_km_init(), HI_SUCCESS, CIPHER_DEINIT);

    /* 3. create keyslot handle */
    sample_chk_expr_goto(hi_mpi_keyslot_create(&keyslot_handle, KM_KEYSLOT_TYPE_MCIPHER), HI_SUCCESS,
        KM_DEINIT);

    /* 4. set clear key */
    sample_chk_expr_goto(cipher_set_clear_key(keyslot_handle, data->key, data->key_length), HI_SUCCESS,
        KEYSLOT_DESTROY);
    data->mac_attr.keyslot_chn = (hi_handle)keyslot_handle;

    /* 5. set mac attr */
    sample_chk_expr_goto(hi_mpi_cipher_mac_start(&symc_handle, &data->mac_attr), HI_SUCCESS, KEYSLOT_DESTROY);

    /* 6. mac process */
    src_buf.virt_addr = data->message;
    sample_chk_expr_goto(hi_mpi_cipher_mac_update(symc_handle, &src_buf, data->message_length), HI_SUCCESS,
        CIPHER_DESTROY);

    /* 7. finish mac calculation and get the mac value */
    sample_chk_expr_goto(hi_mpi_cipher_mac_finish(symc_handle, mac, &mac_length), HI_SUCCESS, CIPHER_DESTROY);

    hi_mpi_keyslot_destroy(keyslot_handle);
    hi_mpi_km_deinit();
    hi_mpi_cipher_symc_deinit();
    return ret;

CIPHER_DESTROY:
    hi_mpi_cipher_symc_destroy(symc_handle);
KEYSLOT_DESTROY:
    hi_mpi_keyslot_destroy(keyslot_handle);
KM_DEINIT:
    hi_mpi_km_deinit();
CIPHER_DEINIT:
    hi_mpi_cipher_symc_deinit();
    return ret;
}

static hi_s32 sample_symc_mac_cal(hi_void)
{
    hi_u32 i;
    hi_s32 ret;
    hi_u32 num = (hi_u32)(sizeof(g_mac_data) / sizeof(g_mac_data[0]));
    for (i = 0; i < num; i++) {
        ret = sample_symc_mac_run(&g_mac_data[i]);
        if (ret != HI_SUCCESS) {
            sample_err("************ test symc mac %s failed ************\n", g_mac_data[i].mac_name);
            return ret;
        }
        sample_log("************ test symc mac %s success ************\n", g_mac_data[i].mac_name);
    }
    return HI_SUCCESS;
}

static hi_s32 data_init(hi_void)
{
    hi_u32 i;
    hi_u32 num = 0;
    /* 1. init g_mac_data */
    num = (hi_u32)(sizeof(g_mac_data) / sizeof(g_mac_data[0]));
    for (i = 0; i < num; i++) {
        sample_chk_expr_return(get_random_data(g_mac_data[i].key, sizeof(g_mac_data[i].key)), HI_SUCCESS);
        sample_chk_expr_return(get_random_data(g_mac_data[i].message, sizeof(g_mac_data[i].message)), HI_SUCCESS);
    }
    return HI_SUCCESS;
}

static hi_void data_deinit(hi_void)
{
    hi_u32 i;
    hi_u32 num = 0;
    /* 1. clear the key in g_mac_data */
    num = (hi_u32)(sizeof(g_mac_data) / sizeof(g_mac_data[0]));
    for (i = 0; i < num; i++) {
        memset_s(g_mac_data[i].key, sizeof(g_mac_data[i].key), 0, sizeof(g_mac_data[i].key));
    }
}

hi_s32 sample_symc_mac(hi_void)
{
    hi_s32 ret;
    sample_chk_expr_return(data_init(), HI_SUCCESS);
    sample_log("************ test symc mac ************\n");
    ret = sample_symc_mac_cal();
    if (ret != HI_SUCCESS) {
        return ret;
    }
    sample_log("************ test symc mac succeed ************\n");
    data_deinit();
    return HI_SUCCESS;
}

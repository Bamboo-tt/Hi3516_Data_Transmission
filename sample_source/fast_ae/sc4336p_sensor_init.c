/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdio.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef OT_GPIO_I2C
#include "gpioi2c_ex.h"
#else
#include "ot_i2c.h"
#endif

#include "securec.h"
#include "hi_mpi_isp.h"
#include "ot_math.h"

#define I2C_DEV_FILE_NUM     16
#define I2C_BUF_NUM          8
#define I2C_DEV_SENSOR       0

#define SC4336P_I2C_ADDR    0x60
#define SC4336P_ADDR_BYTE   2
#define SC4336P_DATA_BYTE   1

#define I2C_MSG_CNT         2
#define I2C_READ_BUF_LEN    4
#define I2C_RDWR       0x0707
#define I2C_READ_STATUS_OK  2

#define high_8bits(x) (((x) & 0xff00) >> 8) /* shift 8 bit */
#define low_8bits(x)  ((x) & 0x00ff)
#define lower_4bits(x)  (((x) & 0x000F) << 4)
#define higher_8bits(x) (((x) & 0x0FF0) >> 4)
#define higher_4bits(x) (((x) & 0xF000) >> 12)

#ifndef clip3
#define clip3(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

/* sc4336p Register Address */
#define SC4336P_EXPO_H_ADDR          0x3E00
#define SC4336P_EXPO_M_ADDR          0x3E01
#define SC4336P_EXPO_L_ADDR          0x3E02
#define SC4336P_AGAIN_L_ADDR         0x3E09
#define SC4336P_DGAIN_H_ADDR         0x3E06
#define SC4336P_DGAIN_L_ADDR         0x3E07
#define SC4336P_VMAX_H_ADDR          0x320E
#define SC4336P_VMAX_L_ADDR          0x320F

#define SC4336P_AGAIN_NODE_NUM               6
#define SC4336P_AGAIN_ADDR_INDEX_NODE_NUM    6
#define SC4336P_DGAIN_NODE_NUM               128
#define SC4336P_DGAIN_ADDR_INDEX_NODE_NUM    128

#define sns_check_pointer_void_return(ptr) \
    do { \
        if ((ptr) == TD_NULL) { \
            isp_err_trace("Null Pointer!\n"); \
            return; \
        } \
    } while (0)

static int g_fd = -1;

struct i2c_rdwr_ioctl_data {
    struct i2c_msg  *msgs;  /* pointers to i2c_msgs */
    __u32 nmsgs;            /* number of i2c_msgs */
};

struct i2c_rdwr_args {
    unsigned int i2c_num;
    unsigned int dev_addr;
    unsigned int reg_addr;
    unsigned int reg_addr_end;
    unsigned int reg_width;
    unsigned int data_width;
    unsigned int reg_step;
};

static hi_u32 i2c_ioc_init(struct i2c_rdwr_ioctl_data *rdwr, unsigned char *buf,
    size_t buf_size, struct i2c_rdwr_args args)
{
    if (memset_s(buf, buf_size, 0, I2C_READ_BUF_LEN) != EOK) {
        printf("memset_s fail!\n");
        return -1;
    }
    rdwr->msgs[0].addr = args.dev_addr;
    rdwr->msgs[0].flags = 0;
    rdwr->msgs[0].len = args.reg_width;
    rdwr->msgs[0].buf = buf;

    rdwr->msgs[1].addr = args.dev_addr;
    rdwr->msgs[1].flags = 0;
    rdwr->msgs[1].flags |= I2C_M_RD;
    rdwr->msgs[1].len = args.data_width;
    rdwr->msgs[1].buf = buf;

    return 0;
}

static int sensor_sc4336p_i2c_init()
{
    int ret;
    char dev_file[I2C_DEV_FILE_NUM] = {0};
    if (g_fd >= 0) {
        printf("sensor i2c fd already be created\n");
        return TD_SUCCESS;
    }
#ifdef OT_GPIO_I2C
    hi_unused(ret);
    g_fd = open("/dev/gpioi2c_ex", O_RDONLY, S_IRUSR);
    if (g_fd < 0) {
        printf("Open gpioi2c_ex error!\n");
        return TD_FAILURE;
    }
#else
    (td_void)snprintf_s(dev_file, sizeof(dev_file), sizeof(dev_file) - 1, "/dev/i2c-%d", I2C_DEV_SENSOR);

    g_fd = open(dev_file, O_RDWR, S_IRUSR | S_IWUSR);
    if (g_fd < 0) {
        printf("Open /dev/ot_i2c_drv-%d error!\n", I2C_DEV_SENSOR);
        return TD_FAILURE;
    }

    ret = ioctl(g_fd, OT_I2C_SLAVE_FORCE, (SC4336P_I2C_ADDR >> 1));
    if (ret < 0) {
        printf("I2C_SLAVE_FORCE error!\n");
        close(g_fd);
        g_fd = -1;
        return ret;
    }
#endif

    return TD_SUCCESS;
}

int sensor_sc4336p_i2c_exit()
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
        return TD_SUCCESS;
    }
    return TD_FAILURE;
}

static td_u32 sensor_sc4336p_read_register(td_u32 addr)
{
    if (g_fd < 0) {
        printf("sc4336p_read_register fd not opened!\n");
        return HI_FAILURE;
    }

    struct i2c_rdwr_args args;
    hi_u32 cur_addr;
    hi_u32 data;
    unsigned char buf[I2C_READ_BUF_LEN];
    static struct i2c_rdwr_ioctl_data rdwr;
    static struct i2c_msg msg[I2C_MSG_CNT];

    rdwr.msgs = &msg[0];
    rdwr.nmsgs = (__u32)I2C_MSG_CNT;
    args.i2c_num = I2C_DEV_SENSOR;
    args.dev_addr = (SC4336P_I2C_ADDR >> 1);
    args.reg_addr = addr;
    args.reg_addr_end = addr;
    args.reg_width = SC4336P_ADDR_BYTE;
    args.data_width = SC4336P_DATA_BYTE;
    args.reg_step = 1;
    if (i2c_ioc_init(&rdwr, buf, sizeof(buf), args) != 0) {
        return -1;
    }

    for (cur_addr = args.reg_addr; cur_addr <= args.reg_addr_end; cur_addr += args.reg_step) {
        if (args.reg_width == 2) {  /* 2 byte */
            buf[0] = (cur_addr >> 8) & 0xff;  /* shift 8 */
            buf[1] = cur_addr & 0xff;
        } else {
            buf[0] = cur_addr & 0xff;
        }

        if (ioctl(g_fd, I2C_RDWR, &rdwr) != I2C_READ_STATUS_OK) {
            printf("CMD_I2C_READ error!\n");
            return -1;
        }

        if (args.data_width == 2) {  /* 2 byte */
            data = buf[1] | (buf[0] << 8);  /* shift 8 */
        } else {
            data = buf[0];
        }
    }

    return data;
}

static td_s32 sensor_sc4336p_write_register(td_u32 addr, td_u32 data)
{
    td_s32 ret;
    if (g_fd < 0) {
        return TD_SUCCESS;
    }

#ifdef OT_GPIO_I2C
    i2c_data.dev_addr = SC4336P_I2C_ADDR;
    i2c_data.reg_addr = addr;
    i2c_data.addr_byte_num = SC4336P_ADDR_BYTE;
    i2c_data.data = data;
    i2c_data.data_byte_num = SC4336P_DATA_BYTE;

    ret = ioctl(g_fd, GPIO_I2C_WRITE, &i2c_data);
    if (ret) {
        isp_err_trace("GPIO-I2C write failed!\n");
        return ret;
    }
#else
    td_u32 idx = 0;
    td_u8 buf[I2C_BUF_NUM];

    buf[idx] = (addr >> 8) & 0xff;  /* shift 8 */
    idx++;
    buf[idx] = addr & 0xff;
    idx++;

    buf[idx] = data & 0xff;

    ret = write(g_fd, buf, SC4336P_ADDR_BYTE + SC4336P_DATA_BYTE);
    if (ret < 0) {
        isp_err_trace("I2C_WRITE error! ret = 0x%x\n", ret);
        return TD_FAILURE;
    }

#endif
    return TD_SUCCESS;
}

static void delay_ms(int ms)
{
    usleep(ms * 1000); /* 1ms: 1000us */
    return;
}

static td_void sensor_sc4336p_write_exp(td_u32 sns_exp)
{
    td_s32 ret = TD_SUCCESS;

    sns_exp = MAX2(MIN2(sns_exp, 1490), 2); /* sns_time [2, 1490] */

    ret += sensor_sc4336p_write_register(SC4336P_EXPO_L_ADDR, lower_4bits(sns_exp));
    ret += sensor_sc4336p_write_register(SC4336P_EXPO_M_ADDR, higher_8bits(sns_exp));
    ret += sensor_sc4336p_write_register(SC4336P_EXPO_H_ADDR, higher_4bits(sns_exp));
    if (ret < 0) {
        printf("sc4336p_write_sns_exp error!\n");
        return;
    }
    return;
}

static td_u32 again_index[SC4336P_AGAIN_NODE_NUM] = {
    1024,  2048,  4096,  8192, 16384, 32768
};

static td_u32 dgain_index[SC4336P_DGAIN_NODE_NUM] = {
    1024, 1056, 1088, 1120,  1152, 1184, 1216, 1248,  1280, 1312, 1344, 1376,
    1408, 1440, 1472, 1504,  1536, 1568, 1600, 1632,  1664, 1696, 1728, 1760,
    1792, 1824, 1856, 1888,  1920, 1952, 1984, 2016,  2048, 2112, 2176, 2240,
    2304, 2368, 2432, 2496,  2560, 2624, 2688, 2752,  2816, 2880, 2944, 3008,
    3072, 3136, 3200, 3264,  3328, 3392, 3456, 3520,  3584, 3648, 3712, 3776,
    3840, 3904, 3968, 4032,  4096, 4224, 4352, 4480,  4608, 4736, 4864, 4992,
    5120, 5248, 5376, 5504,  5632, 5760, 5888, 6016,  6144, 6272, 6400, 6528,
    6656, 6784, 6912, 7040,  7168, 7296, 7424, 7552,  7680, 7808, 7936, 8064,
    8192, 8448, 8704, 8960,  9216, 9472, 9728, 9984, 10240, 10496, 10752, 11008,
    11264, 11520, 11776, 12032, 12288, 12544, 12800, 13056, 13312, 13568, 13824, 14080,
    14336, 14592, 14848, 15104, 15360, 15616, 15872, 16128
};


static td_u32 again_table[SC4336P_AGAIN_ADDR_INDEX_NODE_NUM] = {
    0x0, 0x8, 0x9, 0xb, 0x0f, 0x1f
};

static td_u32 dgain_table[SC4336P_DGAIN_ADDR_INDEX_NODE_NUM] = {
    0x0080, 0x0084, 0x0088, 0x008C, 0x0090, 0x0094, 0x0098, 0x009C, 0x00A0, 0x00A4, 0x00A8, 0x00AC,
    0x00B0, 0x00B4, 0x00B8, 0x00BC, 0x00C0, 0x00C4, 0x00C8, 0x00CC, 0x00D0, 0x00D4, 0x00D8, 0x00DC,
    0x00E0, 0x00E4, 0x00E8, 0x00EC, 0x00F0, 0x00F4, 0x00F8, 0x00FC, 0x0180, 0x0184, 0x0188, 0x018C,
    0x0190, 0x0194, 0x0198, 0x019C, 0x01A0, 0x01A4, 0x01A8, 0x01AC, 0x01B0, 0x01B4, 0x01B8, 0x01BC,
    0x01C0, 0x01C4, 0x01C8, 0x01CC, 0x01D0, 0x01D4, 0x01D8, 0x01DC, 0x01E0, 0x01E4, 0x01E8, 0x01EC,
    0x01F0, 0x01F4, 0x01F8, 0x01FC, 0x0380, 0x0384, 0x0388, 0x038C, 0x0390, 0x0394, 0x0398, 0x039C,
    0x03A0, 0x03A4, 0x03A8, 0x03AC, 0x03B0, 0x03B4, 0x03B8, 0x03BC, 0x03C0, 0x03C4, 0x03C8, 0x03CC,
    0x03D0, 0x03D4, 0x03D8, 0x03DC, 0x03E0, 0x03E4, 0x03E8, 0x03EC, 0x03F0, 0x03F4, 0x03F8, 0x03FC,
    0x0780, 0x0784, 0x0788, 0x078C, 0x0790, 0x0794, 0x0798, 0x079C, 0x07A0, 0x07A4, 0x07A8, 0x07AC,
    0x07B0, 0x07B4, 0x07B8, 0x07BC, 0x07C0, 0x07C4, 0x07C8, 0x07CC, 0x07D0, 0x07D4, 0x07D8, 0x07DC,
    0x07E0, 0x07E4, 0x07E8, 0x07EC, 0x07F0, 0x07F4, 0x07F8, 0x07FC,
};

static td_void cmos_again_calc_table(td_u32 *again_lin, td_u32 *again_db)
{
    int i;
    sns_check_pointer_void_return(again_lin);
    sns_check_pointer_void_return(again_db);

    if (*again_lin >= again_index[SC4336P_AGAIN_ADDR_INDEX_NODE_NUM - 1]) {
        *again_lin = again_index[SC4336P_AGAIN_ADDR_INDEX_NODE_NUM - 1];
        *again_db = again_table[SC4336P_AGAIN_NODE_NUM - 1];
        return;
    }

    for (i = 1; i < SC4336P_AGAIN_NODE_NUM; i++) {
        if (*again_lin == again_index[i]) {
            *again_lin = again_index[i];
            *again_db = again_table[i];
            break;
        }
        if (*again_lin < again_index[i]) {
            *again_lin = again_index[i - 1];
            *again_db = again_table[i - 1];
            break;
        }
    }

    return;
}

static td_void cmos_again_get_table(td_u32 *again_db, td_u32 *again_lin)
{
    int i;
    sns_check_pointer_void_return(again_lin);
    sns_check_pointer_void_return(again_db);

    for (i = 0; i < SC4336P_AGAIN_NODE_NUM; i++) {
        if (*again_db == again_table[i]) {
            *again_lin = again_index[i];
            break;
        }
        if (*again_db < again_table[i]) {
            *again_lin = again_index[i - 1];
            break;
        }
    }
}

static td_void cmos_dgain_calc_table(td_u32 *dgain_lin, td_u32 *dgain_db)
{
    int i;

    sns_check_pointer_void_return(dgain_lin);
    sns_check_pointer_void_return(dgain_db);

    if (*dgain_lin >= dgain_index[SC4336P_DGAIN_ADDR_INDEX_NODE_NUM - 1]) {
        *dgain_lin = dgain_index[SC4336P_DGAIN_ADDR_INDEX_NODE_NUM - 1];
        *dgain_db = dgain_table[SC4336P_DGAIN_NODE_NUM - 1];
        return;
    }

    for (i = 1; i < SC4336P_DGAIN_NODE_NUM; i++) {
        if (*dgain_lin <= dgain_index[i]) {
            *dgain_lin = dgain_index[i - 1];
            *dgain_db = dgain_table[i - 1];
            break;
        }
    }
    return;
}

static td_void cmos_dgain_get_table(td_u32 *dgain_db, td_u32 *dgain_lin)
{
    int i;
    sns_check_pointer_void_return(dgain_lin);
    sns_check_pointer_void_return(dgain_db);

    for (i = 0; i < SC4336P_DGAIN_NODE_NUM; i++) {
        if (*dgain_db == dgain_table[i]) {
            *dgain_lin = dgain_index[i];
            break;
        }
        if (*dgain_db < dgain_table[i]) {
            *dgain_lin = dgain_index[i - 1];
            break;
        }
    }
}

static td_void sensor_sc4336p_write_gain(td_u32 sns_again, td_u32 sns_dgain)
{
    td_s32 ret = TD_SUCCESS;
    td_u32 again_reg, dgain_reg;
    td_u8 reg_0x3e09;
    td_u8 reg_0x3e07;
    td_u8 reg_0x3e06;

    again_reg = 0;
    dgain_reg = 0;
    cmos_again_calc_table(&sns_again, &again_reg);
    cmos_dgain_calc_table(&sns_dgain, &dgain_reg);

    reg_0x3e06 = high_8bits(dgain_reg);
    reg_0x3e07 = low_8bits (dgain_reg);
    reg_0x3e09 = low_8bits (again_reg);

    ret += sensor_sc4336p_write_register(SC4336P_DGAIN_H_ADDR, reg_0x3e06);
    ret += sensor_sc4336p_write_register(SC4336P_DGAIN_L_ADDR, reg_0x3e07);
    ret += sensor_sc4336p_write_register(SC4336P_AGAIN_L_ADDR, reg_0x3e09);
    if (ret < 0) {
        printf("sc4336p_write_sns_gain error!\n");
        return;
    }

    return;
}

static td_void sensor_sc4336p_linear_4m30_10bit_init();

td_void sensor_sc4336p_init(td_u32 sns_exp, td_u32 sns_again, td_u32 sns_dgain)
{
    td_s32 ret;
    delay_ms(1);
    ret = sensor_sc4336p_i2c_init();
    if (ret != TD_SUCCESS) {
        isp_err_trace("i2c init failed!\n");
        return;
    }

    sensor_sc4336p_linear_4m30_10bit_init();
    sensor_sc4336p_write_exp(sns_exp);
    sensor_sc4336p_write_gain(sns_again, sns_dgain);

    delay_ms(5); /* delay 5 ms */
    sensor_sc4336p_write_register(0x0100, 0x01);

    return;
}

td_void sensor_sc4336p_read_exp(td_u32 *sns_exp)
{
    td_u32 exp_reg_low, exp_reg_mid, exp_reg_high;

    exp_reg_low = sensor_sc4336p_read_register(SC4336P_EXPO_L_ADDR);
    exp_reg_mid = sensor_sc4336p_read_register(SC4336P_EXPO_M_ADDR);
    exp_reg_high = sensor_sc4336p_read_register(SC4336P_EXPO_H_ADDR);

    *sns_exp = ((exp_reg_mid & 0xff) << 4) | ((exp_reg_low & 0xf0) >> 4) | /* shift 4 */
         ((exp_reg_high & 0xf) << 12); /* shift 12 */

    return;
}

td_void sensor_sc4336p_read_gain(td_u32 *sns_again, td_u32 *sns_dgain)
{
    td_u32 reg_0x3e06, reg_0x3e07, reg_0x3e09;
    td_u32 again_reg, dgain_reg;

    reg_0x3e09 = sensor_sc4336p_read_register(SC4336P_AGAIN_L_ADDR);
    reg_0x3e07 = sensor_sc4336p_read_register(SC4336P_DGAIN_L_ADDR);
    reg_0x3e06 = sensor_sc4336p_read_register(SC4336P_DGAIN_H_ADDR);

    again_reg = (reg_0x3e09 & 0xFF);
    dgain_reg = ((reg_0x3e06 & 0xFF) << 8) | (reg_0x3e07 & 0xFF); /* bits shift 8 */

    cmos_again_get_table(&again_reg, sns_again);
    cmos_dgain_get_table(&dgain_reg, sns_dgain);

    return;
}

void sensor_sc4336p_exit()
{
    td_s32 ret;

    ret = sensor_sc4336p_i2c_exit();
    if (ret != TD_SUCCESS) {
        isp_err_trace("SC4336P exit failed!\n");
    }

    return;
}

static td_s32 sensor_sc4336p_linear_4m30_10bit_init_part1()
{   // Cleaned_0x01_FT_SC4336P_MIPI_27Minput_2lane_10bit_630Mbps_2560x1440_30fps_LDO_1.35v.ini
    td_s32 ret = TD_SUCCESS;
    ret += sensor_sc4336p_write_register(0x0103, 0x01);
    delay_ms(1); /* 1ms */
    ret += sensor_sc4336p_write_register(0x0100, 0x00);
    ret += sensor_sc4336p_write_register(0x36e9, 0x80);
    ret += sensor_sc4336p_write_register(0x37f9, 0x80);
    ret += sensor_sc4336p_write_register(0x301f, 0x01);
    ret += sensor_sc4336p_write_register(0x30b8, 0x44);
    ret += sensor_sc4336p_write_register(0x3253, 0x10);
    ret += sensor_sc4336p_write_register(0x3301, 0x0a);
    ret += sensor_sc4336p_write_register(0x3302, 0xff);
    ret += sensor_sc4336p_write_register(0x3305, 0x00);
    ret += sensor_sc4336p_write_register(0x3306, 0x90);
    ret += sensor_sc4336p_write_register(0x3308, 0x08);
    ret += sensor_sc4336p_write_register(0x330a, 0x01);
    ret += sensor_sc4336p_write_register(0x330b, 0xb0);
    ret += sensor_sc4336p_write_register(0x330d, 0xf0);
    ret += sensor_sc4336p_write_register(0x3314, 0x14);
    ret += sensor_sc4336p_write_register(0x3333, 0x10);
    ret += sensor_sc4336p_write_register(0x3334, 0x40);
    ret += sensor_sc4336p_write_register(0x335e, 0x06);
    ret += sensor_sc4336p_write_register(0x335f, 0x0a);
    ret += sensor_sc4336p_write_register(0x3364, 0x5e);
    ret += sensor_sc4336p_write_register(0x337d, 0x0e);
    ret += sensor_sc4336p_write_register(0x338f, 0x20);
    ret += sensor_sc4336p_write_register(0x3390, 0x08);
    ret += sensor_sc4336p_write_register(0x3391, 0x09);
    ret += sensor_sc4336p_write_register(0x3392, 0x0f);
    ret += sensor_sc4336p_write_register(0x3393, 0x18);
    ret += sensor_sc4336p_write_register(0x3394, 0x60);
    ret += sensor_sc4336p_write_register(0x3395, 0xff);
    ret += sensor_sc4336p_write_register(0x3396, 0x08);
    ret += sensor_sc4336p_write_register(0x3397, 0x09);
    ret += sensor_sc4336p_write_register(0x3398, 0x0f);
    ret += sensor_sc4336p_write_register(0x3399, 0x0a);
    ret += sensor_sc4336p_write_register(0x339a, 0x18);
    ret += sensor_sc4336p_write_register(0x339b, 0x60);
    ret += sensor_sc4336p_write_register(0x339c, 0xff);
    ret += sensor_sc4336p_write_register(0x33a2, 0x04);
    ret += sensor_sc4336p_write_register(0x33ad, 0x0c);
    ret += sensor_sc4336p_write_register(0x33b2, 0x40);
    ret += sensor_sc4336p_write_register(0x33b3, 0x30);
    ret += sensor_sc4336p_write_register(0x33f8, 0x00);
    ret += sensor_sc4336p_write_register(0x33f9, 0xb0);

    return ret;
}

static td_s32 sensor_sc4336p_linear_4m30_10bit_init_part2()
{
    td_s32 ret = TD_SUCCESS;
    ret += sensor_sc4336p_write_register(0x33fa, 0x00);
    ret += sensor_sc4336p_write_register(0x33fb, 0xf8);
    ret += sensor_sc4336p_write_register(0x33fc, 0x09);
    ret += sensor_sc4336p_write_register(0x33fd, 0x1f);
    ret += sensor_sc4336p_write_register(0x349f, 0x03);
    ret += sensor_sc4336p_write_register(0x34a6, 0x09);
    ret += sensor_sc4336p_write_register(0x34a7, 0x1f);
    ret += sensor_sc4336p_write_register(0x34a8, 0x28);
    ret += sensor_sc4336p_write_register(0x34a9, 0x28);
    ret += sensor_sc4336p_write_register(0x34aa, 0x01);
    ret += sensor_sc4336p_write_register(0x34ab, 0xe0);
    ret += sensor_sc4336p_write_register(0x34ac, 0x02);
    ret += sensor_sc4336p_write_register(0x34ad, 0x28);
    ret += sensor_sc4336p_write_register(0x34f8, 0x1f);
    ret += sensor_sc4336p_write_register(0x34f9, 0x20);
    ret += sensor_sc4336p_write_register(0x3630, 0xc0);
    ret += sensor_sc4336p_write_register(0x3631, 0x84);
    ret += sensor_sc4336p_write_register(0x3632, 0x54);
    ret += sensor_sc4336p_write_register(0x3633, 0x44);
    ret += sensor_sc4336p_write_register(0x3637, 0x49);
    ret += sensor_sc4336p_write_register(0x363f, 0xc0);
    ret += sensor_sc4336p_write_register(0x3641, 0x28);
    ret += sensor_sc4336p_write_register(0x3670, 0x56);
    ret += sensor_sc4336p_write_register(0x3674, 0xb0);
    ret += sensor_sc4336p_write_register(0x3675, 0xa0);
    ret += sensor_sc4336p_write_register(0x3676, 0xa0);
    ret += sensor_sc4336p_write_register(0x3677, 0x84);
    ret += sensor_sc4336p_write_register(0x3678, 0x88);
    ret += sensor_sc4336p_write_register(0x3679, 0x8d);
    ret += sensor_sc4336p_write_register(0x367c, 0x09);
    ret += sensor_sc4336p_write_register(0x367d, 0x0b);
    ret += sensor_sc4336p_write_register(0x367e, 0x08);
    ret += sensor_sc4336p_write_register(0x367f, 0x0f);
    ret += sensor_sc4336p_write_register(0x3696, 0x24);
    ret += sensor_sc4336p_write_register(0x3697, 0x34);
    ret += sensor_sc4336p_write_register(0x3698, 0x34);
    ret += sensor_sc4336p_write_register(0x36a0, 0x0f);
    ret += sensor_sc4336p_write_register(0x36a1, 0x1f);
    ret += sensor_sc4336p_write_register(0x36b0, 0x81);
    ret += sensor_sc4336p_write_register(0x36b1, 0x83);
    ret += sensor_sc4336p_write_register(0x36b2, 0x85);
    ret += sensor_sc4336p_write_register(0x36b3, 0x8b);
    ret += sensor_sc4336p_write_register(0x36b4, 0x09);
    ret += sensor_sc4336p_write_register(0x36b5, 0x0b);
    return ret;
}

static td_s32 sensor_sc4336p_linear_4m30_10bit_init_part3()
{
    td_s32 ret = TD_SUCCESS;
    ret += sensor_sc4336p_write_register(0x36b6, 0x0f);
    ret += sensor_sc4336p_write_register(0x370f, 0x01);
    ret += sensor_sc4336p_write_register(0x3722, 0x09);
    ret += sensor_sc4336p_write_register(0x3724, 0x21);
    ret += sensor_sc4336p_write_register(0x3771, 0x09);
    ret += sensor_sc4336p_write_register(0x3772, 0x05);
    ret += sensor_sc4336p_write_register(0x3773, 0x05);
    ret += sensor_sc4336p_write_register(0x377a, 0x0f);
    ret += sensor_sc4336p_write_register(0x377b, 0x1f);
    ret += sensor_sc4336p_write_register(0x3905, 0x8c);
    ret += sensor_sc4336p_write_register(0x391d, 0x02);
    ret += sensor_sc4336p_write_register(0x391f, 0x49);
    ret += sensor_sc4336p_write_register(0x3926, 0x21);
    ret += sensor_sc4336p_write_register(0x3933, 0x80);
    ret += sensor_sc4336p_write_register(0x3934, 0x03);
    ret += sensor_sc4336p_write_register(0x3937, 0x7b);
    ret += sensor_sc4336p_write_register(0x3939, 0x00);
    ret += sensor_sc4336p_write_register(0x393a, 0x00);
    ret += sensor_sc4336p_write_register(0x39dc, 0x02);
    ret += sensor_sc4336p_write_register(0x3e00, 0x00);
    ret += sensor_sc4336p_write_register(0x3e01, 0x5d);
    ret += sensor_sc4336p_write_register(0x3e02, 0x40);
    ret += sensor_sc4336p_write_register(0x440d, 0x10);
    ret += sensor_sc4336p_write_register(0x440e, 0x01);
    ret += sensor_sc4336p_write_register(0x4509, 0x28);
    ret += sensor_sc4336p_write_register(0x450d, 0x32);
    ret += sensor_sc4336p_write_register(0x5000, 0x06);
    ret += sensor_sc4336p_write_register(0x5780, 0x76);
    ret += sensor_sc4336p_write_register(0x5784, 0x10);
    ret += sensor_sc4336p_write_register(0x5785, 0x04);
    ret += sensor_sc4336p_write_register(0x5787, 0x0a);
    ret += sensor_sc4336p_write_register(0x5788, 0x0a);
    ret += sensor_sc4336p_write_register(0x5789, 0x08);
    ret += sensor_sc4336p_write_register(0x578a, 0x0a);
    ret += sensor_sc4336p_write_register(0x578b, 0x0a);
    ret += sensor_sc4336p_write_register(0x578c, 0x08);
    ret += sensor_sc4336p_write_register(0x578d, 0x40);
    ret += sensor_sc4336p_write_register(0x5790, 0x08);
    ret += sensor_sc4336p_write_register(0x5791, 0x04);
    ret += sensor_sc4336p_write_register(0x5792, 0x04);
    ret += sensor_sc4336p_write_register(0x5793, 0x08);
    return ret;
}

static td_s32 sensor_sc4336p_linear_4m30_10bit_init_part4()
{
    td_s32 ret = TD_SUCCESS;
    ret += sensor_sc4336p_write_register(0x5794, 0x04);
    ret += sensor_sc4336p_write_register(0x5795, 0x04);
    ret += sensor_sc4336p_write_register(0x5799, 0x46);
    ret += sensor_sc4336p_write_register(0x579a, 0x77);
    ret += sensor_sc4336p_write_register(0x57a1, 0x04);
    ret += sensor_sc4336p_write_register(0x57a8, 0xd2);
    ret += sensor_sc4336p_write_register(0x57aa, 0x2a);
    ret += sensor_sc4336p_write_register(0x57ab, 0x7f);
    ret += sensor_sc4336p_write_register(0x57ac, 0x00);
    ret += sensor_sc4336p_write_register(0x57ad, 0x00);
    ret += sensor_sc4336p_write_register(0x57d9, 0x46);
    ret += sensor_sc4336p_write_register(0x57da, 0x77);
    ret += sensor_sc4336p_write_register(0x59e2, 0x08);
    ret += sensor_sc4336p_write_register(0x59e3, 0x03);
    ret += sensor_sc4336p_write_register(0x59e4, 0x00);
    ret += sensor_sc4336p_write_register(0x59e5, 0x10);
    ret += sensor_sc4336p_write_register(0x59e6, 0x06);
    ret += sensor_sc4336p_write_register(0x59e7, 0x00);
    ret += sensor_sc4336p_write_register(0x59e8, 0x08);
    ret += sensor_sc4336p_write_register(0x59e9, 0x02);
    ret += sensor_sc4336p_write_register(0x59ea, 0x00);
    ret += sensor_sc4336p_write_register(0x59eb, 0x10);
    ret += sensor_sc4336p_write_register(0x59ec, 0x04);
    ret += sensor_sc4336p_write_register(0x59ed, 0x00);
    ret += sensor_sc4336p_write_register(0x5ae0, 0xfe);
    ret += sensor_sc4336p_write_register(0x5ae1, 0x40);
    ret += sensor_sc4336p_write_register(0x5ae2, 0x38);
    ret += sensor_sc4336p_write_register(0x5ae3, 0x30);
    ret += sensor_sc4336p_write_register(0x5ae4, 0x28);
    ret += sensor_sc4336p_write_register(0x5ae5, 0x38);
    ret += sensor_sc4336p_write_register(0x5ae6, 0x30);
    ret += sensor_sc4336p_write_register(0x5ae7, 0x28);
    ret += sensor_sc4336p_write_register(0x5ae8, 0x3f);
    ret += sensor_sc4336p_write_register(0x5ae9, 0x34);
    ret += sensor_sc4336p_write_register(0x5aea, 0x2c);
    ret += sensor_sc4336p_write_register(0x5aeb, 0x3f);
    ret += sensor_sc4336p_write_register(0x5aec, 0x34);
    ret += sensor_sc4336p_write_register(0x5aed, 0x2c);
    ret += sensor_sc4336p_write_register(0x36e9, 0x44);
    ret += sensor_sc4336p_write_register(0x37f9, 0x44);
    ret += sensor_sc4336p_write_register(0x3221, 0x0); /* mirror and flip */

    return ret;
}


static td_void sensor_sc4336p_linear_4m30_10bit_init()
{
    td_s32 ret = TD_SUCCESS;
    ret += sensor_sc4336p_linear_4m30_10bit_init_part1();
    ret += sensor_sc4336p_linear_4m30_10bit_init_part2();
    ret += sensor_sc4336p_linear_4m30_10bit_init_part3();
    ret += sensor_sc4336p_linear_4m30_10bit_init_part4();
    if (ret != TD_SUCCESS) {
        isp_err_trace("sc4336p write register failed!\n");
        return;
    }

    printf("===================================================================================\n");
    printf("vi_pipe:%d,== SC4336P_MIPI_27Minput_2lane_10bit_630Mbps_2560x1440_30fps Init OK! ==\n", 0);
    printf("===================================================================================\n");
    return;
}

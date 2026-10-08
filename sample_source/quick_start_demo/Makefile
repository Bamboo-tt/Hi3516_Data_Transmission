include ../Makefile.param

########### SRCS & INC ############
SMP_SRCS := $(wildcard $(PWD)/*.c)

ifeq (${CONFIG_VO_SUPPORT},y)
CFLAGS += -DCONFIG_VO_SUPPORT
endif

ifeq (${CONFIG_SENSOR_INIT_IN_UBOOT},y)
CFLAGS += -DCONFIG_SENSOR_INIT_IN_UBOOT
endif

ifeq (${CONFIG_SUPPORT_VI_ONLY},y)
CFLAGS += -DCONFIG_SUPPORT_VI_ONLY
endif

CFLAGS += -static
CFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections
TARGET := sample_quick_start

TARGET_PATH := $(PWD)

# compile linux or LITEOS
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak

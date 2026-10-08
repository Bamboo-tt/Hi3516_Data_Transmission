
include ../Makefile.param

SMP_SRCS := $(wildcard *.c)

TARGET := $(SMP_SRCS:%.c=%)

TARGET_PATH := $(PWD)

MPI_LIBS += $(HI_REL_LIB)/libhi_bcd.a

ifeq ($(CONFIG_OT_AUDIO_BCD_SUPPORT),y)
# compile linux or liteos
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak
else
all:
	@echo "bcd sample not compile"

clean:
	@echo "bcd sample don't need clean"
endif

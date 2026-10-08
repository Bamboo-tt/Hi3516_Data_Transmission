
include ../Makefile.param

SMP_SRCS := $(wildcard *.c)
SMP_SRCS := $(wildcard $(PWD)/src/*.c)
#TARGET := $(SMP_SRCS:%.c=%)
TARGET := gb28281_app

SMP_SRCS += $(wildcard $(PWD)/adp/*.c)
 
TARGET_PATH := $(PWD)
INC_FLAGS += -I$(PWD)/inc
INC_FLAGS += -I$(PWD)/libinc
INC_FLAGS += -I$(PWD)/src

MPI_LIBS += $(PWD)/lib/libeXosip2.a
MPI_LIBS += $(PWD)/lib/libosip2.a
MPI_LIBS += $(PWD)/lib/libosipparser2.a

#LIBS_LD_CFLAGS += -L$(HI_REL_LIB) -L$(PWD)/lib  -leXosip2 -losip2 -losipparser2#-lsecurec -lmp3_enc -lmp3_lame -lmp3_dec 
LIBS_LD_CFLAGS += $(MPI_LIBS)
CFLAGS += $(INC_FLAGS)
# compile linux or liteos
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak

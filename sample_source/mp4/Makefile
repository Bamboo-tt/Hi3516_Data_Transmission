
include ../Makefile.param

SMP_SRCS := $(wildcard *.c)
TARGET := $(SMP_SRCS:%.c=%)

TARGET_PATH := $(PWD)
curdir = $(shell pwd)
MPI_LIBS += $(curdir)/lib/libmp4v2.a
CFLAGS += -I$(curdir)/lib/
CFLAGS += -I$(curdir)/include/


# compile linux or LITEOS
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak

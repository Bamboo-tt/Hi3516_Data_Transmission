include ../Makefile.param


########### SRCS & INC ############
SMP_SRCS := $(wildcard $(PWD)/*.c)

CFLAGS += -I$(HI_REL_INC)
MPI_LIBS += $(HI_REL_LIB)/libhi_mpi_otp.a
MPI_LIBS += $(HI_REL_LIB)/libhi_mpi_km.a
MPI_LIBS += $(HI_REL_LIB)/libhi_mpi_cipher.a

TARGET := sample_cipher

TARGET_PATH := $(PWD)

ifeq ($(CONFIG_LLVM), y)
	CFLAGS += -fsanitize=address
else
	CFLAGS += -fstack-protector-strong
endif

# compile linux or LITEOS
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak

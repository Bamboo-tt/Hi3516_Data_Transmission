include ../Makefile.param

##################################define AIDETECT_SUPPORT at here to support aidetect##################################################
#CFLAGS += -D AIDETECT_SUPPORT
#MPI_LIBS += -L$(HI_REL_LIB) -lhi_mpi_aidetect
#LIBS_LD_CFLAGS += -L$(REL_LIB) -lsecurec -lhi_mpi -lhi_mpi_sysmem -lsvp_acl -lprotobuf-c -lhi_mpi_km -lhi_mpi_aidetect -lhi_mpi_cipher
#######################################################################################################################################

MPI_LIBS += $(HI_REL_LIB)/libhi_mpi_smartae.a
MPI_LIBS += $(HI_REL_LIB)/libhi_mpi_cipher.a

########### SRCS & INC ############
SMP_SRCS := $(wildcard $(PWD)/*.c)

TARGET := sample_smart_ae

TARGET_PATH := $(PWD)

# compile linux or LITEOS
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak

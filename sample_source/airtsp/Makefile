
include ../Makefile.param
LIBS_LD_CFLAGS += -L$(REL_LIB) -lsecurec -lhi_mpi -lhi_mpi_sysmem -lsvp_acl -lprotobuf-c -lhi_mpi_km -lss_mpi_aidetect
MPI_LIBS += $(REL_LIB)/libhi_mpi_ive.a
MPI_LIBS += $(REL_LIB)/libhi_ivs_md.a
CFLAGS += -fstack-protector-strong

TARGET := sample_airtsp
TARGET_PATH := $(PWD)

CFLAGS += $(COMM_INC)
MPI_LIBS += $(LIBS_LD_CFLAGS)

curdir = $(shell pwd)
MPI_LIBS += $(curdir)/rtsp_lib/librtsp.a
CFLAGS += -I$(curdir)/rtsp_lib/
CFLAGS += -I$(curdir)/include/

# target source
SRC  := $(wildcard ./*.c) 
SRC += $(wildcard ./dump/*.c)
SRC += $(wildcard ./aidetect/*.c)
OBJ  := $(SRC:%.c=%.o)

#TARGET := $(OBJ:%.o=%)
.PHONY : clean all

all: $(TARGET)
	@cd rtsp_lib;   make

$(TARGET):$(OBJ) $(COMM_OBJ)
	$(CC) $(CFLAGS) -lpthread -lm -o $@ $^ $(MPI_LIBS) $(AUDIO_LIBA) $(REL_LIB)/libsecurec.a $(INC_FLAGS)

clean:
	@rm -f $(TARGET)
	@rm -f $(OBJ)
	@rm -f $(COMM_OBJ)

cleanstream:
	@rm -f *.h264
	@rm -f *.jpg
	@rm -f *.mjp
	@rm -f *.mp4


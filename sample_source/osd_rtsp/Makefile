
include ../Makefile.param


TARGET := sample_osd_rtsp
TARGET_PATH := $(PWD)

CFLAGS += $(COMM_INC)
MPI_LIBS += $(LIBS_LD_CFLAGS)

curdir = $(shell pwd)
MPI_LIBS += $(curdir)/rtsp_lib/librtsp.a
CFLAGS += -I$(curdir)/rtsp_lib/
CFLAGS += -I$(curdir)/include/

# target source
SRC  := $(wildcard ./*.c) 
SRC  += $(wildcard ./osd/*.c)
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

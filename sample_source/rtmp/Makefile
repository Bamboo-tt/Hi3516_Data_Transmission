
include ../Makefile.param


TARGET := sample_rtmp
TARGET_PATH := $(PWD)

CFLAGS += $(COMM_INC)
MPI_LIBS += $(LIBS_LD_CFLAGS)

curdir = $(shell pwd)
MPI_LIBS += $(curdir)/lib/librtmp.a
MPI_LIBS += $(curdir)/lib/libssl.a
MPI_LIBS += $(curdir)/lib/libcrypto.a
MPI_LIBS += $(curdir)/lib/libz.a
CFLAGS += -I$(curdir)/lib/
CFLAGS += -I$(curdir)/include/
CFLAGS += -I$(curdir)/openssl/

# target source
SRC  := $(wildcard ./*.c) 
OBJ  := $(SRC:%.c=%.o)

#TARGET := $(OBJ:%.o=%)
.PHONY : clean all

all: $(TARGET)


$(TARGET):$(OBJ) $(COMM_OBJ)
#	$(CC) $(CFLAGS) -L./lib -lrtmp  -ldl -lssl -lcrypto -ldl -lz -lpthread -lm -o $@ $^ $(MPI_LIBS) $(AUDIO_LIBA) $(REL_LIB)/libsecurec.a $(INC_FLAGS)
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


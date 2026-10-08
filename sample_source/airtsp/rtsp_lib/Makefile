
CROSS_COMPILE=arm-v01c02-linux-musleabi-
CC=$(CROSS_COMPILE)gcc
CXX=$(CROSS_COMPILE)g++
AR=$(CROSS_COMPILE)ar
RANLIB= $(CROSS_COOMPILE)ranlib
STRIP=$(CROSS_COMPILE)strip

TARGET = librtsp.a

CFLAGS += -g -Wall -D__LINUX__ -fPIC

all : rtsp_demo.o rtsp_msg.o rtp_enc.o stream_queue.o utils.o
	$(AR) -r $(TARGET) *.o

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $^

clean :
	/bin/rm -f *.o
	/bin/rm -f $(TARGET)

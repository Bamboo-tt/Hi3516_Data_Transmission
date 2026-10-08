

#include "sample_comm.h"
extern td_s32 GB28281_venc_start();
extern td_s32 GB28281_venc_stop();
#define WAIT_GET_STREAM_THREAD_START_SLEEP 50
#define WAIT_GET_STREAM_THREAD_START_TIMES 40
#define VENC_PATH_MAX           1024

#define SAMPLE_RETURN_CONTINUE  1
#define SAMPLE_RETURN_BREAK     2
#define SAMPLE_RETURN_NULL      3
#define SAMPLE_RETURN_GOTO      4
#define SAMPLE_RETURN_FAILURE   (-1)
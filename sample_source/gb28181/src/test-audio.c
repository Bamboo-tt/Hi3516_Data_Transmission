/*****************************copy from sample_easyAudio_comm_audio.c*****************************************/

#if 1
/*
  Copyright (c), 2001-2024, Shenshu Tech. Co., Ltd.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include "sample_comm.h"
#include "ot_audio_aac_adp.h"
#include "ot_audio_mp3_adp.h"
#include "ot_audio_opus_adp.h"
#ifdef OT_ACODEC_TYPE_INNER
#include "ot_acodec.h"
#endif
#ifdef OT_ACODEC_TYPE_ES8388
#include "es8388.h"
#endif
#ifdef OT_ACODEC_TYPE_NVP6158
#include "common.h"
#endif
#ifdef OT_ACODEC_TYPE_TP2828
#include "tp2802.h"
#endif
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////
#include "test-audio.h"
#include "RtpSendPs.h"
struct Data_Info_s pPacker_audio;
char* buf_audio;
static ot_audio_stream stream_broadcast = { 0 };
static char buf_broadcast_temp[1000]={0x00,0x01,0xF0,0x00};//海思头
#define BROADCAST_AUDIO_FRAME_SIZE 480
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////

#define AUDIO_ADPCM_TYPE OT_ADPCM_TYPE_DVI4 /* ADPCM_TYPE_IMA, ADPCM_TYPE_DVI4 */
#define G726_BPS OT_MEDIA_G726_40K          /* MEDIA_G726_16K, MEDIA_G726_24K ... */

#define AUDIO_MICIN_GAIN_OPEN 0             /* should be 1 when micin */

static ot_aac_type g_aac_type = OT_AAC_TYPE_AACLC;
static ot_aac_bps g_aac_bps = OT_AAC_BPS_96K;
static ot_aac_transport_type g_aac_transport_type = OT_AAC_TRANSPORT_TYPE_ADTS;

static ot_opus_application g_opus_app = OT_OPUS_APPLICATION_VOIP;
static ot_opus_bps g_opus_bps = OT_OPUS_BPS_96K;

typedef struct {
    td_bool start;
    pthread_t aenc_pid;
    td_s32 ae_chn;
    td_s32 ad_chn;
    FILE *fd;
    td_bool send_ad_chn;
} sample_easyAudio_aenc;

typedef struct {
    td_bool start;
    td_s32 ai_dev;
    td_s32 ai_chn;
    td_s32 aenc_chn;
    td_s32 ao_dev;
    td_s32 ao_chn;
    td_bool send_aenc;
    td_bool send_ao;
    pthread_t ai_pid;
} sample_easyAudio_ai;

typedef struct {
    td_bool start;
    td_s32 ad_chn;
    FILE *fd;
    pthread_t ad_pid;
} sample_easyAudio_adec;

typedef struct {
    ot_audio_dev ao_dev;
    td_bool start;
    pthread_t ao_pid;
} sample_easyAudio_ao;

static sample_easyAudio_ai g_sample_easyAudio_ai[OT_AI_DEV_MAX_NUM * OT_AI_MAX_CHN_NUM];
static sample_easyAudio_aenc g_sample_easyAudio_aenc[OT_AENC_MAX_CHN_NUM];
static sample_easyAudio_adec g_sample_easyAudio_adec[OT_ADEC_MAX_CHN_NUM];
static sample_easyAudio_ao g_sample_easyAudio_ao[OT_AO_DEV_MAX_NUM];

#ifdef OT_ACODEC_TYPE_INNER
static td_s32 easyAudio_sample_easyAudio_ao(ot_audio_sample_rate sample_rate, ot_acodec_fs *i2s_fs)
{
    ot_acodec_fs i2s_fs_sel;

    switch (sample_rate) {
        case OT_AUDIO_SAMPLE_RATE_8000:
            i2s_fs_sel = OT_ACODEC_FS_8000;
            break;

        case OT_AUDIO_SAMPLE_RATE_16000:
            i2s_fs_sel = OT_ACODEC_FS_16000;
            break;

        case OT_AUDIO_SAMPLE_RATE_32000:
            i2s_fs_sel = OT_ACODEC_FS_32000;
            break;

        case OT_AUDIO_SAMPLE_RATE_11025:
            i2s_fs_sel = OT_ACODEC_FS_11025;
            break;

        case OT_AUDIO_SAMPLE_RATE_22050:
            i2s_fs_sel = OT_ACODEC_FS_22050;
            break;

        case OT_AUDIO_SAMPLE_RATE_44100:
            i2s_fs_sel = OT_ACODEC_FS_44100;
            break;

        case OT_AUDIO_SAMPLE_RATE_12000:
            i2s_fs_sel = OT_ACODEC_FS_12000;
            break;

        case OT_AUDIO_SAMPLE_RATE_24000:
            i2s_fs_sel = OT_ACODEC_FS_24000;
            break;

        case OT_AUDIO_SAMPLE_RATE_48000:
            i2s_fs_sel = OT_ACODEC_FS_48000;
            break;

        case OT_AUDIO_SAMPLE_RATE_64000:
            i2s_fs_sel = OT_ACODEC_FS_64000;
            break;

        case OT_AUDIO_SAMPLE_RATE_96000:
            i2s_fs_sel = OT_ACODEC_FS_96000;
            break;

        default:
            printf("%s: not support sample_rate:%d\n", __FUNCTION__, sample_rate);
            return TD_FAILURE;
    }

    *i2s_fs = i2s_fs_sel;
    return TD_SUCCESS;
}

td_s32 sample_easyAudio_inner_codec_cfg_audio(ot_audio_sample_rate sample_rate)
{
    td_s32 ret;
    td_s32 fd_acodec = -1;
    ot_acodec_fs i2s_fs_sel;
    ot_acodec_mixer input_mode;

    fd_acodec = open(ACODEC_FILE, O_RDWR);
    if (fd_acodec < 0) {
        printf("%s: can't open audio codec,%s\n", __FUNCTION__, ACODEC_FILE);
        return TD_FAILURE;
    }

    ret = ioctl(fd_acodec, OT_ACODEC_SOFT_RESET_CTRL);
    if (ret != TD_SUCCESS) {
        printf("reset audio codec error\n");
        goto cfg_fail;
    }

    ret = easyAudio_sample_easyAudio_ao(sample_rate, &i2s_fs_sel);
    if (ret != TD_SUCCESS) {
        goto cfg_fail;
    }

    ret = ioctl(fd_acodec, OT_ACODEC_SET_I2S1_FS, &i2s_fs_sel);
    if (ret != TD_SUCCESS) {
        printf("%s: set acodec sample rate failed\n", __FUNCTION__);
        goto cfg_fail;
    }

    /* refer to hardware, demo board is pseudo-differential (IN_D), socket board is single-ended (IN1) */
    input_mode = OT_ACODEC_MIXER_IN_D;
    ret = ioctl(fd_acodec, OT_ACODEC_SET_MIXER_MIC, &input_mode);
    if (ret != TD_SUCCESS) {
        printf("%s: select acodec input_mode failed\n", __FUNCTION__);
        goto cfg_fail;
    }

#if AUDIO_MICIN_GAIN_OPEN
    /*
     * The input volume range is [-78, 80]. Both the analog gain and digital gain are adjusted.
     * A larger value indicates higher volume.
     * For example, the value 80 indicates the maximum volume of 80 dB,
     * and the value -78 indicates the minimum volume (muted status).
     * The volume adjustment takes effect simultaneously in the audio-left and audio-right channels.
     * The recommended volume range is [20, 50].
     * Within this range, the noises are lowest because only the analog gain is adjusted,
     * and the voice quality can be guaranteed.
     */
    int acodec_input_vol;

    acodec_input_vol = 30; /* 30dB */
    ret = ioctl(fd_acodec,  OT_ACODEC_SET_INPUT_VOLUME, &acodec_input_vol);
    if (ret != TD_SUCCESS) {
        printf("%s: set acodec micin volume failed\n", __FUNCTION__);
        goto cfg_fail;
    }
#endif

    printf("set inner audio codec ok: sample_rate = %d.\n", sample_rate);

cfg_fail:
    close(fd_acodec);
    return ret;
}
#endif

/* config codec */
td_s32 sample_easyAudio_comm_audio_cfg_acodec(const ot_aio_attr *aio_attr)
{
    td_bool codec_cfg = TD_FALSE;

#ifdef OT_ACODEC_TYPE_ES8388
    td_s32 ret = sample_es8388_cfg_audio(aio_attr->work_mode, aio_attr->sample_rate, aio_attr->bit_width);
    if (ret != TD_SUCCESS) {
        printf("%s: sample_es8388_cfg_audio failed\n", __FUNCTION__);
        return ret;
    }

    codec_cfg = TD_TRUE;
#endif

#ifdef OT_ACODEC_TYPE_INNER
    /* INNER AUDIO CODEC */
    td_s32 ret = sample_easyAudio_inner_codec_cfg_audio(aio_attr->sample_rate);
    if (ret != TD_SUCCESS) {
        printf("%s:sample_easyAudio_inner_codec_cfg_audio failed\n", __FUNCTION__);
        return ret;
    }
    codec_cfg = TD_TRUE;
#endif

    if (codec_cfg == TD_FALSE) {
        printf("can not find the right codec.\n");
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

static td_s32 audio_ai_get_frame_and_send(sample_easyAudio_ai *ai_ctl)
{
    td_s32 ret;
    ot_audio_frame frame = {0};
    ot_aec_frame aec_frm = {0};

    /* get frame from ai chn */
    ret = ss_mpi_ai_get_frame(ai_ctl->ai_dev, ai_ctl->ai_chn, &frame, &aec_frm, TD_FALSE);
    if (ret != TD_SUCCESS) {
        /* continue */
        return TD_SUCCESS;
    }

    /* send frame to encoder */
    if (ai_ctl->send_aenc == TD_TRUE) {
        ret = ss_mpi_aenc_send_frame(ai_ctl->aenc_chn, &frame, &aec_frm);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_aenc_send_frame(%d), failed with %#x!\n", __FUNCTION__, ai_ctl->aenc_chn, ret);
            ss_mpi_ai_release_frame(ai_ctl->ai_dev, ai_ctl->ai_chn, &frame, &aec_frm);
            return TD_FAILURE;
        }
    }

    /* send frame to ao */
    if (ai_ctl->send_ao == TD_TRUE) {
        ret = ss_mpi_ao_send_frame(ai_ctl->ao_dev, ai_ctl->ao_chn, &frame, 1000); /* 1000:1000ms */
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_ao_send_frame(%d, %d), failed with %#x!\n", __FUNCTION__, ai_ctl->ao_dev, ai_ctl->ao_chn,
                ret);
            ss_mpi_ai_release_frame(ai_ctl->ai_dev, ai_ctl->ai_chn, &frame, &aec_frm);
            return TD_FAILURE;
        }
    }

    /* finally you must release the stream */
    ret = ss_mpi_ai_release_frame(ai_ctl->ai_dev, ai_ctl->ai_chn, &frame, &aec_frm);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_ai_release_frame(%d, %d), failed with %#x!\n", __FUNCTION__, ai_ctl->ai_dev, ai_ctl->ai_chn,
            ret);
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

/* get frame from ai, send it to aenc or ao */
void *sample_easyAudio_comm_audio_ai_proc(void *parg)
{
    td_s32 ret;
    td_s32 ai_fd;
    sample_easyAudio_ai *ai_ctl = (sample_easyAudio_ai *)parg;
    fd_set read_fds;
    struct timeval timeout_val;
    ot_ai_chn_param ai_chn_para;

    ret = ss_mpi_ai_get_chn_param(ai_ctl->ai_dev, ai_ctl->ai_chn, &ai_chn_para);
    if (ret != TD_SUCCESS) {
        printf("%s: get ai chn param failed\n", __FUNCTION__);
        return NULL;
    }

    ai_chn_para.usr_frame_depth = SAMPLE_AUDIO_AI_USER_FRAME_DEPTH;

    ret = ss_mpi_ai_set_chn_param(ai_ctl->ai_dev, ai_ctl->ai_chn, &ai_chn_para);
    if (ret != TD_SUCCESS) {
        printf("%s: set ai chn param failed\n", __FUNCTION__);
        return NULL;
    }

    FD_ZERO(&read_fds);
    ai_fd = ss_mpi_ai_get_fd(ai_ctl->ai_dev, ai_ctl->ai_chn);
    if (ai_fd < 0) {
        printf("%s: get ai fd failed\n", __FUNCTION__);
        return NULL;
    }
    FD_SET(ai_fd, &read_fds);

    while (ai_ctl->start) {
        timeout_val.tv_sec = 1;
        timeout_val.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(ai_fd, &read_fds);

        ret = select(ai_fd + 1, &read_fds, NULL, NULL, &timeout_val);
        if (ret < 0) {
            break;
        } else if (ret == 0) {
            printf("%s: get ai frame select time out\n", __FUNCTION__);
            break;
        }

        if (FD_ISSET(ai_fd, &read_fds)) {
            /* get ai frame, send and release */
            ret = audio_ai_get_frame_and_send(ai_ctl);
            if (ret != TD_SUCCESS) {
                break;
            }
        }
    }

    ai_ctl->start = TD_FALSE;
    return NULL;
}

////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////
int GB28281_send_broadcast_audio()
{
    char buf_broadcast[1000]={0};        
    int buf_broadcast_temp_offset =0 ;//用来指示哪些数据已经发送到解码器了         
    int ret,recv_len;
    while(RtpSendPsStructBroadcast.mQuit==false)
    {
        buf_broadcast_temp_offset =0 ; 
        recv_len = 0;               
        recv_len = recv(RtpSendPsStructBroadcast.mSockFd,buf_broadcast,sizeof(buf_broadcast),0);
        // printf("RtpSendPsStructBroadcast.mQuit==false recv_len %d\n",recv_len);        
        if(recv_len >14)
        {
            printf("put audio stream in speaker!!!\n");            
            while((recv_len -14)> buf_broadcast_temp_offset)//有音频数据
            {
                if(stream_broadcast.len >0)//凑一帧的数据
                {
                    // printf("stream_broadcast.len %d \n ",stream_broadcast.len);
                    memset_s(buf_broadcast_temp+stream_broadcast.len,1000-stream_broadcast.len,0,1000-stream_broadcast.len);
                    memcpy_s(buf_broadcast_temp+stream_broadcast.len,1000-stream_broadcast.len,buf_broadcast+14+buf_broadcast_temp_offset,BROADCAST_AUDIO_FRAME_SIZE+4-stream_broadcast.len);//去掉14个字节RTP头部
                    buf_broadcast_temp_offset += BROADCAST_AUDIO_FRAME_SIZE+4-stream_broadcast.len;
                    stream_broadcast.len = BROADCAST_AUDIO_FRAME_SIZE+4;
                    stream_broadcast.stream = buf_broadcast_temp;
                    if (TD_TRUE) {  
                        ret = ss_mpi_adec_send_stream(0, &stream_broadcast, TD_TRUE);
                        if (ret != TD_SUCCESS) {
                            printf("%s: hi_mpi_adec_send_stream(%d), failed with %#x!\n", __FUNCTION__, 0, ret);
                            return TD_FAILURE;
                        }
                    }
                    stream_broadcast.len = 0;
                }                
                if((recv_len -14 - buf_broadcast_temp_offset) > BROADCAST_AUDIO_FRAME_SIZE )//还有一帧的数据
                {
                    memset_s(buf_broadcast_temp+4,1000-4,0,1000-4);
                    memcpy_s(buf_broadcast_temp+4,1000-4,buf_broadcast+14+buf_broadcast_temp_offset,BROADCAST_AUDIO_FRAME_SIZE);//去掉14个字节RTP头部
                    stream_broadcast.len = BROADCAST_AUDIO_FRAME_SIZE+4;
                    stream_broadcast.stream = buf_broadcast_temp;
                    buf_broadcast_temp_offset += BROADCAST_AUDIO_FRAME_SIZE;
                    if (TD_TRUE) { 
                        ret = ss_mpi_adec_send_stream(0, &stream_broadcast, TD_TRUE);
                        //printf("aenc_ctl->ad_chn %d \n",0);
                        if (ret != TD_SUCCESS) {
                            printf("%s: hi_mpi_adec_send_stream(%d), failed with %#x!\n", __FUNCTION__, 0, ret);
                            return TD_FAILURE;
                        }
                    }
                    stream_broadcast.len = 0;
                }
                
                if((recv_len -14 - buf_broadcast_temp_offset) < BROADCAST_AUDIO_FRAME_SIZE )//剩下不足一帧的数据
                {
                    memset_s(buf_broadcast_temp+4,1000-4,0,1000-4);
                    memcpy_s(buf_broadcast_temp+4,1000-4,buf_broadcast+14+buf_broadcast_temp_offset,recv_len -14 - buf_broadcast_temp_offset);//去掉14个字节RTP头部
                    stream_broadcast.len = recv_len -14 - buf_broadcast_temp_offset+4;
                    // printf("stream_broadcast.len %d recv_len -14 - buf_broadcast_temp_offset: %d \n",stream_broadcast.len,recv_len -14-buf_broadcast_temp_offset);
                    stream_broadcast.stream = buf_broadcast_temp;
                    buf_broadcast_temp_offset += recv_len -14;
                }                                
            }
        }         
        if(recv_len == 0)
        {break;}
    }
    return 0;
}

////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////

static td_s32 easyAudio_audio_aenc_get_stream_and_send(sample_easyAudio_aenc *aenc_ctl)
{
    td_s32 ret;
    ot_audio_stream stream = { 0 };

    /* get stream from aenc chn */
    ret = ss_mpi_aenc_get_stream(aenc_ctl->ae_chn, &stream, TD_FALSE);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_aenc_get_stream(%d), failed with %#x!\n", __FUNCTION__, aenc_ctl->ae_chn, ret);
        return TD_FAILURE;
    }
#if 0
    /* send stream to decoder and play for testing */
    if (aenc_ctl->send_ad_chn == TD_TRUE) {
        ret = ss_mpi_adec_send_stream(aenc_ctl->ad_chn, &stream, TD_TRUE);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_adec_send_stream(%d), failed with %#x!\n", __FUNCTION__, aenc_ctl->ad_chn, ret);
            ss_mpi_aenc_release_stream(aenc_ctl->ae_chn, &stream);
            return TD_FAILURE;
        }
    }

    /* save audio stream to file */
    (td_void)fwrite(stream.stream, 1, stream.len, aenc_ctl->fd);
    (td_void)fflush(aenc_ctl->fd);
#endif
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////

	// GB28281_send_broadcast_audio();
    (td_void)memcpy_s(buf_audio + PES_HDR_LEN , 1024 * 1024 - PES_HDR_LEN, stream.stream +4 , stream.len -4 );
    if(RtpSendPsStruct.mQuit == false)//这里帧长减去4，是因为海思的G711a输出帧，固定有个4字节的帧头
    {
        //  printf("send audio dump len %d :%X  %X %X %X AAA\n",stream.len,*(stream.stream +0),*(stream.stream +1),*(stream.stream +2),*(stream.stream +3));
        gb28181_streampackageForH264(buf_audio + PES_HDR_LEN, stream.len -4, &pPacker_audio, 1); //0 表示传递的是视频数据
        pPacker_audio.s64CurPts +=  40;//这个要根据1/(SAMPLE_AUDIO_POINT_NUM_PER_FRAME/80/100)调整
    }    
    audioStreaming = TD_TRUE;
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////
	
    /* finally you must release the stream */
    ret = ss_mpi_aenc_release_stream(aenc_ctl->ae_chn, &stream);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_aenc_release_stream(%d), failed with %#x!\n", __FUNCTION__, aenc_ctl->ae_chn, ret);
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

/* get stream from aenc, send it to adec & save it to file */
void *sample_easyAudio_comm_audio_aenc_proc(void *parg)
{
    td_s32 ret;
    td_s32 aenc_fd;
    sample_easyAudio_aenc *aenc_ctl = (sample_easyAudio_aenc *)parg;
    fd_set read_fds;
    struct timeval timeout_val;
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////

	pPacker_audio.IFrame = 1;
    pPacker_audio.u32Ssrc = 1234567890123;//10进制的ssrc
    pPacker_audio.s64CurPts = 0;
    buf_audio = (char*)malloc(1024 * 1024);
    memset_s(&stream_broadcast,sizeof(stream_broadcast),0,sizeof(stream_broadcast));
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////
	
    FD_ZERO(&read_fds);
    aenc_fd = ss_mpi_aenc_get_fd(aenc_ctl->ae_chn);
    if (aenc_fd < 0) {
        printf("%s: get aenc fd failed\n", __FUNCTION__);
        goto get_fd_fail;
    }
    FD_SET(aenc_fd, &read_fds);

    while (aenc_ctl->start) {
        timeout_val.tv_sec = 1;
        timeout_val.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(aenc_fd, &read_fds);

        ret = select(aenc_fd + 1, &read_fds, NULL, NULL, &timeout_val);
        if (ret < 0) {
            break;
        } else if (ret == 0) {
            printf("%s: get aenc stream select time out\n", __FUNCTION__);
            break;
        }

        if (FD_ISSET(aenc_fd, &read_fds)) {
            /* get stream from aenc chn, send and release */
            ret = easyAudio_audio_aenc_get_stream_and_send(aenc_ctl);
            if (ret != TD_SUCCESS) {
                break;
            }
        }
    }
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////

        free(buf_audio);
////////////////////////////////////// GB28281_RTP ==1 /////////////////////////////

get_fd_fail:
    (td_void)fclose(aenc_ctl->fd);
    aenc_ctl->fd = TD_NULL;
    aenc_ctl->start = TD_FALSE;
    return NULL;
}

/* get stream from file, and send it  to adec */
void *sample_easyAudio_comm_audio_adec_proc(void *parg)
{
    td_s32 ret;
    ot_audio_stream audio_stream;
    const td_u32 len = 640; /* 640: stream length */
    td_u32 read_len;
    td_s32 adec_chn;
    td_u8 *audio_stream_tmp = NULL;
    sample_easyAudio_adec *adec_ctl = (sample_easyAudio_adec *)parg;
    FILE *fd = adec_ctl->fd;
    adec_chn = adec_ctl->ad_chn;

    audio_stream_tmp = (td_u8 *)malloc(sizeof(td_u8) * OT_MAX_AUDIO_STREAM_LEN);
    if (audio_stream_tmp == NULL) {
        printf("%s: malloc failed!\n", __FUNCTION__);
        goto stream_malloc_fail;
    }

    while (adec_ctl->start == TD_TRUE) {
        /* read from file */
        audio_stream.stream = audio_stream_tmp;
        read_len = fread(audio_stream.stream, 1, len, fd);
        if (read_len <= 0) {
            ret = ss_mpi_adec_send_end_of_stream(adec_chn, TD_FALSE);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_adec_send_end_of_stream failed!\n", __FUNCTION__);
            }
            (td_void)fseek(fd, 0, SEEK_SET); /* read file again */
            continue;
        }

        /* here only demo adec streaming sending mode, but pack sending mode is commended */
        audio_stream.len = read_len;
        ret = ss_mpi_adec_send_stream(adec_chn, &audio_stream, TD_TRUE);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_adec_send_stream(%d) failed with %#x!\n", __FUNCTION__, adec_chn, ret);
            break;
        }
    }

    free(audio_stream_tmp);
    audio_stream_tmp = NULL;

stream_malloc_fail:
    (td_void)fclose(adec_ctl->fd);
    adec_ctl->fd = TD_NULL;
    adec_ctl->start = TD_FALSE;
    return NULL;
}

/* set ao volume */
void *sample_easyAudio_comm_audio_ao_vol_proc(void *parg)
{
    td_s32 ret;
    td_s32 volume;
    ot_audio_fade fade;
    sample_easyAudio_ao *ao_ctl = (sample_easyAudio_ao *)parg;
    ot_audio_dev ao_dev = ao_ctl->ao_dev;

    while (ao_ctl->start) {
        for (volume = 0; volume <= 6; volume++) { /* 0,6:test range */
            ret = ss_mpi_ao_set_volume(ao_dev, volume);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_ao_set_volume(%d), failed with %#x!\n", __FUNCTION__, ao_dev, ret);
            }
            printf("\rset volume %d          ", volume);
            sleep(2); /* 2:2s */
        }

        for (volume = 5; volume >= -15; volume--) { /* -15,5:test range */
            ret = ss_mpi_ao_set_volume(ao_dev, volume);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_ao_set_volume(%d), failed with %#x!\n", __FUNCTION__, ao_dev, ret);
            }
            printf("\rset volume %d          ", volume);
            sleep(2); /* 2:2s */
        }

        for (volume = -14; volume <= 0; volume++) { /* -14,0:test range */
            ret = ss_mpi_ao_set_volume(ao_dev, volume);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_ao_set_volume(%d), failed with %#x!\n", __FUNCTION__, ao_dev, ret);
            }
            printf("\rset volume %d          ", volume);
            sleep(2); /* 2:2s */
        }

        fade.fade = TD_TRUE;
        fade.fade_in_rate = OT_AUDIO_FADE_RATE_128;
        fade.fade_out_rate = OT_AUDIO_FADE_RATE_128;

        ret = ss_mpi_ao_set_mute(ao_dev, TD_TRUE, &fade);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_ao_set_volume(%d), failed with %#x!\n", __FUNCTION__, ao_dev, ret);
        }
        printf("\rset ao mute            ");
        sleep(2); /* 2:2s */

        ret = ss_mpi_ao_set_mute(ao_dev, TD_FALSE, NULL);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_ao_set_volume(%d), failed with %#x!\n", __FUNCTION__, ao_dev, ret);
        }
        printf("\rset ao unmute          ");
        sleep(2); /* 2:2s */
    }

    return NULL;
}

/* create the thread to get frame from ai and send to ao */
td_s32 sample_easyAudio_comm_audio_create_thread_ai_ao(ot_audio_dev ai_dev, ot_ai_chn ai_chn,
    ot_audio_dev ao_dev, ot_ao_chn ao_chn)
{
    sample_easyAudio_ai *ai = NULL;

    if ((ai_dev >= OT_AI_DEV_MAX_NUM) || (ai_dev < 0) ||
        (ai_chn >= OT_AI_MAX_CHN_NUM) || (ai_chn < 0)) {
        printf("%s: ai_dev = %d, ai_chn = %d error.\n", __FUNCTION__, ai_dev, ai_chn);
        return TD_FAILURE;
    }

    ai = &g_sample_easyAudio_ai[ai_dev * OT_AI_MAX_CHN_NUM + ai_chn];
    ai->send_aenc = TD_FALSE;
    ai->send_ao = TD_TRUE;
    ai->start = TD_TRUE;
    ai->ai_dev = ai_dev;
    ai->ai_chn = ai_chn;
    ai->ao_dev = ao_dev;
    ai->ao_chn = ao_chn;

    pthread_create(&ai->ai_pid, 0, sample_easyAudio_comm_audio_ai_proc, ai);

    return TD_SUCCESS;
}

/* create the thread to get frame from ai and send to aenc */
td_s32 sample_easyAudio_comm_audio_create_thread_ai_aenc(ot_audio_dev ai_dev, ot_ai_chn ai_chn, ot_aenc_chn ae_chn)
{
    sample_easyAudio_ai *ai = NULL;

    if ((ai_dev >= OT_AI_DEV_MAX_NUM) || (ai_dev < 0) ||
        (ai_chn >= OT_AI_MAX_CHN_NUM) || (ai_chn < 0)) {
        printf("%s: ai_dev = %d, ai_chn = %d error.\n", __FUNCTION__, ai_dev, ai_chn);
        return TD_FAILURE;
    }

    ai = &g_sample_easyAudio_ai[ai_dev * OT_AI_MAX_CHN_NUM + ai_chn];
    ai->send_aenc = TD_TRUE;
    ai->send_ao = TD_FALSE;
    ai->start = TD_TRUE;
    ai->ai_dev = ai_dev;
    ai->ai_chn = ai_chn;
    ai->aenc_chn = ae_chn;
    pthread_create(&ai->ai_pid, 0, sample_easyAudio_comm_audio_ai_proc, ai);

    return TD_SUCCESS;
}

/* create the thread to get stream from aenc and send to adec */
td_s32 sample_easyAudio_comm_audio_create_thread_aenc_adec(ot_aenc_chn ae_chn, ot_adec_chn ad_chn, FILE *aenc_fd)
{
    sample_easyAudio_aenc *aenc = NULL;

    if (aenc_fd == NULL) {
        return TD_FAILURE;
    }

    if ((ae_chn >= OT_AENC_MAX_CHN_NUM) || (ae_chn < 0)) {
        printf("%s: ae_chn = %d error.\n", __FUNCTION__, ae_chn);
        return TD_FAILURE;
    }

    aenc = &g_sample_easyAudio_aenc[ae_chn];
    aenc->ae_chn = ae_chn;
    aenc->ad_chn = ad_chn;
    aenc->send_ad_chn = TD_TRUE;
    aenc->fd = aenc_fd;
    aenc->start = TD_TRUE;
    pthread_create(&aenc->aenc_pid, 0, sample_easyAudio_comm_audio_aenc_proc, aenc);

    return TD_SUCCESS;
}

/* create the thread to get stream from file and send to adec */
td_s32 sample_easyAudio_comm_audio_create_thread_file_adec(ot_adec_chn ad_chn, FILE *adec_fd)
{
    sample_easyAudio_adec *adec = NULL;

    if (adec_fd == NULL) {
        return TD_FAILURE;
    }

    if ((ad_chn >= OT_ADEC_MAX_CHN_NUM) || (ad_chn < 0)) {
        printf("%s: ad_chn = %d error.\n", __FUNCTION__, ad_chn);
        return TD_FAILURE;
    }

    adec = &g_sample_easyAudio_adec[ad_chn];
    adec->ad_chn = ad_chn;
    adec->fd = adec_fd;
    adec->start = TD_TRUE;
    pthread_create(&adec->ad_pid, 0, sample_easyAudio_comm_audio_adec_proc, adec);

    return TD_SUCCESS;
}

/* create the thread to set ao volume */
td_s32 sample_easyAudio_comm_audio_create_thread_ao_vol_ctrl(ot_audio_dev ao_dev)
{
    sample_easyAudio_ao *ao_ctl = NULL;

    if ((ao_dev >= OT_AO_DEV_MAX_NUM) || (ao_dev < 0)) {
        printf("%s: ao_dev = %d error.\n", __FUNCTION__, ao_dev);
        return TD_FAILURE;
    }

    ao_ctl = &g_sample_easyAudio_ao[ao_dev];
    ao_ctl->ao_dev = ao_dev;
    ao_ctl->start = TD_TRUE;
    pthread_create(&ao_ctl->ao_pid, 0, sample_easyAudio_comm_audio_ao_vol_proc, ao_ctl);

    return TD_SUCCESS;
}

/* destroy the thread to get frame from ai and send to ao or aenc */
td_s32 sample_easyAudio_comm_audio_destroy_thread_ai(ot_audio_dev ai_dev, ot_ai_chn ai_chn)
{
    sample_easyAudio_ai *ai = NULL;

    if ((ai_dev >= OT_AI_DEV_MAX_NUM) || (ai_dev < 0) ||
        (ai_chn >= OT_AI_MAX_CHN_NUM) || (ai_chn < 0)) {
        printf("%s: ai_dev = %d, ai_chn = %d error.\n", __FUNCTION__, ai_dev, ai_chn);
        return TD_FAILURE;
    }

    ai = &g_sample_easyAudio_ai[ai_dev * OT_AI_MAX_CHN_NUM + ai_chn];
    if (ai->start) {
        ai->start = TD_FALSE;
        pthread_join(ai->ai_pid, 0);
    }

    return TD_SUCCESS;
}

/* destroy the thread to get stream from aenc and send to adec */
td_s32 sample_easyAudio_comm_audio_destroy_thread_aenc_adec(ot_aenc_chn ae_chn)
{
    sample_easyAudio_aenc *aenc = NULL;

    if ((ae_chn >= OT_AENC_MAX_CHN_NUM) || (ae_chn < 0)) {
        printf("%s: ae_chn = %d error.\n", __FUNCTION__, ae_chn);
        return TD_FAILURE;
    }

    aenc = &g_sample_easyAudio_aenc[ae_chn];
    if (aenc->start) {
        aenc->start = TD_FALSE;
        pthread_join(aenc->aenc_pid, 0);
    }

    if (aenc->fd != TD_NULL) {
        fclose(aenc->fd);
        aenc->fd = TD_NULL;
    }

    return TD_SUCCESS;
}

/* destroy the thread to get stream from file and send to adec */
td_s32 sample_easyAudio_comm_audio_destroy_thread_file_adec(ot_adec_chn ad_chn)
{
    sample_easyAudio_adec *adec = NULL;

    if ((ad_chn >= OT_ADEC_MAX_CHN_NUM) || (ad_chn < 0)) {
        printf("%s: ad_chn = %d error.\n", __FUNCTION__, ad_chn);
        return TD_FAILURE;
    }

    adec = &g_sample_easyAudio_adec[ad_chn];
    if (adec->start) {
        adec->start = TD_FALSE;
        pthread_join(adec->ad_pid, 0);
    }

    if (adec->fd != TD_NULL) {
        fclose(adec->fd);
        adec->fd = TD_NULL;
    }

    return TD_SUCCESS;
}

/* destroy the thread to set ao volume */
td_s32 sample_easyAudio_comm_audio_destroy_thread_ao_vol_ctrl(ot_audio_dev ao_dev)
{
    sample_easyAudio_ao *ao_ctl = NULL;

    if ((ao_dev >= OT_AO_DEV_MAX_NUM) || (ao_dev < 0)) {
        printf("%s: ao_dev = %d error.\n", __FUNCTION__, ao_dev);
        return TD_FAILURE;
    }

    ao_ctl = &g_sample_easyAudio_ao[ao_dev];
    if (ao_ctl->start) {
        ao_ctl->start = TD_FALSE;
        pthread_cancel(ao_ctl->ao_pid);
        pthread_join(ao_ctl->ao_pid, 0);
    }

    return TD_SUCCESS;
}

/* ao bind adec */
td_s32 sample_easyAudio_comm_audio_ao_bind_adec(ot_audio_dev ao_dev, ot_ao_chn ao_chn, ot_adec_chn ad_chn)
{
    ot_mpp_chn src_chn, dest_chn;

    src_chn.mod_id = OT_ID_ADEC;
    src_chn.dev_id = 0;
    src_chn.chn_id = ad_chn;
    dest_chn.mod_id = OT_ID_AO;
    dest_chn.dev_id = ao_dev;
    dest_chn.chn_id = ao_chn;

    return ss_mpi_sys_bind(&src_chn, &dest_chn);
}

/* ao unbind adec */
td_s32 sample_easyAudio_comm_audio_ao_unbind_adec(ot_audio_dev ao_dev, ot_ao_chn ao_chn, ot_adec_chn ad_chn)
{
    ot_mpp_chn src_chn, dest_chn;

    src_chn.mod_id = OT_ID_ADEC;
    src_chn.chn_id = ad_chn;
    src_chn.dev_id = 0;
    dest_chn.mod_id = OT_ID_AO;
    dest_chn.dev_id = ao_dev;
    dest_chn.chn_id = ao_chn;

    return ss_mpi_sys_unbind(&src_chn, &dest_chn);
}

/* ao bind ai */
td_s32 sample_easyAudio_comm_audio_ao_bind_ai(ot_audio_dev ai_dev, ot_ai_chn ai_chn, ot_audio_dev ao_dev, ot_ao_chn ao_chn)
{
    ot_mpp_chn src_chn, dest_chn;

    src_chn.mod_id = OT_ID_AI;
    src_chn.chn_id = ai_chn;
    src_chn.dev_id = ai_dev;
    dest_chn.mod_id = OT_ID_AO;
    dest_chn.dev_id = ao_dev;
    dest_chn.chn_id = ao_chn;

    return ss_mpi_sys_bind(&src_chn, &dest_chn);
}

/* ao unbind ai */
td_s32 sample_easyAudio_comm_audio_ao_unbind_ai(ot_audio_dev ai_dev, ot_ai_chn ai_chn, ot_audio_dev ao_dev, ot_ao_chn ao_chn)
{
    ot_mpp_chn src_chn, dest_chn;

    src_chn.mod_id = OT_ID_AI;
    src_chn.chn_id = ai_chn;
    src_chn.dev_id = ai_dev;
    dest_chn.mod_id = OT_ID_AO;
    dest_chn.dev_id = ao_dev;
    dest_chn.chn_id = ao_chn;

    return ss_mpi_sys_unbind(&src_chn, &dest_chn);
}

/* aenc bind ai */
td_s32 sample_easyAudio_comm_audio_aenc_bind_ai(ot_audio_dev ai_dev, ot_ai_chn ai_chn, ot_aenc_chn ae_chn)
{
    ot_mpp_chn src_chn, dest_chn;

    src_chn.mod_id = OT_ID_AI;
    src_chn.dev_id = ai_dev;
    src_chn.chn_id = ai_chn;
    dest_chn.mod_id = OT_ID_AENC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = ae_chn;

    return ss_mpi_sys_bind(&src_chn, &dest_chn);
}

/* aenc unbind ai */
td_s32 sample_easyAudio_comm_audio_aenc_unbind_ai(ot_audio_dev ai_dev, ot_ai_chn ai_chn, ot_aenc_chn ae_chn)
{
    ot_mpp_chn src_chn, dest_chn;

    src_chn.mod_id = OT_ID_AI;
    src_chn.dev_id = ai_dev;
    src_chn.chn_id = ai_chn;
    dest_chn.mod_id = OT_ID_AENC;
    dest_chn.dev_id = 0;
    dest_chn.chn_id = ae_chn;

    return ss_mpi_sys_unbind(&src_chn, &dest_chn);
}

static td_s32 sample_easyAudio_comm_audio_start_ai_vqe(ot_audio_dev ai_dev_id, ot_ai_chn ai_chn,
    const sample_comm_ai_vqe_param *ai_vqe_param, ot_audio_dev ao_dev_id)
{
    td_s32 ret;

    if (ai_vqe_param->ai_vqe_attr != NULL) {
        td_bool ai_vqe = TD_TRUE;
        switch (ai_vqe_param->ai_vqe_type) {
            case SAMPLE_AUDIO_VQE_TYPE_NONE:
                ret = TD_SUCCESS;
                ai_vqe = TD_FALSE;
                break;
            case SAMPLE_AUDIO_VQE_TYPE_RECORD:
                ret = ss_mpi_ai_set_record_vqe_attr(ai_dev_id, ai_chn,
                    (ot_ai_record_vqe_cfg *)ai_vqe_param->ai_vqe_attr);
                break;
            case SAMPLE_AUDIO_VQE_TYPE_TALK:
                ret = ss_mpi_ai_set_talk_vqe_attr(ai_dev_id, ai_chn, ao_dev_id, ai_chn,
                    (ot_ai_talk_vqe_cfg *)ai_vqe_param->ai_vqe_attr);
                break;
            case SAMPLE_AUDIO_VQE_TYPE_TALKV2:
                ret = ss_mpi_ai_set_talk_vqe_v2_attr(ai_dev_id, ai_chn, ao_dev_id, ai_chn,
                    (ot_ai_talk_vqe_v2_cfg *)ai_vqe_param->ai_vqe_attr);
                break;
            default:
                ret = TD_FAILURE;
                break;
        }
        if (ret != TD_SUCCESS) {
            printf("%s: set_ai_vqe%d(%d,%d) failed with %#x\n", __FUNCTION__, ai_vqe_param->ai_vqe_type,
                ai_dev_id, ai_chn, ret);
            return ret;
        }

        if (ai_vqe == TD_TRUE) {
            ret = ss_mpi_ai_enable_vqe(ai_dev_id, ai_chn);
            if (ret) {
                printf("%s: ss_mpi_ai_enable_vqe(%d,%d) failed with %#x\n", __FUNCTION__, ai_dev_id, ai_chn, ret);
                return ret;
            }
        }
    }

    return TD_SUCCESS;
}

/* start ai */
td_s32 sample_easyAudio_comm_audio_start_ai(ot_audio_dev ai_dev_id, td_u32 ai_chn_cnt, ot_aio_attr *aio_attr,
    const sample_comm_ai_vqe_param *ai_vqe_param, ot_audio_dev ao_dev_id)
{
    td_s32 i;
    td_s32 ret;
    td_u32 chn_cnt;

    ret = ss_mpi_ai_set_pub_attr(ai_dev_id, aio_attr);
    if (ret) {
        printf("%s: ss_mpi_ai_set_pub_attr(%d) failed with %#x\n", __FUNCTION__, ai_dev_id, ret);
        return ret;
    }

    ret = ss_mpi_ai_enable(ai_dev_id);
    if (ret) {
        printf("%s: ss_mpi_ai_enable(%d) failed with %#x\n", __FUNCTION__, ai_dev_id, ret);
        return ret;
    }

    chn_cnt = ai_chn_cnt >> ((td_u32)aio_attr->snd_mode);
    for (i = 0; i < (td_s32)chn_cnt; i++) {
        ret = ss_mpi_ai_enable_chn(ai_dev_id, i);
        if (ret) {
            printf("%s: ss_mpi_ai_enable_chn(%d,%d) failed with %#x\n", __FUNCTION__, ai_dev_id, i, ret);
            return ret;
        }

        if (ai_vqe_param->resample_en == TD_TRUE) {
            ret = ss_mpi_ai_enable_resample(ai_dev_id, i, ai_vqe_param->out_sample_rate);
            if (ret) {
                printf("%s: ss_mpi_ai_enable_re_smp(%d,%d) failed with %#x\n", __FUNCTION__, ai_dev_id, i, ret);
                return ret;
            }
        }

        ret = sample_easyAudio_comm_audio_start_ai_vqe(ai_dev_id, i, ai_vqe_param, ao_dev_id);
        if (ret != TD_SUCCESS) {
            return ret;
        }
    }

    return TD_SUCCESS;
}

/* stop ai */
td_s32 sample_easyAudio_comm_audio_stop_ai(ot_audio_dev ai_dev_id, td_u32 ai_chn_cnt, td_bool resample_en, td_bool vqe_en)
{
    td_s32 i;
    td_s32 ret;

    for (i = 0; i < (td_s32)ai_chn_cnt; i++) {
        if (resample_en == TD_TRUE) {
            ret = ss_mpi_ai_disable_resample(ai_dev_id, i);
            if (ret != TD_SUCCESS) {
                printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "failed");
                return ret;
            }
        }

        if (vqe_en == TD_TRUE) {
            ret = ss_mpi_ai_disable_vqe(ai_dev_id, i);
            if (ret != TD_SUCCESS) {
                printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "failed");
                return ret;
            }
        }

        ret = ss_mpi_ai_disable_chn(ai_dev_id, i);
        if (ret != TD_SUCCESS) {
            printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "failed");
            return ret;
        }
    }

    ret = ss_mpi_ai_disable(ai_dev_id);
    if (ret != TD_SUCCESS) {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "failed");
        return ret;
    }

    return TD_SUCCESS;
}

/* start ao */
td_s32 sample_easyAudio_comm_audio_start_ao(ot_audio_dev ao_dev_id, td_u32 ao_chn_cnt, ot_aio_attr *aio_attr,
    ot_audio_sample_rate in_sample_rate, td_bool resample_en)
{
    td_s32 i;
    td_s32 ret;
    td_u32 chn_cnt;

    ret = ss_mpi_ao_set_pub_attr(ao_dev_id, aio_attr);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_ao_set_pub_attr(%d) failed with %#x!\n", __FUNCTION__, ao_dev_id, ret);
        return TD_FAILURE;
    }

    ret = ss_mpi_ao_enable(ao_dev_id);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_ao_enable(%d) failed with %#x!\n", __FUNCTION__, ao_dev_id, ret);
        return TD_FAILURE;
    }

    chn_cnt = ao_chn_cnt >> ((td_u32)aio_attr->snd_mode);
    for (i = 0; i < (td_s32)chn_cnt; i++) {
        ret = ss_mpi_ao_enable_chn(ao_dev_id, i);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_ao_enable_chn(%d) failed with %#x!\n", __FUNCTION__, i, ret);
            return TD_FAILURE;
        }

        if (resample_en == TD_TRUE) {
            ret = ss_mpi_ao_disable_resample(ao_dev_id, i);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_ao_disable_resample (%d,%d) failed with %#x!\n", __FUNCTION__, ao_dev_id, i, ret);
                return TD_FAILURE;
            }

            ret = ss_mpi_ao_enable_resample(ao_dev_id, i, in_sample_rate);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_ao_enable_resample(%d,%d) failed with %#x!\n", __FUNCTION__, ao_dev_id, i, ret);
                return TD_FAILURE;
            }
        }
    }

    ret = ss_mpi_ao_enable_chn(ao_dev_id, OT_AO_SYS_CHN_ID);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_ao_enable_chn(%d) failed with %#x!\n", __FUNCTION__, i, ret);
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

/* stop ao */
td_s32 sample_easyAudio_comm_audio_stop_ao(ot_audio_dev ao_dev_id, td_u32 ao_chn_cnt, td_bool resample_en)
{
    td_s32 i;
    td_s32 ret;

    for (i = 0; i < (td_s32)ao_chn_cnt; i++) {
        if (resample_en == TD_TRUE) {
            ret = ss_mpi_ao_disable_resample(ao_dev_id, i);
            if (ret != TD_SUCCESS) {
                printf("%s: ss_mpi_ao_disable_re_smp failed with %#x!\n", __FUNCTION__, ret);
                return ret;
            }
        }

        ret = ss_mpi_ao_disable_chn(ao_dev_id, i);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_ao_disable_chn failed with %#x!\n", __FUNCTION__, ret);
            return ret;
        }
    }

    ret = ss_mpi_ao_disable_chn(ao_dev_id, OT_AO_SYS_CHN_ID);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_ao_disable_chn(%d) failed with %#x!\n", __FUNCTION__, i, ret);
        return TD_FAILURE;
    }

    ret = ss_mpi_ao_disable(ao_dev_id);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_ao_disable failed with %#x!\n", __FUNCTION__, ret);
        return ret;
    }

    return TD_SUCCESS;
}

static td_void aenc_attr_opus_get_param(const ot_aio_attr *aio_attr, ot_aenc_attr_opus *aenc_opus)
{
    aenc_opus->bit_rate = g_opus_bps;
    aenc_opus->app = g_opus_app;
    aenc_opus->bit_width = OT_AUDIO_BIT_WIDTH_16;
    aenc_opus->sample_rate = aio_attr->sample_rate;
    aenc_opus->snd_mode = aio_attr->snd_mode;
}

static td_void aenc_attr_mp3_get_param(const ot_aio_attr *aio_attr, ot_aenc_attr_mp3 *aenc_mp3)
{
    aenc_mp3->sample_rate = aio_attr->sample_rate;
    aenc_mp3->bit_width = OT_AUDIO_BIT_WIDTH_16;
    aenc_mp3->sound_mode = aio_attr->snd_mode;
    aenc_mp3->bit_rate = OT_MP3_BPS_128K;
    aenc_mp3->quality = 7; /* 7 : default quality */
}

static td_void aenc_attr_aac_get_param(const ot_aio_attr *aio_attr, ot_aenc_attr_aac *aenc_aac)
{
    aenc_aac->aac_type = g_aac_type;
    aenc_aac->bit_rate = g_aac_bps;
    aenc_aac->bit_width = OT_AUDIO_BIT_WIDTH_16;
    aenc_aac->sample_rate = aio_attr->sample_rate;
    aenc_aac->snd_mode = aio_attr->snd_mode;
    aenc_aac->transport_type = g_aac_transport_type;
    aenc_aac->band_width = 0;
}

/* start aenc */
td_s32 sample_easyAudio_comm_audio_start_aenc(td_u32 aenc_chn_cnt, const ot_aio_attr *aio_attr, ot_payload_type type)
{
    ot_aenc_chn ae_chn;
    td_s32 ret, i, j;
    ot_aenc_chn_attr aenc_attr;
    ot_aenc_attr_adpcm adpcm_aenc;
    ot_aenc_attr_g711 aenc_g711;
    ot_aenc_attr_g726 aenc_g726;
    ot_aenc_attr_lpcm aenc_lpcm;
    ot_aenc_attr_aac aenc_aac;
    ot_aenc_attr_mp3 aenc_mp3;
    ot_aenc_attr_opus aenc_opus;

    /* set AENC chn attr */
    aenc_attr.type = type;
    aenc_attr.buf_size = 30; /* 30:size */
    aenc_attr.point_num_per_frame = aio_attr->point_num_per_frame;

    if (aenc_attr.type == OT_PT_ADPCMA) {
        aenc_attr.value = &adpcm_aenc;
        adpcm_aenc.adpcm_type = AUDIO_ADPCM_TYPE;
    } else if ((aenc_attr.type == OT_PT_G711A) || (aenc_attr.type == OT_PT_G711U)) {
        aenc_attr.value = &aenc_g711;
    } else if (aenc_attr.type == OT_PT_G726) {
        aenc_attr.value = &aenc_g726;
        aenc_g726.g726bps = G726_BPS;
    } else if (aenc_attr.type == OT_PT_LPCM) {
        aenc_attr.value = &aenc_lpcm;
    } else if (aenc_attr.type == OT_PT_AAC) {
        aenc_attr.value = &aenc_aac;
        aenc_attr_aac_get_param(aio_attr, &aenc_aac);
    } else if (aenc_attr.type == OT_PT_MP3) {
        aenc_attr.value = &aenc_mp3;
        aenc_attr_mp3_get_param(aio_attr, &aenc_mp3);
    } else if (aenc_attr.type == OT_PT_OPUS) {
        aenc_attr.value = &aenc_opus;
        aenc_attr_opus_get_param(aio_attr, &aenc_opus);
    } else {
        printf("%s: invalid aenc payload type:%d\n", __FUNCTION__, aenc_attr.type);
        return TD_FAILURE;
    }

    for (i = 0; i < (td_s32)aenc_chn_cnt; i++) {
        ae_chn = i;
        /* create aenc chn */
        ret = ss_mpi_aenc_create_chn(ae_chn, &aenc_attr);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_aenc_create_chn(%d) failed with %#x!\n", __FUNCTION__, ae_chn, ret);
            for (j = 0; j < (td_s32)ae_chn; j++) {
                (td_void)ss_mpi_aenc_destroy_chn((ot_aenc_chn)j);
            }
            return ret;
        }
    }

    return TD_SUCCESS;
}

/* stop aenc */
td_s32 sample_easyAudio_comm_audio_stop_aenc(td_u32 aenc_chn_cnt)
{
    td_s32 i;
    td_s32 ret;

    for (i = 0; i < (td_s32)aenc_chn_cnt; i++) {
        ret = ss_mpi_aenc_destroy_chn(i);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_aenc_destroy_chn(%d) failed with %#x!\n", __FUNCTION__, i, ret);
            return ret;
        }
    }

    return TD_SUCCESS;
}

/* destroy the all thread */
td_s32 sample_easyAudio_comm_audio_destroy_all_thread(void)
{
    td_u32 dev_id, chn_id;

    for (dev_id = 0; dev_id < OT_AI_DEV_MAX_NUM; dev_id++) {
        for (chn_id = 0; chn_id < OT_AI_MAX_CHN_NUM; chn_id++) {
            if (sample_easyAudio_comm_audio_destroy_thread_ai(dev_id, chn_id) != TD_SUCCESS) {
                printf("%s: sample_easyAudio_comm_audio_destroy_thread_ai(%d,%d) failed!\n", __FUNCTION__, dev_id, chn_id);
                return TD_FAILURE;
            }
        }
    }

    for (chn_id = 0; chn_id < OT_AENC_MAX_CHN_NUM; chn_id++) {
        if (sample_easyAudio_comm_audio_destroy_thread_aenc_adec(chn_id) != TD_SUCCESS) {
            printf("%s: sample_easyAudio_comm_audio_destroy_thread_aenc_adec(%d) failed!\n", __FUNCTION__, chn_id);
            return TD_FAILURE;
        }
    }

    for (chn_id = 0; chn_id < OT_ADEC_MAX_CHN_NUM; chn_id++) {
        if (sample_easyAudio_comm_audio_destroy_thread_file_adec(chn_id) != TD_SUCCESS) {
            printf("%s: sample_easyAudio_comm_audio_destroy_thread_file_adec(%d) failed!\n", __FUNCTION__, chn_id);
            return TD_FAILURE;
        }
    }

    for (chn_id = 0; chn_id < OT_AO_DEV_MAX_NUM; chn_id++) {
        if (sample_easyAudio_comm_audio_destroy_thread_ao_vol_ctrl(chn_id) != TD_SUCCESS) {
            printf("%s: sample_easyAudio_comm_audio_destroy_thread_ao_vol_ctrl(%d) failed!\n", __FUNCTION__, chn_id);
            return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}

static td_void adec_attr_opus_get_param(const ot_aio_attr *aio_attr, ot_adec_attr_opus *adec_opus)
{
    adec_opus->sample_rate = aio_attr->sample_rate;
    adec_opus->snd_mode = aio_attr->snd_mode;
}

/* start adec */
td_s32 sample_easyAudio_comm_audio_start_adec(td_u32 adec_chn_cnt, const ot_aio_attr *aio_attr, ot_payload_type type)
{
    td_s32 i, j, ret;
    ot_adec_chn ad_chn;
    ot_adec_chn_attr adec_attr;
    ot_adec_attr_adpcm adpcm;
    ot_adec_attr_g711 adec_g711;
    ot_adec_attr_g726 adec_g726;
    ot_adec_attr_lpcm adec_lpcm;
    ot_adec_attr_aac adec_aac;
    ot_adec_attr_mp3 adec_mp3;
    ot_adec_attr_opus adec_opus;

    adec_attr.type = type;
    adec_attr.buf_size = 20; /* 20: adec buf size */
    adec_attr.mode = OT_ADEC_MODE_STREAM; /* default use stream mode */

    if (adec_attr.type == OT_PT_ADPCMA) {
        adec_attr.value = &adpcm;
        adpcm.adpcm_type = AUDIO_ADPCM_TYPE;
    } else if ((adec_attr.type == OT_PT_G711A) || (adec_attr.type == OT_PT_G711U)) {
        adec_attr.value = &adec_g711;
    } else if (adec_attr.type == OT_PT_G726) {
        adec_attr.value = &adec_g726;
        adec_g726.g726bps = G726_BPS;
    } else if (adec_attr.type == OT_PT_LPCM) {
        adec_attr.value = &adec_lpcm;
        adec_attr.mode = OT_ADEC_MODE_PACK; /* lpcm must use pack mode */
    } else if (adec_attr.type == OT_PT_AAC) {
        adec_attr.value = &adec_aac;
        adec_aac.transport_type = g_aac_transport_type;
    } else if (adec_attr.type == OT_PT_MP3) {
        adec_attr.value = &adec_mp3;
    } else if (adec_attr.type == OT_PT_OPUS) {
        adec_attr.value = &adec_opus;
        adec_attr_opus_get_param(aio_attr, &adec_opus);
    } else {
        printf("%s: invalid aenc payload type:%d\n", __FUNCTION__, adec_attr.type);
        return TD_FAILURE;
    }

    /* create adec chn */
    for (i = 0; i < (td_s32)adec_chn_cnt; i++) {
        ad_chn = i;
        ret = ss_mpi_adec_create_chn(ad_chn, &adec_attr);
        if (ret != TD_SUCCESS) {
            printf("%s: ss_mpi_adec_create_chn(%d) failed with %#x!\n", __FUNCTION__, ad_chn, ret);
            for (j = 0; j < (td_s32)ad_chn; j++) {
                (td_void)ss_mpi_adec_destroy_chn((ot_adec_chn)j);
            }
            return ret;
        }
    }
    return TD_SUCCESS;
}

/* stop adec */
td_s32 sample_easyAudio_comm_audio_stop_adec(ot_adec_chn ad_chn)
{
    td_s32 ret;

    ret = ss_mpi_adec_destroy_chn(ad_chn);
    if (ret != TD_SUCCESS) {
        printf("%s: ss_mpi_adec_destroy_chn(%d) failed with %#x!\n", __FUNCTION__, ad_chn, ret);
        return ret;
    }

    return TD_SUCCESS;
}

/* audio system init */
td_s32 sample_easyAudio_comm_audio_init(td_void)
{
    td_s32 ret;

    ss_mpi_audio_exit();

    ret = ss_mpi_audio_init();
    if (ret != TD_SUCCESS) {
        sample_print("ss_mpi_audio_init failed!\n");
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

/* audio system exit */
td_void sample_easyAudio_comm_audio_exit(td_void)
{
    ss_mpi_audio_exit();
    return;
}

#endif
/*****************************copy from sample_easyAudio_comm_audio.c*****************************************/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include "sample_comm.h"
#include "ot_audio_aac_adp.h"
#include "ot_audio_mp3_adp.h"
#include "ot_audio_opus_adp.h"
#include "ot_audio_dl_adp.h"
#include "ot_resample.h"
td_bool audioStreaming = TD_FALSE;
typedef struct {
    td_bool start;
    td_s32  ai_dev;
    td_s32  ai_chn;
    ot_audio_sample_rate in_sample;
    ot_audio_sample_rate out_sample;
    td_u32 per_frame;
    FILE *fd;
    pthread_t ai_pid;
} td_sample_ai_to_ext_res;
static td_sample_ai_to_ext_res   g_sample_ai_ext_res[OT_AI_DEV_MAX_NUM * OT_AI_MAX_CHN_NUM];
/* function : PT number to string */
static char *sample_audio_pt2_str(ot_payload_type type)
{
    if (type == OT_PT_G711A) {
        return "g711a";
    } else if (type == OT_PT_G711U) {
        return "g711u";
    } else if (type == OT_PT_ADPCMA) {
        return "adpcm";
    } else if (type == OT_PT_G726) {
        return "g726";
    } else if (type == OT_PT_LPCM) {
        return "pcm";
    } else if (type == OT_PT_AAC) {
        return "aac";
    } else if (type == OT_PT_MP3) {
        return "mp3";
    } else if (type == OT_PT_OPUS) {
        return "opus";
    } else {
        return "data";
    }
}

static ot_payload_type g_payload_type = OT_PT_G711A;
static ot_payload_type g_stream_types[] = {OT_PT_G711A, OT_PT_G711U, OT_PT_G726, OT_PT_ADPCMA,
    OT_PT_AAC, OT_PT_MP3, OT_PT_OPUS};
static td_char *g_decode_file_path = TD_NULL;
static td_bool g_aio_resample  = TD_FALSE;
static td_bool g_user_get_mode  = TD_FALSE;
static td_bool g_ao_volume_ctrl = TD_FALSE;
static ot_audio_sample_rate g_in_sample_rate  = OT_AUDIO_SAMPLE_RATE_BUTT;
static ot_audio_sample_rate g_out_sample_rate = OT_AUDIO_SAMPLE_RATE_BUTT;

static sample_audio_vqe_type g_ai_vqe_type = SAMPLE_AUDIO_VQE_TYPE_RECORD;
static td_char *g_ai_vqe_str[SAMPLE_AUDIO_VQE_TYPE_MAX] = {"none", "record", "talk", "talkv2"};
ot_ai_record_vqe_cfg g_ai_vqe_record_attr = { 0 };
ot_ai_talk_vqe_cfg g_ai_vqe_talk_attr = { 0 };
ot_ai_talk_vqe_v2_cfg g_ai_vqe_talk_v2_attr = { 0 };

static void *g_res_handle = TD_NULL;
static td_s16 *g_res_out_buf = TD_NULL;

static td_bool g_sample_audio_exit = TD_FALSE;


ot_adec_chn ad_chn;
td_u32 ao_chn_cnt;
td_u32 adec_chn_cnt;
#define sample_dbg(ret) \
    do { \
        printf("ret = %#x, fuc:%s, line:%d\n", ret, __FUNCTION__, __LINE__); \
    } while (0)

#define sample_res_check_null_ptr(ptr) \
    do { \
        if ((td_u8*)(ptr) == TD_NULL) { \
            printf("ptr is TD_NULL,fuc:%s,line:%d\n", __FUNCTION__, __LINE__); \
            return TD_FAILURE; \
        } \
    } while (0)

#define OT_RES_LIB_NAME "libvqe_res.so"
/* function : Destroy the thread to get frame from ai and send to extern resamler */
td_s32 sample_easyAudio_comm_audio_destroy_thread_ai_ext_res(ot_audio_dev ai_dev, ot_ai_chn ai_chn)
{
    td_sample_ai_to_ext_res *ai_to_ext_res = TD_NULL;

    if ((ai_dev >= OT_AI_DEV_MAX_NUM) || (ai_dev < 0) ||
        (ai_chn >= OT_AI_MAX_CHN_NUM) || (ai_chn < 0)) {
        printf("%s: ai_dev = %d, ai_chn = %d error.\n", __FUNCTION__, ai_dev, ai_chn);
        return TD_FAILURE;
    }

    ai_to_ext_res = &g_sample_ai_ext_res[ai_dev * OT_AI_MAX_CHN_NUM + ai_chn];
    if (ai_to_ext_res->start) {
        ai_to_ext_res->start = TD_FALSE;
        pthread_join(ai_to_ext_res->ai_pid, 0);
    }

    if (ai_to_ext_res->fd != TD_NULL) {
        fclose(ai_to_ext_res->fd);
        ai_to_ext_res->fd = TD_NULL;
    }

    return TD_SUCCESS;
}
static td_void sample_audio_exit_proc(td_void)
{
    td_u32 dev_id, chn_id;

    for (dev_id = 0; dev_id < OT_AI_DEV_MAX_NUM; dev_id++) {
        for (chn_id = 0; chn_id < OT_AI_MAX_CHN_NUM; chn_id++) {
            if (sample_easyAudio_comm_audio_destroy_thread_ai_ext_res(dev_id, chn_id) != TD_SUCCESS) {
                printf("%s: sample_easyAudio_comm_audio_destroy_thread_ai_ext_res(%d,%d) failed!\n", __FUNCTION__,
                    dev_id, chn_id);
            }
        }
    }
    sample_easyAudio_comm_audio_destroy_all_thread();
    sample_easyAudio_comm_audio_exit();
}
// static int smaple_audio_getchar(td_void)
// {
//     int c;

//     if (g_sample_audio_exit == TD_TRUE) {
//         sample_audio_exit_proc();
//         printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
//         exit(-1);
//     }

//     c = getchar();

//     if (g_sample_audio_exit == TD_TRUE) {
//         sample_audio_exit_proc();
//         printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
//         exit(-1);
//     }

//     return c;
// }

/* function : to process abnormal case */
static td_void sample_audio_handle_sig(td_s32 signo)
{
    if (g_sample_audio_exit == TD_TRUE) {
        return;
    }

    if ((signo == SIGINT) || (signo == SIGTERM)) {
        g_sample_audio_exit = TD_TRUE;
    }
}

static td_void sample_audio_ai_aenc_init_param(ot_aio_attr *aio_attr, ot_audio_dev *ai_dev, ot_audio_dev *ao_dev)
{
    aio_attr->sample_rate  = OT_AUDIO_SAMPLE_RATE_8000;
    aio_attr->bit_width    = OT_AUDIO_BIT_WIDTH_16;
    aio_attr->work_mode    = OT_AIO_MODE_I2S_MASTER;
    aio_attr->snd_mode     = OT_AUDIO_SOUND_MODE_MONO;
    aio_attr->expand_flag  = 1;
    aio_attr->frame_num    = SAMPLE_AUDIO_AI_USER_FRAME_DEPTH;
    if (g_payload_type == OT_PT_AAC) {
        aio_attr->point_num_per_frame = OT_AACLC_SAMPLES_PER_FRAME;
    } else if (g_payload_type == OT_PT_MP3) {
        aio_attr->point_num_per_frame = OT_MP3_POINT_NUM_PER_FRAME;
    } else if (g_payload_type == OT_PT_OPUS) {
        aio_attr->point_num_per_frame = aio_attr->sample_rate / 100; /* 100: 10ms */
    } else {
        aio_attr->point_num_per_frame = SAMPLE_AUDIO_POINT_NUM_PER_FRAME;
    }
    aio_attr->chn_cnt      = 1; /* 2:chn num */
#ifdef OT_ACODEC_TYPE_INNER
    *ai_dev = SAMPLE_AUDIO_INNER_AI_DEV;
    *ao_dev = SAMPLE_AUDIO_INNER_AO_DEV;
    aio_attr->clk_share  = 1;
    aio_attr->i2s_type   = OT_AIO_I2STYPE_INNERCODEC;
#else
    *ai_dev = SAMPLE_AUDIO_EXTERN_AI_DEV;
    *ao_dev = SAMPLE_AUDIO_EXTERN_AO_DEV;
    aio_attr->clk_share  = 1;
    aio_attr->i2s_type   = OT_AIO_I2STYPE_EXTERN;
#endif

    g_aio_resample = TD_FALSE;
    g_in_sample_rate  = OT_AUDIO_SAMPLE_RATE_8000;
    g_out_sample_rate = OT_AUDIO_SAMPLE_RATE_8000;
}
static td_void sample_audio_set_ai_vqe_param(sample_comm_ai_vqe_param *ai_vqe_param,
    ot_audio_sample_rate out_sample_rate, td_bool resample_en, td_void *ai_vqe_attr, sample_audio_vqe_type ai_vqe_type)
{
    ai_vqe_param->out_sample_rate = out_sample_rate;
    ai_vqe_param->resample_en = resample_en;
    ai_vqe_param->ai_vqe_attr = ai_vqe_attr;
    ai_vqe_param->ai_vqe_type = ai_vqe_type;
}


/* function : open aenc file */
static FILE *sample_audio_open_aenc_file(ot_aenc_chn ae_chn, ot_payload_type type)
{
    FILE *fd = TD_NULL;
    td_char asz_file_name[FILE_NAME_LEN] = {0};
    td_s32 ret;

    /* create file for save stream */
#ifdef __LITEOS__
    ret = snprintf_s(asz_file_name, FILE_NAME_LEN, FILE_NAME_LEN - 1,
        "/sharefs/audio_chn%d.%s", ae_chn, sample_audio_pt2_str(type));
#else
    ret = snprintf_s(asz_file_name, FILE_NAME_LEN, FILE_NAME_LEN - 1,
        "audio_chn%d.%s", ae_chn, sample_audio_pt2_str(type));
#endif
    if (ret < 0) {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "get aenc file name failed");
        return TD_NULL;
    }

    if (asz_file_name[0] == '\0') {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "aenc file name is NULL");
        return TD_NULL;
    }

    if (strlen(asz_file_name) > (FILE_NAME_LEN - 1)) {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "aenc file name extra long");
        return TD_NULL;
    }

    fd = fopen(asz_file_name, "w+");
    if (fd == TD_NULL) {
        printf("%s: open file %s failed\n", __FUNCTION__, asz_file_name);
        return TD_NULL;
    }
    printf("open stream file:\"%s\" for aenc ok\n", asz_file_name);
    return fd;
}

static td_s32 sample_audio_ao_bind_adec(td_u32 adec_chn_cnt, ot_audio_dev ao_dev)
{
    td_s32 ret;
    td_u32 i, j;
    ot_ao_chn ao_chn;
    ot_adec_chn ad_chn;

    for (i = 0; i < adec_chn_cnt; i++) {
        ao_chn = i;
        ad_chn = i;
        ret = sample_easyAudio_comm_audio_ao_bind_adec(ao_dev, ao_chn, ad_chn);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
            for (j = 0; j < i; j++) {
                sample_easyAudio_comm_audio_ao_unbind_adec(ao_dev, j, j);
            }
            return TD_FAILURE;
        }

        printf("bind adec:%d to ao(%d,%d) ok \n", ad_chn, ao_dev, ao_chn);
    }

    return TD_SUCCESS;
}
static td_void sample_audio_ao_unbind_adec(td_u32 adec_chn_cnt, ot_audio_dev ao_dev)
{
    td_s32 ret;
    td_u32 i;
    ot_ao_chn ao_chn;
    ot_adec_chn ad_chn;

    for (i = 0; i < adec_chn_cnt; i++) {
        ao_chn = i;
        ad_chn = i;
        ret = sample_easyAudio_comm_audio_ao_unbind_adec(ao_dev, ao_chn, ad_chn);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
    }
}
static td_s32 sample_audio_aenc_bind_ai(ot_audio_dev ai_dev, td_u32 aenc_chn_cnt)
{
    td_s32 ret;
    td_u32 i, j;
    ot_ai_chn ai_chn;
    ot_aenc_chn ae_chn;

    for (i = 0; i < aenc_chn_cnt; i++) {
        ae_chn = i;
        ai_chn = i;

        if (g_user_get_mode == TD_TRUE) {
            ret = sample_easyAudio_comm_audio_create_thread_ai_aenc(ai_dev, ai_chn, ae_chn);
        } else {
            ret = sample_easyAudio_comm_audio_aenc_bind_ai(ai_dev, ai_chn, ae_chn);
        }
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
            goto aenc_bind_err;
        }
        printf("ai(%d,%d) bind to aenc_chn:%d ok!\n", ai_dev, ai_chn, ae_chn);
    }
    return TD_SUCCESS;

aenc_bind_err:
    for (j = 0; j < i; j++) {
        ae_chn = j;
        ai_chn = j;

        if (g_user_get_mode == TD_TRUE) {
            sample_easyAudio_comm_audio_destroy_thread_ai(ai_dev, ai_chn);
        } else {
            sample_easyAudio_comm_audio_aenc_unbind_ai(ai_dev, ai_chn, ae_chn);
        }
    }
    return TD_FAILURE;
}

/* function : open adec file */
static FILE *sample_audio_open_adec_file(ot_adec_chn ad_chn, ot_payload_type type, td_char *decode_file_path)
{
    FILE *fd = TD_NULL;
    td_char asz_file_name[FILE_NAME_LEN] = {0};
    td_s32 ret;
    td_char path[PATH_MAX] = {0};

    /* create file for save stream */
    if (decode_file_path != TD_NULL) {
        ret = strcpy_s(asz_file_name, FILE_NAME_LEN, decode_file_path);
        if (ret != 0) {
            printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "get adec file name failed");
            return TD_NULL;
        }
    } else {
#ifdef __LITEOS__
        ret = snprintf_s(asz_file_name, FILE_NAME_LEN, FILE_NAME_LEN - 1,
            "/sharefs/audio_chn%d.%s", ad_chn, sample_audio_pt2_str(type));
#else
        ret = snprintf_s(asz_file_name, FILE_NAME_LEN, FILE_NAME_LEN - 1,
            "audio_chn%d.%s", ad_chn, sample_audio_pt2_str(type));
#endif
        if (ret < 0) {
            printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "get adec file name failed");
            return TD_NULL;
        }
    }

    if (asz_file_name[0] == '\0') {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "adec file name is NULL");
        return TD_NULL;
    }

    if (strlen(asz_file_name) > (FILE_NAME_LEN - 1)) {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "adec file name extra long");
        return TD_NULL;
    }

    if (realpath(asz_file_name, path) == TD_NULL) {
        printf("[func]:%s [line]:%d [info]:%s\n", __FUNCTION__, __LINE__, "adec file name realpath fail");
        return TD_NULL;
    }

    fd = fopen(path, "rb");
    if (fd == TD_NULL) {
        printf("%s: open file %s failed\n", __FUNCTION__, asz_file_name);
        return TD_NULL;
    }
    printf("open stream file:\"%s\" for adec ok\n", asz_file_name);
    return fd;
}

static td_s32 sample_audio_adec_bind_aenc(td_u32 adec_chn_cnt)
{
    td_s32 ret;
    td_u32 i, j;
    ot_aenc_chn ae_chn;
    ot_adec_chn ad_chn;
    FILE *fd = TD_NULL;

    for (i = 0; i < adec_chn_cnt; i++) {
        ae_chn = i;
        ad_chn = i;

        fd = sample_audio_open_aenc_file(ad_chn, g_payload_type);
        if (fd == TD_NULL) {
            sample_dbg(TD_FAILURE);
            for (j = 0; j < i; j++) {
                sample_easyAudio_comm_audio_destroy_thread_aenc_adec(j);
            }
            return TD_FAILURE;
        }

        ret = sample_easyAudio_comm_audio_create_thread_aenc_adec(ae_chn, ad_chn, fd);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
            fclose(fd);
            fd = TD_NULL;
            for (j = 0; j < i; j++) {
                sample_easyAudio_comm_audio_destroy_thread_aenc_adec(j);
            }
            return TD_FAILURE;
        }
    }

    return TD_SUCCESS;
}
static td_void sample_audio_adec_unbind_aenc(td_u32 adec_chn_cnt)
{
    td_s32 ret;
    td_u32 i;
    ot_adec_chn ad_chn;

    for (i = 0; i < adec_chn_cnt; i++) {
        ad_chn = i;
        ret = sample_easyAudio_comm_audio_destroy_thread_aenc_adec(ad_chn);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
    }
}

static td_void sample_audio_ai_aenc_inner(td_bool send_adec, ot_aio_attr *aio_attr, ot_audio_dev ao_dev)
{
    td_s32 ret;
    td_u32 i;
    

    /* step 5: start adec & ao. ( if you want ) */
    if (send_adec == TD_TRUE) {
        adec_chn_cnt = aio_attr->chn_cnt >> ((td_u32)aio_attr->snd_mode);
        ret = sample_easyAudio_comm_audio_start_adec(adec_chn_cnt, aio_attr, g_payload_type);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
            return;
        }

        ao_chn_cnt = aio_attr->chn_cnt;
        ret = sample_easyAudio_comm_audio_start_ao(ao_dev, ao_chn_cnt, aio_attr, g_in_sample_rate, g_aio_resample);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
            // goto ai_aenc_err2;
ai_aenc_err2:
        for (i = 0; i < adec_chn_cnt; i++) {
            ad_chn = i;
            ret = sample_easyAudio_comm_audio_stop_adec(ad_chn);
            if (ret != TD_SUCCESS) {
                sample_dbg(ret);
            }
        }
        }

        ret = sample_audio_adec_bind_aenc(adec_chn_cnt);// bin adec & aenc and  play & save
        if (ret != TD_SUCCESS) {
            // goto ai_aenc_err1;

        ret = sample_easyAudio_comm_audio_stop_ao(ao_dev, ao_chn_cnt, g_aio_resample);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }

        for (i = 0; i < adec_chn_cnt; i++) {
            ad_chn = i;
            ret = sample_easyAudio_comm_audio_stop_adec(ad_chn);
            if (ret != TD_SUCCESS) {
                sample_dbg(ret);
            }
        }
        }

        ret = sample_audio_ao_bind_adec(adec_chn_cnt, ao_dev);
        if (ret != TD_SUCCESS) {
            // goto ai_aenc_err0;
        sample_audio_adec_unbind_aenc(adec_chn_cnt);

        ret = sample_easyAudio_comm_audio_stop_ao(ao_dev, ao_chn_cnt, g_aio_resample);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
        for (i = 0; i < adec_chn_cnt; i++) {
            ad_chn = i;
            ret = sample_easyAudio_comm_audio_stop_adec(ad_chn);
            if (ret != TD_SUCCESS) {
                sample_dbg(ret);
            }
        }
        }
    }


}

ot_audio_dev ai_dev;
ot_audio_dev ao_dev;
td_u32 ai_chn_cnt;
td_u32 aenc_chn_cnt;
td_bool send_adec = TD_TRUE;
ot_aio_attr aio_attr = {0};
sample_comm_ai_vqe_param ai_vqe_param = {0};
td_s32 GB28281_audio_stop()
{
    td_s32 ret;
    int i;
    /* step 6: exit the process */
    if (send_adec == TD_TRUE) {
        sample_audio_ao_unbind_adec(adec_chn_cnt, ao_dev);

        sample_audio_adec_unbind_aenc(adec_chn_cnt);

        ret = sample_easyAudio_comm_audio_stop_ao(ao_dev, ao_chn_cnt, g_aio_resample);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }

        for (i = 0; i < adec_chn_cnt; i++) {
            ad_chn = i;
            ret = sample_easyAudio_comm_audio_stop_adec(ad_chn);
            if (ret != TD_SUCCESS) {
                sample_dbg(ret);
            }
        }
    }
    
    ret = sample_easyAudio_comm_audio_stop_aenc(aenc_chn_cnt);
    if (ret != TD_SUCCESS) {
        sample_dbg(ret);
    }

    ret = sample_easyAudio_comm_audio_stop_ai(ai_dev, ai_chn_cnt, g_aio_resample, TD_FALSE);
    if (ret != TD_SUCCESS) {
        sample_dbg(ret);
    }
    audioStreaming = TD_FALSE;
    // td_mpi_aenc_aac_deinit();
    // td_mpi_adec_aac_deinit();
    sample_audio_exit_proc();
    return ret;

}

td_s32 GB28281_audio_start()
{
    td_s32 ret;
    if(audioStreaming == TD_TRUE )
    {
        GB28281_audio_stop();
        
    }
    ret = sample_easyAudio_comm_audio_init();
     if (ret != TD_SUCCESS) {
        printf("%s: audio init failed with %d!\n", __FUNCTION__, ret);
        return TD_FAILURE;
    }
    // td_mpi_aenc_aac_init();
    // td_mpi_adec_aac_init();
    sample_audio_ai_aenc_init_param(&aio_attr, &ai_dev, &ao_dev);


    /* step 1: start ai */
    ai_chn_cnt = aio_attr.chn_cnt;
    sample_audio_set_ai_vqe_param(&ai_vqe_param, g_out_sample_rate, g_aio_resample, TD_NULL, 0);
    ret = sample_easyAudio_comm_audio_start_ai(ai_dev, ai_chn_cnt, &aio_attr, &ai_vqe_param, -1);
    if (ret != TD_SUCCESS) {
        sample_dbg(ret);
        // goto ai_aenc_err6;
        return ret;
    }

    /* step 2: config audio codec */
    ret = sample_easyAudio_comm_audio_cfg_acodec(&aio_attr);
    if (ret != TD_SUCCESS) {
        sample_dbg(ret);
        // goto ai_aenc_err5;
        ret = sample_easyAudio_comm_audio_stop_ai(ai_dev, ai_chn_cnt, g_aio_resample, TD_FALSE);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
        return ret;
    }    

    /* step 3: start aenc */
    aenc_chn_cnt = aio_attr.chn_cnt >> ((td_u32)aio_attr.snd_mode);
    ret = sample_easyAudio_comm_audio_start_aenc(aenc_chn_cnt, &aio_attr, g_payload_type);
    if (ret != TD_SUCCESS) {
        sample_dbg(ret);
        // goto ai_aenc_err5;
        ret = sample_easyAudio_comm_audio_stop_ai(ai_dev, ai_chn_cnt, g_aio_resample, TD_FALSE);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
        return ret;
    }

    /* step 4: aenc bind ai chn */
    ret = sample_audio_aenc_bind_ai(ai_dev, aenc_chn_cnt);
    if (ret != TD_SUCCESS) {
        // goto ai_aenc_err4;
        ret = sample_easyAudio_comm_audio_stop_aenc(aenc_chn_cnt);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
        ret = sample_easyAudio_comm_audio_stop_ai(ai_dev, ai_chn_cnt, g_aio_resample, TD_FALSE);
        if (ret != TD_SUCCESS) {
            sample_dbg(ret);
        }
        return ret;
    }

    sample_audio_ai_aenc_inner(send_adec, &aio_attr, ao_dev);


// ai_aenc_err4:
//     ret = sample_easyAudio_comm_audio_stop_aenc(aenc_chn_cnt);
//     if (ret != TD_SUCCESS) {
//         sample_dbg(ret);
//     }

// ai_aenc_err5:
//     ret = sample_easyAudio_comm_audio_stop_ai(ai_dev, ai_chn_cnt, g_aio_resample, TD_FALSE);
//     if (ret != TD_SUCCESS) {
//         sample_dbg(ret);
//     }

// ai_aenc_err6:
    return ret;

}

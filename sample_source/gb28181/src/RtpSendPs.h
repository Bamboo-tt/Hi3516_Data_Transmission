#ifndef RTP_SENDPS_H
#define RTP_SENDPS_H

#include<stdbool.h>

#define PS_HDR_LEN  14 // ps header 字节长度
#define SYS_HDR_LEN 18 // ps system header 字节长度
#define PSM_HDR_LEN 24 // ps system map    字节长度
#define PES_HDR_LEN 19 // ps pes header    字节长度
#define RTP_HDR_LEN 12 // rtp header       字节长度
#define RTP_VERSION 2  // rtp 版本号
#define RTP_MAX_PACKET_BUFF 1460 // rtp传输时的最大包长
#define PS_PES_PAYLOAD_SIZE 65522 // 分片进循发送的最大长度上限

union LESize
{
    unsigned short int  length;
    unsigned char   byte[2];
};

struct bits_buffer_s {
    unsigned char* p_data;
    unsigned char  i_mask;
    int i_size;
    int i_data;
};

struct Data_Info_s {
    uint64_t s64CurPts;
    int      IFrame;
    uint16_t u16CSeq;
    uint32_t u32Ssrc;
    char szBuff[RTP_MAX_PACKET_BUFF];
};

struct RtpSendPsType{
int mSockFd;
const char * mRtpServerIp;
int          mRtpServerPort;
int          mLocalRtpPort;
pthread_t mThread;
bool mQuit;
const char mCallID[20];
};


/***
*@remark:  讲传入的数据按位一个一个的压入数据
*@param :  buffer   [in]  压入数据的buffer
*          count    [in]  需要压入数据占的位数
*          bits     [in]  压入的数值
*/
#define bits_write(buffer, count, bits)\
{\
	struct bits_buffer_s *p_buffer = (buffer); \
	int i_count = (count); \
	uint64_t i_bits = (bits); \
while (i_count > 0)\
{\
	i_count--; \
if ((i_bits >> i_count) & 0x01)\
{\
	p_buffer->p_data[p_buffer->i_data] |= p_buffer->i_mask; \
}\
	else\
{\
	p_buffer->p_data[p_buffer->i_data] &= ~p_buffer->i_mask; \
}\
	p_buffer->i_mask >>= 1;         /*操作完一个字节第一位后，操作第二位*/\
if (p_buffer->i_mask == 0)     /*循环完一个字节的8位后，重新开始下一位*/\
{\
	p_buffer->i_data++; \
	p_buffer->i_mask = 0x80; \
}\
}\
}


extern struct RtpSendPsType RtpSendPsStruct;
extern struct RtpSendPsType RtpSendPsStructBroadcast;
extern struct Data_Info_s pPacker;
extern td_bool videostreaming;
extern unsigned char video_type;
extern int RtpSendPsInit(const char* rtpServerIp,int rtpServerPort,int localRtpPort);
extern int RtpSendPsStart();
extern void RtpSendPsStop();
extern void RtpSendPsStopBroadcast();
/***
*@remark:  音视频数据的打包成ps流，并封装成rtp
*@param :  pData      [in] 需要发送的音视频数据
*          nFrameLen  [in] 发送数据的长度
*          pPacker    [in] 数据包的一些信息，包括时间戳，rtp数据buff，发送的socket相关信息
*          stream_type[in] 数据类型 0 视频 1 音频
*@return:  0 success others failed
*/

extern int gb28181_streampackageForH264(char* pData, int nFrameLen, struct Data_Info_s* pPacker, int stream_type);
extern int RtpSendPsInitBroadcast(const char* rtpServerIp,int RemoteAudioRtpPort,int localReceiveAudioport);

#endif
#ifndef SIP_CLIENT_H
#define SIP_CLIENT_H
#include<stdbool.h>
struct sipClientConfigType{
    char* ua;
    char* ip;
    int port;
    char* id;
    int localRtpPort;
    int localReceiveAudioport;
    bool quit;
    struct eXosip_t* mSipCtx;
    bool mRegistered;
    int  mRegisterId;
};
struct sipServerConfigType{
    char* mIp; // SIP服务IP
    int mPort; // SIP服务端口
    char* mSipId; // SIP服务器ID
    char* mSipRealm; // SIP服务器域
    char* mSipPass; // SIP password
    int mSipTimeout; // SIP超时
    int mSipExpiry; // SIP到期
};
struct SdpTrack {
    char mediaType[10]; // video
    char peerStreamType[40] ; // 96 98 97
    char peerIp[40] ;//接收国标推流Player的IP地址
    int  peerRtpPort; // 15060
    char peerRtpTransport[20]; // 对端通过sdp发送过来的需要的rtp传输方式：TCP/RTP/AVP or RTP/AVP
};
#define IPPROTO_TCP 6
#define IPPROTO_UDP				  17
#define AF_INET 				   2

extern int init_sip_client(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig);
extern int request_register(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig);
extern int response_invite(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig);
extern int sip_event_handle(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig);
extern int request_message_keepalive(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig);
extern long getCurTimestamp( void);
#endif
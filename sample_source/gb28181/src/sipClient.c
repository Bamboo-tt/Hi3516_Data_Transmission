#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <stdarg.h>
#include "log.h"
#include "sample_comm.h"
#include<stdbool.h>
#include "osip2/osip_mt.h"
#include "eXosip2/eXosip.h"
#include "eXosip2/eX_setup.h"
#include "sipClient.h"
#include "ot_type.h"
#include "string.h"
#include "osip2/osip_time.h"
#include "RtpSendPs.h"
extern struct sipClientConfigType sipClientConfig;
extern struct sipServerConfigType sipServerConfig;
int mediaRtpPort =0;//推流�??�??
int RemoteAudioRtpPort = 0; //�??音�?��?��??�??
extern int response_broadcast(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig, char * SN);
extern int response_broadcast_invite(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig);
extern int response_broadcast_ack(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig);
extern int response_catalog(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig, char * SN, char * DeviceID);
int init_sip_client(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig)
{   
    sipClientConfig->mSipCtx =  eXosip_malloc();
    if (!sipClientConfig->mSipCtx) {
        LOGI("new uas context error");
        return -1;
    }
    if (eXosip_init(sipClientConfig->mSipCtx)) {
        LOGI("exosip init error");
        return -1;
    }
    if (eXosip_listen_addr(sipClientConfig->mSipCtx, IPPROTO_UDP, NULL, sipClientConfig->port, AF_INET, 0)) {
        LOGI("listen error");
        return -1;
    }
    eXosip_set_user_agent(sipClientConfig->mSipCtx, sipClientConfig->ua);
    if (eXosip_add_authentication_info(sipClientConfig->mSipCtx, sipClientConfig->id, sipClientConfig->id, sipServerConfig->mSipId, NULL, NULL) < 0) {
        LOGI("eXosip_add_authentication_info error");
        return -1;
    }    
    return 0;
}
void dump_request(eXosip_event_t* evt)
{
        char* s;
        size_t len;
        osip_message_to_str(evt->request, &s, &len);
        LOGI("\n打印请求包开始\ntype=%d\n%s\n打印请求包结束\n", evt->type, s);    
}
void dump_response(eXosip_event_t* evt)
{
        char* s;
        size_t len;
        osip_message_to_str(evt->response, &s, &len);
        LOGI("\n打印响应包开始\ntype=%d\n%s\n打印响应包结束\n", evt->type, s);    
}
int request_register(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig)
{
    int ret = -1;
    osip_message_t* msg = NULL;
    char from[1024] = { 0 };
    char contact[1024] = { 0 };
    char proxy[1024] = { 0 };
        if (sipClientConfig->mRegistered) { // refresh register
            LOGI("刷新注册 mRegisterId=%d", sipClientConfig->mRegisterId);

            ret = eXosip_register_build_register(sipClientConfig->mSipCtx, sipClientConfig->mRegisterId, sipServerConfig->mSipExpiry, &msg);
            if (!ret) {
                LOGI("eXosip_register_build_register error: ret=%d", ret);
                return -1;
            }
        }
        else { // new register
            LOGI("�??注册 mRegisterId=%d", sipClientConfig->mRegisterId);

            sprintf(from, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
            sprintf(proxy, "sip:%s@%s:%d", sipServerConfig->mSipId, sipServerConfig->mIp, sipServerConfig->mPort);
            sprintf(contact, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
            sipClientConfig->mRegisterId = eXosip_register_build_initial_register(sipClientConfig->mSipCtx, from, proxy, contact, sipServerConfig->mSipExpiry, &msg);
            if (sipClientConfig->mRegisterId <= 0) {
                LOGI("eXosip_register_build_initial_register error: mRegisterId=%d", sipClientConfig->mRegisterId);
                return -1;
            }
        }

        ret = eXosip_register_send_register(sipClientConfig->mSipCtx, sipClientConfig->mRegisterId, msg);
        if (ret) {
            LOGI("eXosip_register_send_register error: ret=%d", ret);
            return ret;
        }

        char* msg_str;
        size_t msg_strlen;
        osip_message_to_str(msg, &msg_str, &msg_strlen);
        LOGI("注册请求�??:\n %s", msg_str);
        

    return 0;    
}

int parse_xml(const char* data, const char* s_mark, bool with_s_make, const char* e_mark, bool with_e_make, char* dest)
{
        const char* start = strstr(data, s_mark);

        if (start != NULL) {
            const char* end = strstr(start, e_mark);
            if (end != NULL) {
                int s_pos = with_s_make ? 0 : strlen(s_mark);
                int e_pos = with_e_make ? strlen(e_mark) : 0;

                strncpy(dest, start + s_pos, (end + e_pos) - (start + s_pos));
            }
            return 0;
        }
        return -1;    
}

int response_message_answer(eXosip_event_t* evtp, int code,struct sipClientConfigType * sipClientConfig)
{
        osip_message_t* msg = TD_NULL;
        int returnCode = eXosip_message_build_answer(sipClientConfig->mSipCtx, evtp->tid, code, &msg);

        if (returnCode == 0 && msg) {
            eXosip_lock(sipClientConfig->mSipCtx);
            eXosip_message_send_answer(sipClientConfig->mSipCtx, evtp->tid, code, msg);
            eXosip_unlock(sipClientConfig->mSipCtx);
            //        osip_message_free(msg);
        }
        else {
            bool msg_state = false;
            if (msg) {
                msg_state = true;
            }
            LOGI("error: code=%d,returnCode=%d,msg=%d", code, returnCode, msg_state);
        }

        return 0;    
}

int response_message(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig)
{
        osip_body_t* req_body = TD_NULL;
        osip_message_get_body(evtp->request, 0, &req_body);
        //        LOGI("req_body->body: %s", req_body->body);

        char cmd[64] = { 0 };
        parse_xml(req_body->body, "<CmdType>", false, "</CmdType>", false, cmd);
        LOGI("got message: %s", cmd);

        char SN_c[20] = { 0 }; // 序列�??
        int SN = 0;// 序列�??
        char DeviceID[100] = { 0 }; // 设�?�编�??
        char DecoderChannelID[100] = { 0 }; // 解码器通道编码
        char PlayUrl[512] = { 0 }; // 源�?��?�地址

        parse_xml(req_body->body, "<SN>", false, "</SN>", false, SN_c);
        parse_xml(req_body->body, "<DeviceID>", false, "</DeviceID>", false, DeviceID);
        parse_xml(req_body->body, "<DecoderChannelID>", false, "</DecoderChannelID>", false, DecoderChannelID);
        parse_xml(req_body->body, "<PlayUrl>", false, "</PlayUrl>", false, PlayUrl);


        LOGI("DeviceID:%s", DeviceID);
        LOGI("DecoderChannelID:%s", DecoderChannelID);
        SN = atoi(SN_c);

        if (!strcmp(cmd, "Broadcast")) {
            // LOGI("get Broadcast\n");
            response_message_answer(evtp, 200,sipClientConfig);
            response_broadcast(sipClientConfig,&sipServerConfig,SN_c);
            response_broadcast_invite(sipClientConfig,&sipServerConfig);
        }
        else if(!strcmp(cmd, "Catalog"))
        {
            // LOGI("get Catalog\n");
            response_message_answer(evtp, 200,sipClientConfig);
            response_catalog(sipClientConfig,&sipServerConfig,SN_c,DeviceID);
        }
        else {
            response_message_answer(evtp, 200,sipClientConfig);
        }

        return 0;
}

int response_bye(eXosip_event_t* evtp) 
{
    char* username = evtp->request->to->url->username;//对应摄像头的DeviceID
    char* CallID = evtp->request->call_id->number;
    
    LOGI("username:%s", username);
    LOGI("CallID:%s", CallID);
    if(strcmp(CallID, RtpSendPsStruct.mCallID)== 0)//根据CallID判断BYE�??针�?�拉流还�??对�??
    {
        RtpSendPsStop();
        LOGI("接收到信令服务的BYE，停�??ps over rtp 推流");
    }
    if(strcmp(CallID, RtpSendPsStructBroadcast.mCallID)== 0)
    {
        RtpSendPsStopBroadcast();
        LOGI("接收到信令服务的BYE，停�?�??��?�拉�??");
    }

    return 0;
}


int power(int n, int p)//�??能算整数整�?�幂
{
    int pow =1 , i;
    for(i=1;i<=p;i++)
    {
        pow *=n;
    }
    return pow;
}
static int genRandomInt()
{
    int num =0,i=0;
    for(i=0;i<7;i++)
    {
        num += (rand()%10) *power(10,i) ;
    }
    num += (rand()%9 + 1) * power(10,7);
    return num;
}

int response_invite(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig)
{
        char* username = evtp->request->to->url->username;//对应摄像头的DeviceID
        char* CallID = evtp->request->call_id->number;
        LOGI("username:%s", username);
        LOGI("CallID:%s", CallID);
        memcpy_s(RtpSendPsStruct.mCallID,20,CallID,strlen(CallID));//把CallID保存起来后面收到BYE的时候判�??�??�??个call
        osip_message_t* answer = TD_NULL;
        eXosip_lock(sipClientConfig->mSipCtx);
        eXosip_call_send_answer(sipClientConfig->mSipCtx, evtp->tid, 180, TD_NULL);
        int ret = eXosip_call_build_answer(sipClientConfig->mSipCtx, evtp->tid, 200, &answer);
        if (ret != 0) {
            eXosip_call_send_answer(sipClientConfig->mSipCtx, evtp->tid, 400, TD_NULL);
            LOGI("camera: %s eXosip_call_build_answer error", username);
        }
        else {
{
            // 采用exosip的函数解析sdp            
            // printf("-----------exosip parse start-----------\n");
            // sdp_message_t *remote_sdp = eXosip_get_remote_sdp(sipClientConfig->mSipCtx, evtp->did); //BUG 这里老是返回NULL
            // if(remote_sdp){
            //     sdp_media_t *video_sdp = eXosip_get_video_media(remote_sdp);
            //     sdp_media_t *audio_sdp = eXosip_get_audio_media(remote_sdp);
            //     if (video_sdp) {
            //         int pos = 0;
            //         char *video_port = video_sdp->m_port; // audio_port
            //         LOGI("Get remote_sdp video: %s port %d, audio %s port %d",video_sdp->m_media,video_sdp->m_port,audio_sdp->m_media,audio_sdp->m_port);
            //         for (int i = 0; i < video_sdp->a_attributes.nb_elt; i++)
            //         {
            //             sdp_attribute_t *attr = (sdp_attribute_t *)osip_list_get(&video_sdp->a_attributes, i);
            //             printf("1-%s : %s\n", attr->a_att_field, attr->a_att_value);
            //         }
            //         while (!osip_list_eol(&(remote_sdp->a_attributes), pos)) {
            //             sdp_attribute_t *at;
            //             at = (sdp_attribute_t *)osip_list_get(&remote_sdp->a_attributes, pos);
            //             printf(
            //                 "2-%s : %s\n", at->a_att_field,
            //                 at->a_att_value); // 这里解释了为什么在SDP消息体中属�?a里面存放必须�??两列
            //             pos++;
            //         }
            //         while (!osip_list_eol(&(remote_sdp->m_medias), pos)) {
            //             sdp_attribute_t *at;

            //             at = (sdp_attribute_t *)osip_list_get(&remote_sdp->m_medias, pos);
            //             printf(
            //                 "3-%s : %s\n", at->a_att_field,
            //                 at->a_att_value); // 这里解释了为什么在SDP消息体中属�?a里面存放必须�??两列

            //             pos++;
            //         }
            //     }
            // }
            // else
            // {
            //     LOGI("eXosip_get_remote_sdp error");
            // }
            // printf("-----------exosip parse end-----------\n");
            
}
{
            printf("-----------my parse start-----------\n");
            int trackSize = 0;
            struct SdpTrack track;
            // 下面解析SDP的方式只适合一�??媒体流，所有�?�果同时包含音�?�和视�?�流，需要注意！！！
            // 采用�??定义的方式解析sdp
            osip_body_t* req_body = TD_NULL;
            char * char_tmp_ptr;
            osip_message_get_body(evtp->request, 0, &req_body);
            LOGI("osip_message_get_body: %s len: %d",req_body->body,req_body->length);
            char_tmp_ptr = strstr(req_body->body,"c=IN IP4");
            //查找对�?�IP
            if(char_tmp_ptr != TD_NULL)
            {
                if(sscanf(char_tmp_ptr,"c=IN IP4 %s ", &track.peerIp) !=1)
                {
                    LOGI("sscanf find remote IP error \n");
                }
                else
                {
                    LOGI("get remot ip %s \n",track.peerIp);
                }                
            }
            else
            {
                LOGI("find remote IP error\n");
            }
            //查找媒体信息 注意�??前只能�?�理一条媒体信�??
            char_tmp_ptr = strstr(req_body->body,"m=");
            if(char_tmp_ptr != TD_NULL)
            {
                if(sscanf(char_tmp_ptr, "m=%s %d %s %s ", &track.mediaType, &track.peerRtpPort, &track.peerRtpTransport, &track.peerStreamType) != 4)
                {
                    LOGI("cannot find media info \n");
                }
            }
            else
            {
                LOGI("cannot find media info m= \n");
            }
            mediaRtpPort = track.peerRtpPort;
            LOGI("Media info mediaType %s, peerRtpPort %d,  peerRtpTransport %s, peerStreamType %s \n", track.mediaType, track.peerRtpPort, track.peerRtpTransport, track.peerStreamType);
            LOGI("trackSize=%d", ++trackSize);
            printf("-----------my parse end-----------\n");
}

            //还有一点需要注意，这个设置：a=sendonly
            char sdpBuf[2048];
            snprintf(
                sdpBuf, sizeof(sdpBuf),
                "v=0\r\n"
                "o=admin %d 1 IN IP4 %s\r\n"
                "s=Play\r\n"
                "c=IN IP4 %s\r\n"
                "t=0 0\r\n"
                "m=video %d RTP/AVP 96 98 97\r\n"
                "a=sendonly\r\n"
                "a=rtpmap:96 PS/90000\r\n"
                "a=rtpmap:98 H264/90000\r\n"
                "a=rptmap:97 MPEG4/90000\r\n",
                genRandomInt(),
                sipClientConfig->ip, sipClientConfig->ip,
                sipClientConfig->localRtpPort);

            osip_message_set_body(answer, sdpBuf, strlen(sdpBuf));
            osip_message_set_content_type(answer, "application/sdp");
            eXosip_call_send_answer(sipClientConfig->mSipCtx, evtp->tid, 200, answer);
        }
        eXosip_unlock(sipClientConfig->mSipCtx);

        return 0;    
}

int response_ack(eXosip_event_t* evtp) 
{
    char* username = evtp->request->to->url->username;//对应摄像头的DeviceID
    char* CallID = evtp->request->call_id->number;
    int ret;
    LOGI("接收到信令服务的ACK，开始ps over rtp 推流");
    printf("username:%s\n", username);
    printf("CallID:%s\n", CallID);
    if((RtpSendPsStruct.mQuit == false) && (RtpSendPsStruct.mSockFd != -1))
    {
        LOGI("response_ack close socket");
        RtpSendPsStop();
    }
    ret = RtpSendPsInit(sipServerConfig.mIp,mediaRtpPort,sipClientConfig.localRtpPort);
    if(ret==0)
    {
        RtpSendPsStart();
    }

    return 0;
}
int request_message_keepalive(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig) {
    char from[1024] = { 0 };
    char to[1024] = { 0 };

    sprintf(from, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
    sprintf(to, "sip:%s@%s:%d", sipServerConfig->mSipId, sipServerConfig->mIp , sipServerConfig->mPort );

    osip_message_t* msg;
    char body[1024] = { 0 };

    sprintf(
        body,
        "<?xml version=\"1.0\" encoding=\"GB2312\" standalone=\"yes\" ?>\r\n"
        "<Notify>\r\n"
        "<CmdType>Keepalive</CmdType>\r\n"
        "<SN>1</SN>\r\n"
        "<DeviceID>%s</DeviceID>\r\n"
        "<Status>OK</Status>\r\n"
        "</Notify>\r\n",
        sipClientConfig->id);

    eXosip_message_build_request(sipClientConfig->mSipCtx, &msg, "MESSAGE", to, from, NULL);
    osip_message_set_body(msg, body, strlen(body));
    osip_message_set_content_type(msg, "Application/MANSCDP+xml");
    eXosip_message_send_request(sipClientConfig->mSipCtx, msg);

    //        char *s;
    //        size_t len;
    //        osip_message_to_str(msg, &s, &len);
    //         LOGI("send cmd catalog: \n%s", s);

    return 0;
}
int response_catalog(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig, char * SN, char * DeviceID)
{
    LOGI("response_catalog\n");
    char from[1024] = { 0 };
    char to[1024] = { 0 };
    int ret;
    sprintf(from, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
    sprintf(to, "sip:%s@%s:%d", sipServerConfig->mSipId, sipServerConfig->mIp , sipServerConfig->mPort );

    osip_message_t* msg;
    char body[1024] = { 0 };
    snprintf(body, 1024, "<?xml version=\"1.0\"?>\r\n"
                            "<Response>\r\n"
                            "<CmdType>Catalog</CmdType>\r\n"/*命令类型*/
                            "<SN>%s</SN>\r\n"/*命令序列�??*/
                            "<DeviceID>%s</DeviceID>\r\n"/*�??标�?��??/区域/系统的编�??*/
                            "<SumNum>1</SumNum>\r\n"/*查�?�结果总数*/
                            "<DeviceList Num=\"1\">\r\n"/*设�?�目录项列表*/
                            "<Item>\r\n"
                            "<DeviceID>%s</DeviceID>\r\n"/*�??标�?��??/区域/系统的编�??*/
                            "<Name>%s</Name>\r\n"/*设�??/区域/系统名称*/
                            "<Manufacturer>%s</Manufacturer>\r\n"/*设�?�厂�??*/
                            "<Model>%s</Model>\r\n"/*设�?�型�??*/
                            "<Owner>Owner1</Owner>\r\n"/*设�?�归�??*/
                            "<CivilCode>CivilCode1</CivilCode>\r\n"/*行政区域*/
                            "<Block>Block1</Block>\r\n"/*警区*/
                            "<Address>Address1</Address>\r\n"/*安�?�地址*/
                            "<Parental>0</Parental>\r\n"/*�??否有子�?��??*/
                            "<ParentID>%s</ParentID>\r\n"/*父�?��??/区域/系统ID*/
                            "<SafetyWay>0</SafetyWay>\r\n"/*信令安全模式/0为不采用/2为S/MIME签名方式/3为S/MIME加密签名同时采用方式/4为数字摘要方�??*/
                            "<RegisterWay>1</RegisterWay>\r\n"/*注册方式/1为�?�合sip3261标准的�?�证注册模式/2为基于口令的双向认证注册模式/3为基于数字证书的双向认证注册模式*/
                            "<CertNum>CertNum1</CertNum>\r\n"/*证书序列�??*/
                            "<Certifiable>0</Certifiable>\r\n"/*证书有效标识/0为无�??/1为有�??*/
                            "<ErrCode>400</ErrCode>\r\n"/*无效原因�??*/
                            "<EndTime>2099-12-31T23:59:59</EndTime>\r\n"/*证书终�?�有效期*/
                            "<Secrecy>0</Secrecy>\r\n"/*保密属�?/0为不涉密/1为涉�??*/
                            "<IPAddress>%s</IPAddress>\r\n"/*设�??/区域/系统IP地址*/
                            "<Port>%d</Port>\r\n"/*设�??/区域/系统�??�??*/
                            "<Password>Password1</Password>\r\n"/*设�?�口�??*/
                            "<Status>OK</Status>\r\n"/*设�?�状�??*/
                            "<Longitude>171.3</Longitude>\r\n"/*经度*/
                            "<Latitude>34.2</Latitude>\r\n"/*�??�??*/
                            "</Item>\r\n"
                            "</DeviceList>\r\n"
                            "</Response>\r\n",
                            SN,
                            DeviceID,
                            DeviceID,
                            "ϵͳ1",
                            "ϵͳ2",
                            "ϵͳ3",
                            "ϵͳID",
                            sipClientConfig->ip,
                            sipClientConfig->port);
    ret = eXosip_message_build_request(sipClientConfig->mSipCtx, &msg, "MESSAGE", to, from, NULL);
    LOGI("ret = %d",ret);
    ret = osip_message_set_body(msg, body, strlen(body));
    LOGI("ret = %d",ret);
    ret = osip_message_set_content_type(msg, "Application/MANSCDP+xml");
    LOGI("ret = %d",ret);
    ret = eXosip_message_send_request(sipClientConfig->mSipCtx, msg);
    LOGI("ret = %d",ret);

    return 0;
}
int response_broadcast(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig, char * SN)
{
    LOGI("response_broadcast\n");
    char from[1024] = { 0 };
    char to[1024] = { 0 };
    int ret;
    sprintf(from, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
    sprintf(to, "sip:%s@%s:%d", sipServerConfig->mSipId, sipServerConfig->mIp , sipServerConfig->mPort );

    osip_message_t* msg;
    char body[1024] = { 0 };
    sprintf(
        body,
        "<?xml version=\"1.0\" encoding=\"GB2312\" standalone=\"yes\" ?>\r\n"
        "<Response>\r\n"
        "<CmdType>Broadcast</CmdType>\r\n"
        "<SN>%s</SN>\r\n"
        "<DeviceID>%s</DeviceID>\r\n"
        "<Result>OK</Result>\r\n"
        "</Response>\r\n"
        "\r\n",
        SN,sipClientConfig->id);

    ret = eXosip_message_build_request(sipClientConfig->mSipCtx, &msg, "MESSAGE", to, from, NULL);
    LOGI("ret = %d",ret);
    ret = osip_message_set_body(msg, body, strlen(body));
    LOGI("ret = %d",ret);
    ret = osip_message_set_content_type(msg, "Application/MANSCDP+xml");
    LOGI("ret = %d",ret);
    ret = eXosip_message_send_request(sipClientConfig->mSipCtx, msg);
    LOGI("ret = %d",ret);

    return 0;
}
int response_broadcast_invite(struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig)
{
    LOGI("response_broadcast_invite\n");
    char from[1024] = { 0 };
    char to[1024] = { 0 };
    int ret;
    sprintf(from, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
    sprintf(to, "sip:%s@%s:%d", sipServerConfig->mSipId, sipServerConfig->mIp , sipServerConfig->mPort );
    char route[256];
    sprintf(route, "<sip:%s:%d;lr>", sipServerConfig->mIp, sipServerConfig->mPort);
    osip_message_t* msg;
    char sdpBuf[2048];
    snprintf(
        sdpBuf, sizeof(sdpBuf),
        "v=0\r\n"
        "o=%s 0 0 IN IP4 %s\r\n"
        "s=Play\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio %d TCP/RTP/AVP 8 96\r\n"
        "a=setup:active\r\n"
        "a=connection:new\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"        
        "a=rtpmap:96 PS/90000\r\n"
        "a=recvonly\r\n"
        "y=%d\r\n"
        "f=v/////a/1/8/1",
        sipClientConfig->id,sipClientConfig->ip,
        sipClientConfig->ip,sipClientConfig->localReceiveAudioport,
        genRandomInt());
    eXosip_lock(sipClientConfig->mSipCtx);
    ret = eXosip_call_build_initial_invite(sipClientConfig->mSipCtx,&msg,to,from,route,NULL);
    LOGI("ret = %d",ret);    
    ret = osip_message_set_body(msg, sdpBuf, strlen(sdpBuf));
    LOGI("ret = %d strlen(sdpBuf) %d",ret,strlen(sdpBuf));
    ret = osip_message_set_content_type(msg, "application/sdp");
            
    LOGI("ret = %d",ret);
    eXosip_call_send_initial_invite(sipClientConfig->mSipCtx,msg);
    LOGI("ret = %d",ret);
    eXosip_unlock(sipClientConfig->mSipCtx);
    return 0;
}
int response_broadcast_ack(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig)
{
    LOGI("response_broadcast_ack\n");
    char* CallID = evtp->request->call_id->number;
    LOGI("CallID:%s", CallID);
    memcpy_s(RtpSendPsStructBroadcast.mCallID,20,CallID,strlen(CallID));//把CallID保存起来后面收到BYE的时候判�??�??�??个call
    char from[1024] = { 0 };
    char to[1024] = { 0 };
    int ret;
    sprintf(from, "sip:%s@%s:%d", sipClientConfig->id, sipClientConfig->ip, sipClientConfig->port);
    sprintf(to, "sip:%s@%s:%d", sipServerConfig->mSipId, sipServerConfig->mIp , sipServerConfig->mPort );
    osip_message_t* msg;
    eXosip_lock(sipClientConfig->mSipCtx);
    ret = eXosip_call_build_ack(sipClientConfig->mSipCtx,evtp->tid,&msg);
    LOGI("ret = %d",ret); 
    ret = eXosip_call_send_ack(sipClientConfig->mSipCtx,evtp->tid,msg);   
    LOGI("ret = %d",ret);
    eXosip_unlock(sipClientConfig->mSipCtx);

{
            printf("-----------my parse start broadcast-----------\n");
            int trackSize = 0;
            struct SdpTrack track;
            // 下面解析SDP的方式只适合一�??媒体流，所有�?�果同时包含音�?�和视�?�流，需要注意！！！
            // 采用�??定义的方式解析sdp
            osip_body_t* req_body = TD_NULL;
            char * char_tmp_ptr;
            osip_message_get_body(evtp->response, 0, &req_body);
            LOGI("osip_message_get_body: %s \nlen: %d\n",req_body->body,req_body->length);
            char_tmp_ptr = strstr(req_body->body,"c=IN IP4");
            //查找对�?�IP
            if(char_tmp_ptr != TD_NULL)
            {
                if(sscanf(char_tmp_ptr,"c=IN IP4 %s ", &track.peerIp) !=1)
                {
                    LOGI("sscanf find remote IP error \n");
                }
                else
                {
                    LOGI("get remote ip %s \n",track.peerIp);
                }                
            }
            else
            {
                LOGI("find remote IP error\n");
            }
            //查找媒体信息 注意�??前只能�?�理一条媒体信�??
            char_tmp_ptr = strstr(req_body->body,"m=");
            if(char_tmp_ptr != TD_NULL)
            {
                if(sscanf(char_tmp_ptr, "m=%s %d %s %s ", &track.mediaType, &track.peerRtpPort, &track.peerRtpTransport, &track.peerStreamType) != 4)
                {
                    LOGI("cannot find media info \n");
                }
            }
            else
            {
                LOGI("cannot find media info m= \n");
            }
            RemoteAudioRtpPort = track.peerRtpPort;
            LOGI("Media info mediaType %s, peerRtpPort %d,  peerRtpTransport %s, peerStreamType %s \n", track.mediaType, track.peerRtpPort, track.peerRtpTransport, track.peerStreamType);
            LOGI("trackSize=%d", ++trackSize);
            printf("-----------my parse end broadcast-----------\n");
            RtpSendPsInitBroadcast(track.peerIp,RemoteAudioRtpPort,sipClientConfig->localReceiveAudioport);
}
        

    return 0;
}
#if 1
int sip_event_handle(eXosip_event_t* evtp,struct sipClientConfigType * sipClientConfig,struct sipServerConfigType * sipServerConfig)
{
    switch (evtp->type) {
        case EXOSIP_MESSAGE_NEW:
            LOGI("EXOSIP_MESSAGE_NEW");

            if (MSG_IS_REGISTER(evtp->request)) {
                LOGI("MSG_IS_REGISTER，不应�?�出现的响应，�?�排查问�??");
            }
            else if (MSG_IS_MESSAGE(evtp->request)) {
                dump_request(evtp);
                response_message(evtp,sipClientConfig);
            }
            else {
                LOGI("�??定义类型的MESSAGE");
                dump_request(evtp);
            }
            break;
        case EXOSIP_CALL_ANSWERED://7
            LOGI("EXOSIP_CALL_ANSWERED type=%d:这里应�?�主动发送ACK之后的回�??", evtp->type);
            dump_request(evtp);
            dump_response(evtp);
            osip_cseq_t * cseq_type_ptr = TD_NULL;
            cseq_type_ptr = osip_message_get_cseq(evtp->response);
            if(!strcmp(cseq_type_ptr->method,"INVITE"))//这个是broadcast�??200ok
            {
                LOGI("osip_message_get_cseq: %s ",cseq_type_ptr->method);
                response_broadcast_ack(evtp,sipClientConfig,sipServerConfig);

            }
            break;
        case EXOSIP_REGISTRATION_FAILURE:
            LOGI("EXOSIP_REGISTRATION_FAILURE type=%d", evtp->type);
            LOGI("mRegistered=%d,mRegisterId=%d", sipClientConfig->mRegistered, sipClientConfig->mRegisterId);
            sipClientConfig->mRegistered = TD_FALSE;

            if (eXosip_add_authentication_info(sipClientConfig->mSipCtx, sipClientConfig->id, sipClientConfig->id, sipServerConfig->mSipPass, NULL, NULL) < 0) {
                LOGI("eXosip_add_authentication_info error");
            }
            break;
        case EXOSIP_REGISTRATION_SUCCESS:
            LOGI("EXOSIP_REGISTRATION_SUCCESS type=%d", evtp->type);
            sipClientConfig->mRegistered = true;
            LOGI("mRegistered=%d,mRegisterId=%d", sipClientConfig->mRegistered, sipClientConfig->mRegisterId);

            //                this->dump_request(evtp);
            //                this->request_message_keepalive();
            break;
        case EXOSIP_CALL_INVITE:
            LOGI("EXOSIP_CALL_INVITE type=%d: 接收到�?�方发送了Invite请求", evtp->type);
            //dump_request(evtp);
            //                this->dump_response(evtp);
            response_invite(evtp,sipClientConfig);
            break;
        case EXOSIP_CALL_ACK:
            LOGI("EXOSIP_CALL_ACK type=%d: 收到来自对方ACK请求。准备国标推流�?", evtp->type);
            dump_request(evtp);
            response_ack(evtp);
            break;
        case EXOSIP_IN_SUBSCRIPTION_NEW:
            LOGI("EXOSIP_IN_SUBSCRIPTION_NEW type=%d", evtp->type);
            dump_request(evtp);
            break;
        case EXOSIP_CALL_NOANSWER: {
            LOGI("EXOSIP_IN_SUBSCRIPTION_NEW type=%d", evtp->type);
            break;
        }
        case EXOSIP_CALL_MESSAGE_NEW://14
        {
            LOGI("EXOSIP_CALL_MESSAGE_NEW type=%d", evtp->type);
            dump_request(evtp);
            dump_response(evtp);
            break;
        }
        case EXOSIP_CALL_CLOSED://21
        {
            LOGI("EXOSIP_CALL_CLOSED type=%d", evtp->type);
            dump_request(evtp);
            dump_response(evtp);
            response_bye(evtp);
            break;
        }
        case EXOSIP_CALL_RELEASED://22
        {
            LOGI("EXOSIP_CALL_RELEASED type=%d: Bye�??�??", evtp->type);
            break;
        }
        case EXOSIP_MESSAGE_REQUESTFAILURE:
            LOGI("EXOSIP_MESSAGE_REQUESTFAILURE type=%d", evtp->type);
            LOGI("evtp->textinfo= '%s' ", evtp->textinfo);
            if (evtp->ack) {
                char* ack_str;
                size_t ack_str_len;
                osip_message_to_str(evtp->ack, &ack_str, &ack_str_len);
                LOGI("ack_str=%s", ack_str);
            }
            dump_request(evtp);
            dump_response(evtp);

            break;
        case EXOSIP_MESSAGE_ANSWERED://25
            LOGI("EXOSIP_MESSAGE_ANSWERED type=%d: 接收到来�??对应的MESSAGE请求�??", evtp->type);
            break;
        default: LOGI("type=%d unknown ", evtp->type); break;
        }

    return 0;
}


long getCurTimestamp( void)
{
    struct timeval tv;   /* 存储当前获取的时�??*/
    long mstime;        /* 存储�??秒级时间*/
    osip_gettimeofday(&tv, NULL);    /* 获取当前时间*/
    mstime = tv.tv_sec * 1000 + tv.tv_usec / 1000;   /* 将�?�级时间和微秒级时间合�?�，�??换成�??秒级时间*/
    //printf("mstime: %ld\n", mstime);   /* 输出�??秒级时间*/

    return mstime;
}

#endif
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
#include <stdarg.h>
#include "log.h"
#include "sample_comm.h"
#include<stdbool.h>
#include "osip2/osip_mt.h"
#include "eXosip2/eXosip.h"
#include "sipClient.h"
#include "RtpSendPs.h"

/***********************************************/
    char serverIp[20];// = "101.37.28.157";//223.75.206.202
    int serverPort;// = 15060;
    char serverSipId[30];// = "34020000002000000001";
    char serverSipRealm[30];// = "3402000000";
    char serverSipPass[20];// = "12345678";
    int serverSipTimeout;// = 0;
    int serverSipExpiry;// = 3600;

    char clientUa[20];// ="hi3516cv610";
    char clientIp[20];// = "192.168.137.168";
    int clientPort;// = 5060;
    char clientId[30];// = "24000000000024000004";
    int localRtpPort = 10011;// = 10011;// 本地RTP的推流端口  这个参数不存储在配置文件里
    int localReceiveAudioport = 1024;// = 1024;//本地接收RTP音频流的端口；这个参数不存储在配置文件里
/********************END***************************/

struct sipClientConfigType sipClientConfig;
struct sipServerConfigType sipServerConfig;
int sipClientInit(struct sipClientConfigType * sipClientConfig)
{
    sipClientConfig->ua = clientUa;
    sipClientConfig->ip = clientIp;
    sipClientConfig->port = clientPort;
    sipClientConfig->id  = clientId;
    sipClientConfig->localRtpPort = localRtpPort;
    sipClientConfig->localReceiveAudioport = localReceiveAudioport;
    sipClientConfig->quit = TD_FALSE;
    sipClientConfig->mRegistered = TD_FALSE;
    sipClientConfig->mRegisterId = 0;
    return 0;
}
int sipServerInit(struct sipServerConfigType * sipServerConfig)
{
    sipServerConfig->mIp = serverIp;
    sipServerConfig->mPort = serverPort;
    sipServerConfig->mSipId = serverSipId;
    sipServerConfig->mSipRealm = serverSipRealm;
    sipServerConfig->mSipPass  = serverSipPass;
    sipServerConfig->mSipTimeout = serverSipTimeout;
    sipServerConfig->mSipExpiry  = serverSipExpiry;
    return 0;
}
int readConfigFile(char * parameter, char * value)//获得参数parameter的值value 注意将value全部置为'\0'
{
    FILE *file = fopen("GB28281_config.txt", "rt+");
    char * pos;
    char * pos1;
    if (file == NULL) {
        LOGI("打开配置文件失败");
        return -1;
    }
    char allFile[2048] = {0};
    fread(allFile,sizeof(allFile),1,file);
    // LOGI("allFile:\n%s",allFile);
    if(strstr(allFile,parameter)!=TD_FALSE)
    {
        pos= strstr(allFile,parameter);
        pos1= strstr(pos,"#");
        memcpy(value,pos+strlen(parameter),pos1-pos-strlen(parameter));
        LOGI("找到参数: %s%s",parameter,value);
        fclose(file);
        return 0;
    }
    LOGI("找参数错误:%s error",parameter);
    fclose(file);
    return -1;
}
int writeConfigFile(char * parameter, char * value)//设置参数parameter的值value
{
    FILE *file = fopen("GB28281_config.txt", "rt+");
    char * pos;char * pos1;
    int ret;
    if (file == NULL) {
        LOGI("打开配置文件失败");
        return -1;
    }

    char allFile[2048] = {0}, allFileTemp[2048]= {0};
    fread(allFile,sizeof(allFile),1,file);
    // LOGI("allFile:\n%s",allFile);
    if(strstr(allFile,parameter)!=TD_FALSE)
    {
        pos= strstr(allFile,parameter);
        memcpy(allFileTemp,pos,strlen(pos));
        // LOGI("找到参数原始值: %s",strstr(allFile,parameter));
        memcpy(pos+strlen(parameter),value,strlen(value));//写入参数
        memset(pos+strlen(parameter)+strlen(value),'\0',strlen(allFile)-strlen(parameter)-strlen(value));//写入参数后清零
        pos1 = strstr(allFileTemp,"#");
        strcat(allFile,pos1);
        fclose(file);
        //重新打开文件，清空，保存
        file = fopen("GB28281_config.txt", "w");
        ret = fwrite(allFile,strlen(allFile),1,file);
        if(ret != 1)
        {
            LOGI("写入参数错误");
            fclose(file);
            return -1;
        }
        else
        {
            LOGI("写入参数成功 %s%s",parameter,value);
            fflush(file);
            fclose(file);
            return 0;
        }        
    }
    LOGI("writeConifgFile error");
    fclose(file);
    return -1;
}
int initConfig()//读取配置参数文件
{
    int ret;
    char parameter[20]="serverIp=",value[30]={'\0'};
    //serverIp
    memset(serverIp,'\0',sizeof(serverIp));
    ret = readConfigFile(parameter,serverIp);
    if(ret == -1){ return -1;}
    //serverPort
    memset(parameter,'\0',sizeof(parameter));
    memset(value,'\0',sizeof(value));
    memcpy(parameter,"serverPort=",strlen("serverPort="));
    ret = readConfigFile(parameter,value);
    // LOGI("value= %d",atoi(value));
    serverPort = atoi(value);
    if(ret == -1){ return -1;}
    //serverSipId
    memset(parameter,'\0',sizeof(parameter));
    memset(serverSipId,'\0',sizeof(serverSipId));
    memcpy(parameter,"serverSipId=",strlen("serverSipId="));
    ret = readConfigFile(parameter,serverSipId);
    if(ret == -1){ return -1;}
    //serverSipRealm
    memset(parameter,'\0',sizeof(parameter));
    memset(serverSipRealm,'\0',sizeof(serverSipRealm));
    memcpy(parameter,"serverSipRealm=",strlen("serverSipRealm="));
    ret = readConfigFile(parameter,serverSipRealm);
    if(ret == -1){ return -1;}
    //serverSipPass
    memset(parameter,'\0',sizeof(parameter));
    memset(serverSipPass,'\0',sizeof(serverSipPass));
    memcpy(parameter,"serverSipPass=",strlen("serverSipPass="));
    ret = readConfigFile(parameter,serverSipPass);
    if(ret == -1){ return -1;}
    //serverSipTimeout
    memset(parameter,'\0',sizeof(parameter));
    memset(value,'\0',sizeof(value));
    memcpy(parameter,"serverSipTimeout=",strlen("serverSipTimeout="));
    ret = readConfigFile(parameter,value);
    // LOGI("value= %d",atoi(value));
    serverSipTimeout = atoi(value);
    if(ret == -1){ return -1;}
    //serverSipExpiry
    memset(parameter,'\0',sizeof(parameter));
    memset(value,'\0',sizeof(value));
    memcpy(parameter,"serverSipExpiry=",strlen("serverSipExpiry="));
    ret = readConfigFile(parameter,value);
    // LOGI("value= %d",atoi(value));
    serverSipExpiry = atoi(value);
    if(ret == -1){ return -1;}
    //clientUa
    memset(parameter,'\0',sizeof(parameter));
    memset(clientUa,'\0',sizeof(clientUa));
    memcpy(parameter,"clientUa=",strlen("clientUa="));
    ret = readConfigFile(parameter,clientUa);
    if(ret == -1){ return -1;}
    //clientIp
    memset(parameter,'\0',sizeof(parameter));
    memset(clientIp,'\0',sizeof(clientIp));
    memcpy(parameter,"clientIp=",strlen("clientIp="));
    ret = readConfigFile(parameter,clientIp);
    if(ret == -1){ return -1;}
    //clientPort
    memset(parameter,'\0',sizeof(parameter));
    memset(value,'\0',sizeof(value));
    memcpy(parameter,"clientPort=",strlen("clientPort="));
    ret = readConfigFile(parameter,value);
    // LOGI("value= %d",atoi(value));
    clientPort = atoi(value);
    if(ret == -1){ return -1;}
    //clientId
    memset(parameter,'\0',sizeof(parameter));
    memset(clientId,'\0',sizeof(clientId));
    memcpy(parameter,"clientId=",strlen("clientId="));
    ret = readConfigFile(parameter,clientId);
    if(ret == -1){ return -1;}
    //video_type
    memset(parameter,'\0',sizeof(parameter));
    memset(value,'\0',sizeof(value));
    memcpy(parameter,"video_type=",strlen("video_type="));
    ret = readConfigFile(parameter,value);
    // LOGI("value= %d",atoi(value));
    video_type = atoi(value);
    if(ret == -1){ return -1;}
    return 0;
}
td_s32 main(int argc, char *argv[])
{
    LOGI("GB28281 start \n");
    srand((int)time(NULL));   //每次执行种子不同，生成不同的随机数
    initConfig();//读取配置参数文件
    // char parameter[20]="clientUa=",value[30]={'\0'};
    // // readConfigFile(parameter,value);
    // writeConfigFile(parameter,"hbuas");
    // initConfig();//读取配置参数文件
    sipServerInit(&sipServerConfig);
    sipClientInit(&sipClientConfig);
    if(init_sip_client(&sipClientConfig,&sipServerConfig) != 0)
    {
        LOGI("init_sip_client failed \n");
    }
    static td_bool allowReg = TD_TRUE;
    td_u64 lastKeepaliveTimestamp = 0;
    td_u64 curTimestamp = 0;
    td_u64 interval = 3000;// 客户端发送keepalive检测的间隔，单位：毫秒  
    while(!sipClientConfig.quit)
    {
        // 首次发起注册
        if (allowReg && !sipClientConfig.mRegistered) {
            allowReg = TD_FALSE;
            if(request_register(&sipClientConfig,&sipServerConfig) != 0)
            {
                LOGI("request_register failed \n");
            }
            sleep(5);
        }
        // 心跳机制 start （开发过程中，为防止影响抓包，可以先注释）
        if(sipClientConfig.mRegistered){
            curTimestamp = getCurTimestamp();
            if(lastKeepaliveTimestamp == 0){
                lastKeepaliveTimestamp = curTimestamp;
            } else {
                if((curTimestamp - lastKeepaliveTimestamp) > interval){
                    request_message_keepalive(&sipClientConfig,&sipServerConfig);
                    lastKeepaliveTimestamp = curTimestamp;
                }
            }
        }
        // 心跳机制 end
        eXosip_event_t* evtp = eXosip_event_wait(sipClientConfig.mSipCtx, 0, 20);
        if (!evtp) {
            eXosip_automatic_action(sipClientConfig.mSipCtx);
            osip_usleep(100000);
            continue;
        }
        eXosip_automatic_action(sipClientConfig.mSipCtx);
        sip_event_handle(evtp,&sipClientConfig,&sipServerConfig);
        eXosip_event_free(evtp); // 释放        
    }

    printf("hello AAA \n");
}
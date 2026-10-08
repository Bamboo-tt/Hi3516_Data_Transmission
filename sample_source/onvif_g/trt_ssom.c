#include "soapStub.h"
#include "utils.h"

int SOAP_ENV__Fault_Ex(struct soap* soap, char *faultcode, char *faultstring, char *faultactor, struct SOAP_ENV__Detail *detail, struct SOAP_ENV__Code *SOAP_ENV__Code, struct SOAP_ENV__Reason *SOAP_ENV__Reason, char *SOAP_ENV__Node, char *SOAP_ENV__Role, struct SOAP_ENV__Detail *SOAP_ENV__Detail)
{
    return 0;
}

static int GetProfile(struct soap* soap, char* token, void* prof)
{

    static struct tt__VideoSourceConfiguration            vsrc;
    static struct tt__AudioSourceConfiguration            asrc;
    static struct tt__IntRectangle                        vrect;
    static struct tt__MulticastConfiguration              mcast;
    static struct tt__IPAddress                           mcast_addr;
    static enum xsd__boolean enfixed = xsd__boolean__false_;
    static char* mcast_saddr = "0.0.0.0";
    static int mcast_port  = 6601;
    int sizeProfiles = 1, i=0;

    // Allocate memory for user list.
    int profiles_size = sizeof(struct tt__Profile) * sizeProfiles;
    int vrcl = sizeof(struct tt__VideoRateControl) * sizeProfiles;
    int vrlo = sizeof(struct tt__VideoResolution)  * sizeProfiles;
    int vrln = sizeof(struct tt__H264Configuration)  * sizeProfiles;
    int ecfg = sizeof(struct tt__VideoEncoderConfiguration) * sizeProfiles;
    int aerc = sizeof(struct tt__AudioEncoderConfiguration) * sizeProfiles;
    int aopc = sizeof(struct tt__AudioOutputConfiguration) * sizeProfiles;
    int extn = sizeof(struct tt__ProfileExtension) * sizeProfiles;
    struct tt__Profile* tProfiles = (struct tt__Profile*)soap_malloc(soap, sizeof(struct tt__Profile) * sizeProfiles);
    struct tt__VideoResolution* vres = (struct tt__VideoResolution*)soap_malloc(soap, sizeof(struct tt__VideoResolution)  * sizeProfiles);
    struct tt__H264Configuration* vren = (struct tt__H264Configuration*)soap_malloc(soap, sizeof(struct tt__H264Configuration)  * sizeProfiles);
    struct tt__VideoRateControl*  vrctl = (struct tt__VideoRateControl*)soap_malloc(soap, sizeof(struct tt__VideoRateControl) * sizeProfiles);
    struct tt__VideoEncoderConfiguration* venc = (struct tt__VideoEncoderConfiguration*)soap_malloc(soap, sizeof(struct tt__VideoEncoderConfiguration) * sizeProfiles);
    struct tt__AudioEncoderConfiguration* aevc = (struct tt__AudioEncoderConfiguration*)soap_malloc(soap, sizeof(struct tt__AudioEncoderConfiguration) * sizeProfiles);
    struct tt__AudioOutputConfiguration * aoce = (struct tt__AudioOutputConfiguration*)soap_malloc(soap, sizeof(struct tt__AudioOutputConfiguration) * sizeProfiles);
    struct tt__ProfileExtension *pfex = (struct tt__ProfileExtension*)soap_malloc(soap, sizeof(struct tt__ProfileExtension) * sizeProfiles);
    memset(tProfiles, 0, sizeof(struct tt__Profile) * sizeProfiles);

    memset(vres, 0, sizeof(struct tt__VideoResolution)  * sizeProfiles);
    memset(vren, 0, sizeof(struct tt__H264Configuration)  * sizeProfiles);
    memset(venc, 0, sizeof(struct tt__VideoEncoderConfiguration) * sizeProfiles);
    memset(vrctl, 0, sizeof(struct tt__VideoRateControl) * sizeProfiles);
    memset(aevc, 0, sizeof(struct tt__AudioEncoderConfiguration) * sizeProfiles);
    memset(aoce, 0, sizeof(struct tt__AudioOutputConfiguration) * sizeProfiles);
    memset(pfex, 0, sizeof(struct tt__ProfileExtension) * sizeProfiles);

    if (!tProfiles) {
        return SOAP_FAULT;
    }
    //check profiles is exist
    if (token) {
        if(strcmp(token, "media_profile1")!=0 && strcmp(token, "media_profile2")!=0)
            return soap_sender_fault_subcode(soap, "ter:InvalidArgVal/ter:NoProfile", "The requested profile token ProfileToken does not exist.", NULL);
    }
    // Fill each user information.
    for (; i < sizeProfiles; i++) {
        // Profile
        tProfiles[i].Name   = "media_profile1";
        tProfiles[i].token  = "media_profile1";
        venc[i].token       = "video_encoder_config1";
        venc[i].Name        = "video_encoder_config1";

        venc[i].UseCount    = 2;
        venc[i].Encoding    = tt__VideoEncoding__H264;
        venc[i].Quality     = 2;
        aevc[i].token       = "video_encoder_config1";
        aevc[i].Name        = "video_encoder_config1";

        aevc[i].UseCount    = 2;

        aevc[i].Encoding = 0;
        aevc[i].Bitrate = 64;
        aevc[i].SampleRate = 8;
        vrect.height            = 1080;
        vrect.width             = 1920;//1080 720 *必须一样，否则测试不通过
        vrect.y                 = 1;
        vrect.x                 = 1;
        vsrc.Name           = "video_source_config1";
        vsrc.token          = "video_source_config1";
        vsrc.SourceToken    = "video_source1";
        vsrc.UseCount       = 2;//vsctg.UseCount;
        vsrc.Bounds         = &vrect;
        //dbc_get_audio_source_configurations(gWsApp->fDBC, &ascts);
        asrc.Name           = "AudioSourceConfig";
        asrc.UseCount       = 2;
        asrc.token          = "AudioSourceConfigToken";
        asrc.SourceToken    = "AudioSourceChannel1";
        vrctl[i].FrameRateLimit            = 30;
        vrctl[i].BitrateLimit              = 4000;
        vres[i].Height                     = 720;
        vres[i].Width                      = 1280;
        mcast_addr.Type          = tt__IPType__IPv4;
        mcast_addr.IPv4Address   = mcast_saddr;
        mcast.Address                = &mcast_addr;
        mcast.Port                   = mcast_port;
        mcast.TTL                = 64;
        mcast.AutoStart          = xsd__boolean__false_;
        venc[i].Multicast   = &mcast;
        aevc[i].Multicast   = &mcast;
        venc[i].H264        = &vren[i];    
        venc[i].RateControl = &vrctl[i];
        venc[i].Resolution  = &vres[i];
        static LONG64 SmTimeout;
        soap_s2xsd__duration(soap,"PT100S",&SmTimeout);
        venc[i].SessionTimeout = SmTimeout;
        aevc[i].SessionTimeout = SmTimeout;
        aoce[i].Name = "AudioOutputConfigName";
        aoce[i].UseCount = 2;
        aoce[i].token = "AudioOutputConfigToken";
        aoce[i].OutputToken = "AudioOutputToken";
        aoce[i].SendPrimacy = "www.onvif.org/ver20/HalfDuplex/Server";
        aoce[i].OutputLevel = 10;
        pfex[i].AudioOutputConfiguration = &aoce[i];
        tProfiles[i].PTZConfiguration   =  NULL;

        tProfiles[i].VideoEncoderConfiguration = &venc[i];
        tProfiles[i].VideoSourceConfiguration  = &vsrc;

        tProfiles[i].AudioEncoderConfiguration = &aevc[i];
        tProfiles[i].AudioSourceConfiguration  = &asrc;
        tProfiles[i].Extension  = &pfex[i];
        tProfiles[i].fixed    = &enfixed;
        if (token) {
            if (strcmp(token, tProfiles[i].Name) == 0) {
                ((struct _trt__GetProfileResponse*)prof)->Profile = &tProfiles[i];
                return SOAP_OK;
            }
        }
    }
    ((struct _trt__GetProfilesResponse*)prof)->__sizeProfiles = sizeProfiles;
    ((struct _trt__GetProfilesResponse*)prof)->Profiles       = tProfiles;
    return SOAP_OK;
}


int __trt__GetServiceCapabilities(struct soap* soap, struct _trt__GetServiceCapabilities *trt__GetServiceCapabilities, struct _trt__GetServiceCapabilitiesResponse *trt__GetServiceCapabilitiesResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoSources(struct soap* soap, struct _trt__GetVideoSources *trt__GetVideoSources, struct _trt__GetVideoSourcesResponse *trt__GetVideoSourcesResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
    static struct tt__VideoSource tVideoSources;
    static struct tt__VideoResolution tResolution;
    tVideoSources.token      = "video_source1";
    tVideoSources.Framerate  = 25;
    tVideoSources.Resolution = &tResolution;
    tResolution.Height       = 720;//tvs.ResolutionHeight;
    tResolution.Width        = 1280;//tvs.ResolutionWidth;
    trt__GetVideoSourcesResponse->__sizeVideoSources = 1;
    trt__GetVideoSourcesResponse->VideoSources     = &tVideoSources;
    return SOAP_OK;
}

int __trt__GetAudioSources(struct soap* soap, struct _trt__GetAudioSources *trt__GetAudioSources, struct _trt__GetAudioSourcesResponse *trt__GetAudioSourcesResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioOutputs(struct soap* soap, struct _trt__GetAudioOutputs *trt__GetAudioOutputs, struct _trt__GetAudioOutputsResponse *trt__GetAudioOutputsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__CreateProfile(struct soap* soap, struct _trt__CreateProfile *trt__CreateProfile, struct _trt__CreateProfileResponse *trt__CreateProfileResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetProfile(struct soap* soap, struct _trt__GetProfile *trt__GetProfile, struct _trt__GetProfileResponse *trt__GetProfileResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
    return GetProfile(soap, trt__GetProfile->ProfileToken, trt__GetProfileResponse);
}

int __trt__GetProfiles(struct soap* soap, struct _trt__GetProfiles *trt__GetProfiles, struct _trt__GetProfilesResponse *trt__GetProfilesResponse)
{

    printf("%s:%d\n",__FUNCTION__,__LINE__);
    return GetProfile(soap, NULL, trt__GetProfilesResponse);
}

int __trt__AddVideoEncoderConfiguration(struct soap* soap, struct _trt__AddVideoEncoderConfiguration *trt__AddVideoEncoderConfiguration, struct _trt__AddVideoEncoderConfigurationResponse *trt__AddVideoEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddVideoSourceConfiguration(struct soap* soap, struct _trt__AddVideoSourceConfiguration *trt__AddVideoSourceConfiguration, struct _trt__AddVideoSourceConfigurationResponse *trt__AddVideoSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddAudioEncoderConfiguration(struct soap* soap, struct _trt__AddAudioEncoderConfiguration *trt__AddAudioEncoderConfiguration, struct _trt__AddAudioEncoderConfigurationResponse *trt__AddAudioEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddAudioSourceConfiguration(struct soap* soap, struct _trt__AddAudioSourceConfiguration *trt__AddAudioSourceConfiguration, struct _trt__AddAudioSourceConfigurationResponse *trt__AddAudioSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddPTZConfiguration(struct soap* soap, struct _trt__AddPTZConfiguration *trt__AddPTZConfiguration, struct _trt__AddPTZConfigurationResponse *trt__AddPTZConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddVideoAnalyticsConfiguration(struct soap* soap, struct _trt__AddVideoAnalyticsConfiguration *trt__AddVideoAnalyticsConfiguration, struct _trt__AddVideoAnalyticsConfigurationResponse *trt__AddVideoAnalyticsConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddMetadataConfiguration(struct soap* soap, struct _trt__AddMetadataConfiguration *trt__AddMetadataConfiguration, struct _trt__AddMetadataConfigurationResponse *trt__AddMetadataConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddAudioOutputConfiguration(struct soap* soap, struct _trt__AddAudioOutputConfiguration *trt__AddAudioOutputConfiguration, struct _trt__AddAudioOutputConfigurationResponse *trt__AddAudioOutputConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__AddAudioDecoderConfiguration(struct soap* soap, struct _trt__AddAudioDecoderConfiguration *trt__AddAudioDecoderConfiguration, struct _trt__AddAudioDecoderConfigurationResponse *trt__AddAudioDecoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveVideoEncoderConfiguration(struct soap* soap, struct _trt__RemoveVideoEncoderConfiguration *trt__RemoveVideoEncoderConfiguration, struct _trt__RemoveVideoEncoderConfigurationResponse *trt__RemoveVideoEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveVideoSourceConfiguration(struct soap* soap, struct _trt__RemoveVideoSourceConfiguration *trt__RemoveVideoSourceConfiguration, struct _trt__RemoveVideoSourceConfigurationResponse *trt__RemoveVideoSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveAudioEncoderConfiguration(struct soap* soap, struct _trt__RemoveAudioEncoderConfiguration *trt__RemoveAudioEncoderConfiguration, struct _trt__RemoveAudioEncoderConfigurationResponse *trt__RemoveAudioEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveAudioSourceConfiguration(struct soap* soap, struct _trt__RemoveAudioSourceConfiguration *trt__RemoveAudioSourceConfiguration, struct _trt__RemoveAudioSourceConfigurationResponse *trt__RemoveAudioSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemovePTZConfiguration(struct soap* soap, struct _trt__RemovePTZConfiguration *trt__RemovePTZConfiguration, struct _trt__RemovePTZConfigurationResponse *trt__RemovePTZConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveVideoAnalyticsConfiguration(struct soap* soap, struct _trt__RemoveVideoAnalyticsConfiguration *trt__RemoveVideoAnalyticsConfiguration, struct _trt__RemoveVideoAnalyticsConfigurationResponse *trt__RemoveVideoAnalyticsConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveMetadataConfiguration(struct soap* soap, struct _trt__RemoveMetadataConfiguration *trt__RemoveMetadataConfiguration, struct _trt__RemoveMetadataConfigurationResponse *trt__RemoveMetadataConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveAudioOutputConfiguration(struct soap* soap, struct _trt__RemoveAudioOutputConfiguration *trt__RemoveAudioOutputConfiguration, struct _trt__RemoveAudioOutputConfigurationResponse *trt__RemoveAudioOutputConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__RemoveAudioDecoderConfiguration(struct soap* soap, struct _trt__RemoveAudioDecoderConfiguration *trt__RemoveAudioDecoderConfiguration, struct _trt__RemoveAudioDecoderConfigurationResponse *trt__RemoveAudioDecoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__DeleteProfile(struct soap* soap, struct _trt__DeleteProfile *trt__DeleteProfile, struct _trt__DeleteProfileResponse *trt__DeleteProfileResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoSourceConfigurations(struct soap* soap, struct _trt__GetVideoSourceConfigurations *trt__GetVideoSourceConfigurations, struct _trt__GetVideoSourceConfigurationsResponse *trt__GetVideoSourceConfigurationsResponse)
{
    static struct tt__VideoSourceConfiguration videosourceft;
    static struct tt__IntRectangle tBounds; /* required element of type tt:IntRectangle */
    trt__GetVideoSourceConfigurationsResponse->__sizeConfigurations    = 1;
    videosourceft.token             = "video_source_config1";
    videosourceft.Name              = "video_source_config1";
    videosourceft.UseCount          = 2;
    videosourceft.SourceToken       = "video_source1";
    tBounds.height                  = 1080;
    tBounds.width                   = 1920; //与getprofile里一致
    tBounds.y                       = 1;
    tBounds.x                       = 1;
    videosourceft.Bounds    = &tBounds;
    trt__GetVideoSourceConfigurationsResponse->Configurations  = &videosourceft;
    return SOAP_OK;
}

int __trt__GetVideoEncoderConfigurations(struct soap* soap, struct _trt__GetVideoEncoderConfigurations *trt__GetVideoEncoderConfigurations, struct _trt__GetVideoEncoderConfigurationsResponse *trt__GetVideoEncoderConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioSourceConfigurations(struct soap* soap, struct _trt__GetAudioSourceConfigurations *trt__GetAudioSourceConfigurations, struct _trt__GetAudioSourceConfigurationsResponse *trt__GetAudioSourceConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioEncoderConfigurations(struct soap* soap, struct _trt__GetAudioEncoderConfigurations *trt__GetAudioEncoderConfigurations, struct _trt__GetAudioEncoderConfigurationsResponse *trt__GetAudioEncoderConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoAnalyticsConfigurations(struct soap* soap, struct _trt__GetVideoAnalyticsConfigurations *trt__GetVideoAnalyticsConfigurations, struct _trt__GetVideoAnalyticsConfigurationsResponse *trt__GetVideoAnalyticsConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetMetadataConfigurations(struct soap* soap, struct _trt__GetMetadataConfigurations *trt__GetMetadataConfigurations, struct _trt__GetMetadataConfigurationsResponse *trt__GetMetadataConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioOutputConfigurations(struct soap* soap, struct _trt__GetAudioOutputConfigurations *trt__GetAudioOutputConfigurations, struct _trt__GetAudioOutputConfigurationsResponse *trt__GetAudioOutputConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioDecoderConfigurations(struct soap* soap, struct _trt__GetAudioDecoderConfigurations *trt__GetAudioDecoderConfigurations, struct _trt__GetAudioDecoderConfigurationsResponse *trt__GetAudioDecoderConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoSourceConfiguration(struct soap* soap, struct _trt__GetVideoSourceConfiguration *trt__GetVideoSourceConfiguration, struct _trt__GetVideoSourceConfigurationResponse *trt__GetVideoSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
    static struct tt__VideoSourceConfiguration tdConfiguration;
    tdConfiguration.Name        = "video_source_config1";
    tdConfiguration.UseCount    = 2;
    tdConfiguration.token       = "video_source_config1";
    tdConfiguration.SourceToken = "video_source1";
    static struct tt__IntRectangle tBounds;
    tBounds.height              = 1080;
    tBounds.width               = 1920;   // should same as getprofile
    tBounds.y                   = 1;
    tBounds.x                   = 1;
    tdConfiguration.Bounds  = &tBounds;
    trt__GetVideoSourceConfigurationResponse->Configuration   = &tdConfiguration;
    return SOAP_OK;
}

int __trt__GetVideoEncoderConfiguration(struct soap* soap, struct _trt__GetVideoEncoderConfiguration *trt__GetVideoEncoderConfiguration, struct _trt__GetVideoEncoderConfigurationResponse *trt__GetVideoEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioSourceConfiguration(struct soap* soap, struct _trt__GetAudioSourceConfiguration *trt__GetAudioSourceConfiguration, struct _trt__GetAudioSourceConfigurationResponse *trt__GetAudioSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioEncoderConfiguration(struct soap* soap, struct _trt__GetAudioEncoderConfiguration *trt__GetAudioEncoderConfiguration, struct _trt__GetAudioEncoderConfigurationResponse *trt__GetAudioEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoAnalyticsConfiguration(struct soap* soap, struct _trt__GetVideoAnalyticsConfiguration *trt__GetVideoAnalyticsConfiguration, struct _trt__GetVideoAnalyticsConfigurationResponse *trt__GetVideoAnalyticsConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetMetadataConfiguration(struct soap* soap, struct _trt__GetMetadataConfiguration *trt__GetMetadataConfiguration, struct _trt__GetMetadataConfigurationResponse *trt__GetMetadataConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioOutputConfiguration(struct soap* soap, struct _trt__GetAudioOutputConfiguration *trt__GetAudioOutputConfiguration, struct _trt__GetAudioOutputConfigurationResponse *trt__GetAudioOutputConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioDecoderConfiguration(struct soap* soap, struct _trt__GetAudioDecoderConfiguration *trt__GetAudioDecoderConfiguration, struct _trt__GetAudioDecoderConfigurationResponse *trt__GetAudioDecoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleVideoEncoderConfigurations(struct soap* soap, struct _trt__GetCompatibleVideoEncoderConfigurations *trt__GetCompatibleVideoEncoderConfigurations, struct _trt__GetCompatibleVideoEncoderConfigurationsResponse *trt__GetCompatibleVideoEncoderConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleVideoSourceConfigurations(struct soap* soap, struct _trt__GetCompatibleVideoSourceConfigurations *trt__GetCompatibleVideoSourceConfigurations, struct _trt__GetCompatibleVideoSourceConfigurationsResponse *trt__GetCompatibleVideoSourceConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleAudioEncoderConfigurations(struct soap* soap, struct _trt__GetCompatibleAudioEncoderConfigurations *trt__GetCompatibleAudioEncoderConfigurations, struct _trt__GetCompatibleAudioEncoderConfigurationsResponse *trt__GetCompatibleAudioEncoderConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleAudioSourceConfigurations(struct soap* soap, struct _trt__GetCompatibleAudioSourceConfigurations *trt__GetCompatibleAudioSourceConfigurations, struct _trt__GetCompatibleAudioSourceConfigurationsResponse *trt__GetCompatibleAudioSourceConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleVideoAnalyticsConfigurations(struct soap* soap, struct _trt__GetCompatibleVideoAnalyticsConfigurations *trt__GetCompatibleVideoAnalyticsConfigurations, struct _trt__GetCompatibleVideoAnalyticsConfigurationsResponse *trt__GetCompatibleVideoAnalyticsConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleMetadataConfigurations(struct soap* soap, struct _trt__GetCompatibleMetadataConfigurations *trt__GetCompatibleMetadataConfigurations, struct _trt__GetCompatibleMetadataConfigurationsResponse *trt__GetCompatibleMetadataConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleAudioOutputConfigurations(struct soap* soap, struct _trt__GetCompatibleAudioOutputConfigurations *trt__GetCompatibleAudioOutputConfigurations, struct _trt__GetCompatibleAudioOutputConfigurationsResponse *trt__GetCompatibleAudioOutputConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetCompatibleAudioDecoderConfigurations(struct soap* soap, struct _trt__GetCompatibleAudioDecoderConfigurations *trt__GetCompatibleAudioDecoderConfigurations, struct _trt__GetCompatibleAudioDecoderConfigurationsResponse *trt__GetCompatibleAudioDecoderConfigurationsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetVideoSourceConfiguration(struct soap* soap, struct _trt__SetVideoSourceConfiguration *trt__SetVideoSourceConfiguration, struct _trt__SetVideoSourceConfigurationResponse *trt__SetVideoSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetVideoEncoderConfiguration(struct soap* soap, struct _trt__SetVideoEncoderConfiguration *trt__SetVideoEncoderConfiguration, struct _trt__SetVideoEncoderConfigurationResponse *trt__SetVideoEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetAudioSourceConfiguration(struct soap* soap, struct _trt__SetAudioSourceConfiguration *trt__SetAudioSourceConfiguration, struct _trt__SetAudioSourceConfigurationResponse *trt__SetAudioSourceConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetAudioEncoderConfiguration(struct soap* soap, struct _trt__SetAudioEncoderConfiguration *trt__SetAudioEncoderConfiguration, struct _trt__SetAudioEncoderConfigurationResponse *trt__SetAudioEncoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetVideoAnalyticsConfiguration(struct soap* soap, struct _trt__SetVideoAnalyticsConfiguration *trt__SetVideoAnalyticsConfiguration, struct _trt__SetVideoAnalyticsConfigurationResponse *trt__SetVideoAnalyticsConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetMetadataConfiguration(struct soap* soap, struct _trt__SetMetadataConfiguration *trt__SetMetadataConfiguration, struct _trt__SetMetadataConfigurationResponse *trt__SetMetadataConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetAudioOutputConfiguration(struct soap* soap, struct _trt__SetAudioOutputConfiguration *trt__SetAudioOutputConfiguration, struct _trt__SetAudioOutputConfigurationResponse *trt__SetAudioOutputConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetAudioDecoderConfiguration(struct soap* soap, struct _trt__SetAudioDecoderConfiguration *trt__SetAudioDecoderConfiguration, struct _trt__SetAudioDecoderConfigurationResponse *trt__SetAudioDecoderConfigurationResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoSourceConfigurationOptions(struct soap* soap, struct _trt__GetVideoSourceConfigurationOptions *trt__GetVideoSourceConfigurationOptions, struct _trt__GetVideoSourceConfigurationOptionsResponse *trt__GetVideoSourceConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetVideoEncoderConfigurationOptions(struct soap* soap, struct _trt__GetVideoEncoderConfigurationOptions *trt__GetVideoEncoderConfigurationOptions, struct _trt__GetVideoEncoderConfigurationOptionsResponse *trt__GetVideoEncoderConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
    static struct tt__VideoEncoderConfigurationOptions sVideoEncodeConfigureationOptions;
    static struct tt__IntRange sIntqualittRange;
    static struct tt__JpegOptions sJpegop;
    static struct tt__VideoResolution stResolutionsAvailables[4];
    static struct tt__IntRange stFrameRateRanges;
    static struct tt__H264Options stH264s;
    static struct tt__VideoResolution stdResolutionsAvailables[4];
    static struct tt__IntRange stdFrameRateRange;
    static enum tt__H264Profile stdH264ProfilesSupport[4];
    static struct tt__VideoEncoderOptionsExtension stdvideoeencoderextension;
    static struct tt__JpegOptions2 stdjpeg;
    static struct tt__VideoResolution stJResolutionsAvailables[4];
    static struct tt__IntRange stJFrameRateRanges;
    static struct tt__IntRange stJdBitrateRange;
    static struct tt__H264Options2 stdh264;
    static struct tt__VideoResolution stdhResolutionsAvailables[5];
    static struct tt__IntRange stdhFrameRateRange;
    static enum tt__H264Profile stdhH264ProfilesSupport[4];
    static struct tt__IntRange stsdBitrateRange;

    char chConfigurationToken[128];
    if ((!trt__GetVideoEncoderConfigurationOptions->ConfigurationToken && !trt__GetVideoEncoderConfigurationOptions->ProfileToken) || !trt__GetVideoEncoderConfigurationOptions->ConfigurationToken ) {
        sprintf(chConfigurationToken, "%s", "video_encoder_config1");
    } else {
        sprintf(chConfigurationToken, "%s", trt__GetVideoEncoderConfigurationOptions->ConfigurationToken);
    }
    //static struct VideoEncoderConfigurationOptionsTable tpc;
    if (strcmp(chConfigurationToken, "video_encoder_config1") == 0) {
        sIntqualittRange.Min = 0;
        sIntqualittRange.Max = 5;

        sJpegop.__sizeResolutionsAvailable = 4;
        stResolutionsAvailables[0].Width      = 1920;
        stResolutionsAvailables[0].Height     = 1080;
        stResolutionsAvailables[1].Width      = 1280;
        stResolutionsAvailables[1].Height     = 720;
        stResolutionsAvailables[2].Width      = 720;
        stResolutionsAvailables[2].Height     = 576;
        stResolutionsAvailables[3].Width      = 720;
        stResolutionsAvailables[3].Height     = 480;

        stFrameRateRanges.Min              = 1;
        stFrameRateRanges.Max              = 30;

        stH264s.__sizeResolutionsAvailable     = 4;
        stdResolutionsAvailables[0].Width      = 1920;
        stdResolutionsAvailables[0].Height     = 1080;
        stdResolutionsAvailables[1].Width      = 1280;
        stdResolutionsAvailables[1].Height     = 720;
        stdResolutionsAvailables[2].Width      = 720;
        stdResolutionsAvailables[2].Height     = 576;
        stdResolutionsAvailables[3].Width      = 720;
        stdResolutionsAvailables[3].Height     = 480;
        
        stdFrameRateRange.Min         = 1;
        stdFrameRateRange.Max         = 30;

        stdH264ProfilesSupport[0] = tt__H264Profile__Baseline;
        stdH264ProfilesSupport[1] = tt__H264Profile__Main;
        stdH264ProfilesSupport[2] = tt__H264Profile__Extended;
        stdH264ProfilesSupport[3] = tt__H264Profile__High;
        stH264s.__sizeH264ProfilesSupported            = 4;

        stdjpeg.__sizeResolutionsAvailable = 4;
        stJResolutionsAvailables[0].Width      = 1920;
        stJResolutionsAvailables[0].Height     = 1080;
        stJResolutionsAvailables[1].Width      = 1280;
        stJResolutionsAvailables[1].Height     = 720;
        stJResolutionsAvailables[2].Width      = 720;
        stJResolutionsAvailables[2].Height     = 576;
        stJResolutionsAvailables[3].Width      = 720;
        stJResolutionsAvailables[3].Height     = 480;
        
        stJFrameRateRanges.Min              = 1;
        stJFrameRateRanges.Max              = 30;

       
        stJdBitrateRange.Min  = 512;
        stJdBitrateRange.Max  = 8192;

        stdh264.__sizeResolutionsAvailable     = 5;
        stdhResolutionsAvailables[0].Width      = 1920;
        stdhResolutionsAvailables[0].Height     = 1080;
        stdhResolutionsAvailables[1].Width      = 1280;
        stdhResolutionsAvailables[1].Height     = 720;
        stdhResolutionsAvailables[2].Width      = 720;
        stdhResolutionsAvailables[2].Height     = 576;
        stdhResolutionsAvailables[3].Width      = 720;
        stdhResolutionsAvailables[3].Height     = 480;


        stdhFrameRateRange.Min         = 1;
        stdhFrameRateRange.Max         = 30;

        stdhH264ProfilesSupport[0] = tt__H264Profile__Baseline;
        stdhH264ProfilesSupport[1] = tt__H264Profile__Main;
        stdhH264ProfilesSupport[2] = tt__H264Profile__Extended;
        stdhH264ProfilesSupport[3] = tt__H264Profile__High;
        stdh264.__sizeH264ProfilesSupported            = 4;

        stsdBitrateRange.Min                            = 512;//
        stsdBitrateRange.Max                            = 8192;//
 
        stdh264.BitrateRange                           = &stsdBitrateRange;
        stdh264.H264ProfilesSupported                  = stdhH264ProfilesSupport;

        stdh264.FrameRateRange                         = &stdhFrameRateRange;
        stdh264.ResolutionsAvailable                   = stdhResolutionsAvailables;
        stdvideoeencoderextension.H264                 = &stdh264;
        sVideoEncodeConfigureationOptions.Extension    = &stdvideoeencoderextension;
        stdjpeg.FrameRateRange                         = &stJFrameRateRanges;

        stdjpeg.BitrateRange                           = &stJdBitrateRange;
        stdjpeg.ResolutionsAvailable                   = stJResolutionsAvailables;
        stdvideoeencoderextension.JPEG                 = &stdjpeg;
        sVideoEncodeConfigureationOptions.Extension    = &stdvideoeencoderextension;
        stH264s.H264ProfilesSupported                  = stdH264ProfilesSupport;

        stH264s.FrameRateRange                         = &stdFrameRateRange;
        stH264s.ResolutionsAvailable                   = stdResolutionsAvailables;
        sJpegop.FrameRateRange                         = &stFrameRateRanges;
        sJpegop.ResolutionsAvailable                   = stResolutionsAvailables;
        sVideoEncodeConfigureationOptions.H264         = &stH264s;
        sVideoEncodeConfigureationOptions.JPEG         = &sJpegop;
        sVideoEncodeConfigureationOptions.QualityRange = &sIntqualittRange;
        trt__GetVideoEncoderConfigurationOptionsResponse->Options = &sVideoEncodeConfigureationOptions;
    } else if (strcmp(chConfigurationToken, "video_encoder_config2") ==  0) {
               
        static struct tt__VideoEncoderConfigurationOptions VideoEncodeConfigureationOptions;
        static struct tt__IntRange IntqualittRange;
        static struct tt__JpegOptions Jpegop;
        static struct tt__VideoResolution tResolutionsAvailables[2];
        static struct tt__IntRange tFrameRateRanges;
        static struct tt__H264Options tH264s;
        static struct tt__VideoResolution tdResolutionsAvailables[2];
        static struct tt__IntRange tdFrameRateRange;
        static enum tt__H264Profile tdH264ProfilesSupport[4];
        static struct tt__VideoEncoderOptionsExtension tdvideoeencoderextension;
        static struct tt__JpegOptions2 tdjpeg;
        static struct tt__VideoResolution tJResolutionsAvailables[2];
        static struct tt__IntRange tdBitrateRange;    
        static struct tt__IntRange tJFrameRateRanges;
        static struct tt__H264Options2 tdhh264;
        static struct tt__VideoResolution thResolutionsAvailables[2];
        static enum tt__H264Profile tdhH264ProfilesSupport[4];
        static struct tt__IntRange sthFrameRateRange;
        static struct tt__IntRange tsdBitrateRange;

        IntqualittRange.Min = 1;
        IntqualittRange.Max = 5;
        Jpegop.__sizeResolutionsAvailable = 2;
        tResolutionsAvailables[0].Width   = 720;
        tResolutionsAvailables[0].Height  = 576;
        tResolutionsAvailables[1].Width   = 720;
        tResolutionsAvailables[1].Height  = 480;

        tFrameRateRanges.Min              = 1;
        tFrameRateRanges.Max              = 30;

        tH264s.__sizeResolutionsAvailable  = 2;
        tdResolutionsAvailables[0].Width   = 720;
        tdResolutionsAvailables[0].Height  = 576;
        tdResolutionsAvailables[1].Width   = 720;
        tdResolutionsAvailables[1].Height  = 480;

        tdFrameRateRange.Min         = 1;
        tdFrameRateRange.Max         = 30;
        tdH264ProfilesSupport[0] = tt__H264Profile__Baseline;
        tdH264ProfilesSupport[1] = tt__H264Profile__Main;
        tdH264ProfilesSupport[2] = tt__H264Profile__Extended;
        tdH264ProfilesSupport[3] = tt__H264Profile__High;
        tH264s.__sizeH264ProfilesSupported  = 4;

        tdjpeg.__sizeResolutionsAvailable = 2;
        tJResolutionsAvailables[0].Width   = 720;
        tJResolutionsAvailables[0].Height  = 576;
        tJResolutionsAvailables[1].Width   = 720;
        tJResolutionsAvailables[1].Height  = 480;

        tdBitrateRange.Min                            =  1000;//tpc.extension.JpegBitrateRange.Min
        tdBitrateRange.Max                            = 4000;//tpc.extension.JpegBitrateRange.Max

        tJFrameRateRanges.Min              = 1;
        tJFrameRateRanges.Max              = 30;

        tdhh264.__sizeResolutionsAvailable  = 2;
        thResolutionsAvailables[0].Width   = 720;
        thResolutionsAvailables[0].Height  = 576;
        thResolutionsAvailables[1].Width   = 720;
        thResolutionsAvailables[1].Height  = 480;

        
        tdhH264ProfilesSupport[0] = tt__H264Profile__Baseline;
        tdhH264ProfilesSupport[1] = tt__H264Profile__Main;
        tdhH264ProfilesSupport[2] = tt__H264Profile__Extended;
        tdhH264ProfilesSupport[3] = tt__H264Profile__High;
        tdhh264.__sizeH264ProfilesSupported            = 4;

        sthFrameRateRange.Min         = 1;
        sthFrameRateRange.Max         = 30;
        
        tsdBitrateRange.Min                            = 1000;
        tsdBitrateRange.Max                            = 4000;
        tdhh264.BitrateRange                           = &tsdBitrateRange;
        tdhh264.H264ProfilesSupported                  = tdhH264ProfilesSupport;
        tdhh264.FrameRateRange                         = &sthFrameRateRange;
        tdhh264.ResolutionsAvailable                   = thResolutionsAvailables;
        tdvideoeencoderextension.H264                 = &tdhh264;
        tdjpeg.FrameRateRange                         = &tJFrameRateRanges;
        tdjpeg.BitrateRange                           = &tdBitrateRange;
        tdjpeg.ResolutionsAvailable                   = tJResolutionsAvailables;
        tdvideoeencoderextension.JPEG                 = &tdjpeg;
        VideoEncodeConfigureationOptions.Extension    = &tdvideoeencoderextension;
        tH264s.H264ProfilesSupported                  = tdH264ProfilesSupport;
        tH264s.FrameRateRange                         = &tdFrameRateRange;
        tH264s.ResolutionsAvailable                   = tdResolutionsAvailables;
        Jpegop.FrameRateRange                         = &tFrameRateRanges;
        Jpegop.ResolutionsAvailable                   = tResolutionsAvailables;
        VideoEncodeConfigureationOptions.H264         = &tH264s;
        VideoEncodeConfigureationOptions.JPEG         = &Jpegop;
        VideoEncodeConfigureationOptions.QualityRange = &IntqualittRange;
        trt__GetVideoEncoderConfigurationOptionsResponse->Options   = &VideoEncodeConfigureationOptions;
    } else {
        return SOAP_FAULT;
    }
    return SOAP_OK;
}

int __trt__GetAudioSourceConfigurationOptions(struct soap* soap, struct _trt__GetAudioSourceConfigurationOptions *trt__GetAudioSourceConfigurationOptions, struct _trt__GetAudioSourceConfigurationOptionsResponse *trt__GetAudioSourceConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioEncoderConfigurationOptions(struct soap* soap, struct _trt__GetAudioEncoderConfigurationOptions *trt__GetAudioEncoderConfigurationOptions, struct _trt__GetAudioEncoderConfigurationOptionsResponse *trt__GetAudioEncoderConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetMetadataConfigurationOptions(struct soap* soap, struct _trt__GetMetadataConfigurationOptions *trt__GetMetadataConfigurationOptions, struct _trt__GetMetadataConfigurationOptionsResponse *trt__GetMetadataConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioOutputConfigurationOptions(struct soap* soap, struct _trt__GetAudioOutputConfigurationOptions *trt__GetAudioOutputConfigurationOptions, struct _trt__GetAudioOutputConfigurationOptionsResponse *trt__GetAudioOutputConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetAudioDecoderConfigurationOptions(struct soap* soap, struct _trt__GetAudioDecoderConfigurationOptions *trt__GetAudioDecoderConfigurationOptions, struct _trt__GetAudioDecoderConfigurationOptionsResponse *trt__GetAudioDecoderConfigurationOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetGuaranteedNumberOfVideoEncoderInstances(struct soap* soap, struct _trt__GetGuaranteedNumberOfVideoEncoderInstances *trt__GetGuaranteedNumberOfVideoEncoderInstances, struct _trt__GetGuaranteedNumberOfVideoEncoderInstancesResponse *trt__GetGuaranteedNumberOfVideoEncoderInstancesResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetStreamUri(struct soap* soap, struct _trt__GetStreamUri *trt__GetStreamUri, struct _trt__GetStreamUriResponse *trt__GetStreamUriResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
        if (trt__GetStreamUri->StreamSetup) {
        if (trt__GetStreamUri->StreamSetup->Stream == 1) {
            return soap_sender_fault_subcode(soap, "ter:InvalidArgVal/ter:InvalidStreamSetup", "Specification of StreamType or Transport part in StreamSetup is not supported.", NULL);
        }
        if (trt__GetStreamUri->StreamSetup->Transport->Protocol== 3) {
            return soap_sender_fault_subcode(soap, "ter:InvalidArgVal/ter:InvalidStreamSetup", "The HTTP is not supported.", NULL);
        }
    } else {
        return soap_sender_fault_subcode(soap, "ter:InvalidArgVal/ter:GetStreamUri", "Invalid GetStreamUri.", NULL);
    }
    static struct tt__MediaUri tMediaUri;
    static char Dev_Addr[64];
    
    if (strcmp(trt__GetStreamUri->ProfileToken, "media_profile1") ==  0) {
        sprintf(Dev_Addr, "rtsp://%s/test.264", get_local_ip("eth0"));  // 流地址
    } else    if (strcmp(trt__GetStreamUri->ProfileToken, "media_profile2") ==  0) {
        sprintf(Dev_Addr, "rtsp://%s/test.264",  get_local_ip("eth0"));
    } else {
        return SOAP_ERR;
    }
    tMediaUri.Uri                 = Dev_Addr;
    tMediaUri.InvalidAfterConnect = xsd__boolean__false_;
    tMediaUri.InvalidAfterReboot  = xsd__boolean__false_;
    static LONG64 SmTimeout;
    soap_s2xsd__duration(soap,"PT100S",&SmTimeout);
    tMediaUri.Timeout             = SmTimeout;//"PT100S";
    trt__GetStreamUriResponse->MediaUri = &tMediaUri;
    return SOAP_OK;
}

int __trt__StartMulticastStreaming(struct soap* soap, struct _trt__StartMulticastStreaming *trt__StartMulticastStreaming, struct _trt__StartMulticastStreamingResponse *trt__StartMulticastStreamingResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__StopMulticastStreaming(struct soap* soap, struct _trt__StopMulticastStreaming *trt__StopMulticastStreaming, struct _trt__StopMulticastStreamingResponse *trt__StopMulticastStreamingResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetSynchronizationPoint(struct soap* soap, struct _trt__SetSynchronizationPoint *trt__SetSynchronizationPoint, struct _trt__SetSynchronizationPointResponse *trt__SetSynchronizationPointResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetSnapshotUri(struct soap* soap, struct _trt__GetSnapshotUri *trt__GetSnapshotUri, struct _trt__GetSnapshotUriResponse *trt__GetSnapshotUriResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
    static struct tt__MediaUri tMediaUri;
    static char Dev_Addr[64];

    sprintf(Dev_Addr, "http://%s/images/snapshot.jpeg",  get_local_ip("eth0")); // onvif地址
    tMediaUri.Uri                   = Dev_Addr;//"http://192.168.10.128/images/snapshot.png";
    tMediaUri.InvalidAfterConnect   = xsd__boolean__false_;
    tMediaUri.InvalidAfterReboot    = xsd__boolean__false_;
    static LONG64 SmTimeout;
    soap_s2xsd__duration(soap,"PT100S",&SmTimeout);
    tMediaUri.Timeout               = SmTimeout;//"PT100S";
    trt__GetSnapshotUriResponse->MediaUri                = &tMediaUri;
    return SOAP_OK;
}

int __trt__GetVideoSourceModes(struct soap* soap, struct _trt__GetVideoSourceModes *trt__GetVideoSourceModes, struct _trt__GetVideoSourceModesResponse *trt__GetVideoSourceModesResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetVideoSourceMode(struct soap* soap, struct _trt__SetVideoSourceMode *trt__SetVideoSourceMode, struct _trt__SetVideoSourceModeResponse *trt__SetVideoSourceModeResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetOSDs(struct soap* soap, struct _trt__GetOSDs *trt__GetOSDs, struct _trt__GetOSDsResponse *trt__GetOSDsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetOSD(struct soap* soap, struct _trt__GetOSD *trt__GetOSD, struct _trt__GetOSDResponse *trt__GetOSDResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__GetOSDOptions(struct soap* soap, struct _trt__GetOSDOptions *trt__GetOSDOptions, struct _trt__GetOSDOptionsResponse *trt__GetOSDOptionsResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__SetOSD(struct soap* soap, struct _trt__SetOSD *trt__SetOSD, struct _trt__SetOSDResponse *trt__SetOSDResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__CreateOSD(struct soap* soap, struct _trt__CreateOSD *trt__CreateOSD, struct _trt__CreateOSDResponse *trt__CreateOSDResponse)
{
	
	printf("%s:%d\n",__FUNCTION__,__LINE__);
}

int __trt__DeleteOSD(struct soap* soap, struct _trt__DeleteOSD *trt__DeleteOSD, struct _trt__DeleteOSDResponse *trt__DeleteOSDResponse)
{
	return;
}

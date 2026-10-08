#ifndef _HH_OSD_API_H
#define _HH_OSD_API_H

//海思规定每个编码组只支持4 个osd叠加区域
//如果要增加osd 区域需修改以下两个参数

#include <ot_type.h>
#include <ot_common.h>
#include <ot_common_region.h>
//swann add
typedef td_u32 VENC_GRP;
#define HI_ID_GROUP  6
#define PIXEL_FORMAT_RGB_1555 8

#define COMM_SYSLOG(value,fmt, args...) do { printf("[%s, %s, %d] "fmt, __FILE__, __FUNCTION__, __LINE__, ##args); } while(0)
#define VIDEO_NUM	1


#define HH_MAX_OSD_NUM	  48   //(4*3*3)

#define HH_MAX_OSD_PER_GROUP    2  //目前 victa 只用2个


#define HH_MAX_TITLE_NUM  256
#define HH_MAX_TITLE_LINE 11



typedef struct _HI_OSD_ORG
{
	td_u16      nOrgX;       //osd 区域的左上角X坐标
	td_u16      nOrgY;       //osd 区域的左上角Y坐标
}HI_OSD_ORG, *PHI_OSD_ORG;

typedef struct _HI_OSD_COLOR
{
	td_u16      bgColor;     //osd 背景颜色
	td_u16      fgColor;     //osd 前景颜色
	td_u16		bgAlpha;     //osd 背景透明度
	td_u16		fgAlpha;	 //osd 前景透明度
	td_u16		edgeColor;	 //osd 前景边缘颜色
	td_u16      selColor;    //osd 菜单选中行颜色
}HI_OSD_COLOR, *PHI_OSD_COLOR;

typedef struct _HH_OSD_LOGO
{
	VENC_GRP     vencGroup ;   //osd 区域叠加到的编码组
	td_u8        nStatus   ;   //1 显示 0 隐藏
	td_u8        nOsdLine  ;   //osd 字幕的行数
	td_u8        nOsdFontH ;
	td_u8        nOsdFontW ;
	td_u16		 nWidth    ;      //osd 区域的宽度
	td_u16		 nHeight   ;	 //osd 区域的高度
	td_u16       nImageWidth;
	td_u16       nImageHeight;
	td_s16       nFontInt  ;    //osd 字符间隔
	td_u16       nVencFeild;
	td_u8        nOsdTitleMaxLen; ////标记最长的一行字符的个数
	td_u8        nOsdTitleFlag[HH_MAX_TITLE_LINE]; //osd 最大支持8 行 标记每一行osd 第一个字符在osdtitle 中的位置
	td_u8        nOsdTitleLen[HH_MAX_TITLE_LINE];  //标记每一行osd 的长度
	char         szOsdTitle[HH_MAX_TITLE_NUM];   //osd 叠加字幕信息
	HI_OSD_ORG   osdPos    ;   //
	HI_OSD_COLOR osdColor  ;   //
	td_u8          *pBmpBuf;
//#if defined(HI3516_ARCH) || defined(HI3518_ARCH)
	int			  regHdl;		//osd 区域句柄

	int          nSelLine  ;    //标记当前行为选中行
}HH_OSD_LOGO, *PHH_OSD_LOGO;

typedef struct _HH_VIDEO_RESOLUTION
{
	int      width;
	int      height;
}HH_VIDEO_RESOLUTION, *PHH_VIDEO_RESOLUTION;

HH_OSD_LOGO *HI_Create_Osd(int index, HH_OSD_LOGO **ppLog, VENC_GRP vencGroup,
						   HI_OSD_ORG	osdOrg, HI_OSD_COLOR osdColor,
 							char *pSzTitle, td_s16 nFontInt, td_u8 nShow);

int  HI_OSD_Parse_OsdTitle(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Get_BmpSize(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Set_Show(HH_OSD_LOGO *pOsdLogo, int show);

int  HI_Create_Osd_Reg(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Create_Bitmap(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Refresh_Color(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Refresh_Title(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Refresh_Org(HH_OSD_LOGO *pOsdLogo);
int HI_OSD_Check_ImaSizeChange(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Destroy(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_Build(HH_OSD_LOGO *pOsdLogo);

int  HI_OSD_GetCharBit(unsigned char *in, short qu, short wei, short fontSize);

int  HI_OSD_DrawBitmapBit(int  x_pos, td_u8 *bitmap, td_u16 fColor, td_u16 bColor,
						  td_u16 edgeColor, td_u8 *bmpBuffer, td_u16 bmpWidth,
						  td_u16 fontSize);

int  HI_OSD_DrawBitmapBit32(int x_pos, td_u8 *bitmap, td_u16 fColor, td_u16 bColor,
						  td_u16 edgeColor, td_u8 *bmpBuffer, td_u16 bmpWidth,
						  td_u16 fontSize);

void HI_OSD_CreateBMP(int bTwoFeild,  int nSize, int bmpWidth, int space,
					  char *bmpBuffer, char *string, td_u16 fColor,
					  td_u16 bColor, td_u16 edgeColor);

int  HI_OSD_Refresh_SelLine(HH_OSD_LOGO *pOsdLogo, int nLine);

#endif


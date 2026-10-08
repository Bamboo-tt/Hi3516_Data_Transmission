To reduce the CPU usage when playing YUV, add the line below At the beginning of the file  open_source/linux/linux-5.10.y/drivers/media/usb/uvc/uvc_video.c :
#define CONFIG_DMA_NONCOHERENT


When the NV21 format is played, the screen image flickers and the memcpy error is reported on serial port. This is because the kernel does not support NV21 in UVC host driver.
Modify as follows:

1. drivers\media\usb\uvc\uvcvideo.h
add a macro before #define UVC_GUID_FORMAT_NV12 : 
#define UVC_GUID_FORMAT_NV21 \
	{ 'N', 'V', '2', '1', 0x00, 0x00, 0x10, 0x00, \
	0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}

2. drivers\media\usb\uvc\uvc_driver.c
add an struct item in array :
static struct uvc_format_desc uvc_fmts[] ... {
...
    {
        .name = "YUV 4:2:0 (NV21)",
        .guid = UVC_GUID_FORMAT_NV21,
        .fcc = V4L2_PIX_FMT_NV21,
    },
...
}
	
rebuild kernel and burn it.


The UVC camera device's max resolution affects the V4L2 buffer size, if max resolution is very large, the sample may require more OS memory，to avoid being killed due to OOM(Out of memory),
we need to expand the Linux OS memory in bootargs(mem=?) and reduce the MPP MMZ memory in loadko script(mmz_start=?, mmz_size=?)。


How to change USB driver to HOST mode ?  Run this command on the board:
echo host     > /proc/10300000.usb20drd/mode

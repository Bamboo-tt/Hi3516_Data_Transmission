Compile:
	only compile uvc default, if need uac, please change the value of "UAC_COMPILE" into "y" in Makefile.
	$ make clean
	$ make

This sample relies on ot_uvc.ko: check load3516cv610_xxx and make sure "insmod ot_uvc.ko" and "rmmod ot_uvc.ko" are opened.

YUV fomat high resolution like 2560*1440 and 1920*1080 may require more OS memory，to avoid being killed due to OOM(Out of memory),
we need to expand the Linux OS memory in bootargs(mem=?) and reduce the MPP MMZ memory in loadko script(mmz_start=?, mmz_size=?)。

There are 4 data input mode to select:
1. OT_UVC_MPP_BIND_UVC: for venc stream or yuv frame, VENC bind UVC or VPSS/VI bind UVC, stream data flows in the kernel space;
2. OT_UVC_SEND_VENC_STREAM: user get VENC stream from venc channel, save or send out by network, then send venc stream to UVC by ss_mpi_uvc_send_stream();
3. OT_UVC_SEND_YUV_FRAME: user get YUV frame from VI/VPSS, then send it to UVC by ss_mpi_uvc_send_frame();
4. OT_UVC_SEND_USER_STREAM: user have private buffer that contains encoded stream, send it to UVC by ss_mpi_uvc_send_user_stream(), this is not for YUV frame, we can use VI/VPSS bind UVC for YUV frame.

uvc_media.c has a global variable: g_data_input_mode, it decides the send mode.If g_data_input_mode is changed, then rebuild the uvc_app sample.


VPSS online mode not support zoom-in, if the preset resolution larger than the sensor input resolution, you should remove the larger resolution.
For example, if the sensor not support 2560x1440, remove all the line that include 2560...1440 in uvc.h:
	{ 2560, 1440, {333333,       0}, 32768, 3072 }
then remove the 1440p when export the format's resolution before run the ConfigUVC.sh on board:
	export NV21="360p 720p 1080p"
	export MJPEG="360p 720p 1080p"
	export H264="360p 720p 1080p"
	export H265="360p 720p 1080p"

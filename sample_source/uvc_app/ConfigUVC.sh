#!/bin/sh

######################################################
# set_resolution()  fill resoluton
# $1 formats
# $2 base_path
# $3 0 or 1: means need fill dwMaxVideoFrameBufferSize
# $4 format name string
#######################################################
function set_resolution()
{
	for i in $1
	do
		echo "$i"
		case $i in
		"360p")
			mkdir $2/360p/
			echo -e "333333\n400000" > $2/360p/dwFrameInterval
			echo "400000" > $2/360p/dwDefaultFrameInterval
			echo "110592000" > $2/360p/dwMaxBitRate
			if [ $3 -eq 1 ]; then
				echo "460800" > $2/360p/dwMaxVideoFrameBufferSize
			fi
			echo "110592000" > $2/360p/dwMinBitRate
			echo "360" > $2/360p/wHeight
			echo "640" > $2/360p/wWidth
			;;
		"480p")
			mkdir $2/480p/
			echo -e "333333\n400000" > $2/480p/dwFrameInterval
			echo "400000" > $2/480p/dwDefaultFrameInterval
			echo "147456000" > $2/480p/dwMaxBitRate
			if [ $3 -eq 1 ]; then
				echo "614400" > $2/480p/dwMaxVideoFrameBufferSize
			fi
			echo "147456000" > $2/480p/dwMinBitRate
			echo "480" > $2/480p/wHeight
			echo "640" > $2/480p/wWidth
			;;
		"720p")
			mkdir $2/720p/
			echo -e "333333\n400000" > $2/720p/dwFrameInterval
			echo "400000" > $2/720p/dwDefaultFrameInterval
			echo "442368000" > $2/720p/dwMaxBitRate
			if [ $3 -eq 1 ]; then
				echo "1843200" > $2/720p/dwMaxVideoFrameBufferSize
			fi
			echo "442368000" > $2/720p/dwMinBitRate
			echo "720" > $2/720p/wHeight
			echo "1280" > $2/720p/wWidth
			;;
		"1080p")
			mkdir $2/1080p/
			echo -e "333333\n400000"> $2/1080p/dwFrameInterval
			echo "400000" > $2/1080p/dwDefaultFrameInterval
			echo "995328000" > $2/1080p/dwMaxBitRate
			if [ $3 -eq 1 ]; then
				echo "4147200" > $2/1080p/dwMaxVideoFrameBufferSize
			fi
			echo "995328000" > $2/1080p/dwMinBitRate
			echo "1080" > $2/1080p/wHeight
			echo "1920" > $2/1080p/wWidth
			;;
		"2160p")
			mkdir $2/2160p/
			echo -e "333333\n400000" > $2/2160p/dwFrameInterval
			echo "400000" > $2/2160p/dwDefaultFrameInterval
			echo "3981312000" > $2/2160p/dwMaxBitRate
			if [ $3 -eq 1 ]; then
				echo "16588800" > $2/2160p/dwMaxVideoFrameBufferSize
			fi
			echo "3981312000" > $2/2160p/dwMinBitRate
			echo "2160" > $2/2160p/wHeight
			echo "3840" > $2/2160p/wWidth
			;;
		*)
			echo "$4 $i is invalid!"
			;;
		esac
	done
}

######################################################
# set_format()  fill format
# no argument
#######################################################
function set_format()
{
	#YUV
	if [ -n "$YUYV" ]; then
		echo "Add YUYV..."
		mkdir streaming/uncompressed/yuy2/
		echo -en "\x59\x55\x59\x32\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71" > streaming/uncompressed/yuy2/guidFormat
		echo 16 > streaming/uncompressed/yuy2/bBitsPerPixel

		set_resolution "$YUYV" streaming/uncompressed/yuy2/ 1 "YUYV"

		ln -s streaming/uncompressed/yuy2/ streaming/header/h/
		echo -e "Added YUYV\n"
	fi

	#NV21
	if [ -n "$NV21" ]; then
		echo "Add NV21..."
		mkdir streaming/uncompressed/nv21/
		echo -en "\x4E\x56\x32\x31\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71" > streaming/uncompressed/nv21/guidFormat
		echo 12 > streaming/uncompressed/nv21/bBitsPerPixel

		set_resolution "$NV21" streaming/uncompressed/nv21/ 1 "NV21"

		ln -s streaming/uncompressed/nv21/ streaming/header/h/
		echo -e "Added NV21\n"
	fi

	#NV12
	if [ -n "$NV12" ]; then
		echo "Add NV12..."
		mkdir streaming/uncompressed/nv12/
		echo -en "\x4E\x56\x31\x32\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71" > streaming/uncompressed/nv12/guidFormat
		echo 12 > streaming/uncompressed/nv12/bBitsPerPixel

		set_resolution "$NV12" streaming/uncompressed/nv12/ 1 "NV12"

		ln -s streaming/uncompressed/nv12/ streaming/header/h/
		echo -e "Added NV12\n"
	fi

	#MJPEG
	if [ -n "$MJPEG" ]; then
		echo "Add MJPEG..."
		mkdir streaming/mjpeg/m/

		set_resolution "$MJPEG" streaming/mjpeg/m/ 1 "MJPEG"

		ln -s streaming/mjpeg/m/ streaming/header/h/
		echo -e "Added MJPEG\n"
	fi

	#H264
	if [ -n "$H264" ]; then
		echo "Add H264..."
		mkdir streaming/framebased/h264/
		echo -en "\x48\x32\x36\x34\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71" > streaming/framebased/h264/guidFormat

		set_resolution "$H264" streaming/framebased/h264/ 0 "H264"

		ln -s streaming/framebased/h264/ streaming/header/h/
		echo -e "Added H264\n"
	fi

	#HEVC aka H265
	if [ -n "$H265" ]; then
		echo "Add HEVC(H265)..."
		mkdir streaming/framebased/h265/
		#echo -en "\x48\x45\x56\x43\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71" > streaming/framebased/h265/guidFormat
		echo -en "\x48\x32\x36\x35\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71" > streaming/framebased/h265/guidFormat

		set_resolution "$H265" streaming/framebased/h265/ 0 "HEVC(H265)"

		ln -s streaming/framebased/h265/ streaming/header/h/
		echo -e "Added HEVC(H265)\n"
	fi
}

mount -t configfs none /sys/kernel/config/
cd /sys/kernel/config/usb_gadget/
mkdir camera
cd camera

echo "0x01" > bDeviceProtocol
echo "0x02" > bDeviceSubClass
echo "0xEF" > bDeviceClass
echo $VID > idVendor
echo $PID > idProduct
mkdir strings/0x409
echo $MANUFACTURER > strings/0x409/manufacturer
echo $PRODUCT > strings/0x409/product
echo $SERIALNUMBER > strings/0x409/serialnumber

mkdir functions/uvc.usb0
cd functions/uvc.usb0
mkdir control/header/h/
echo "0x0100" > control/header/h/bcdUVC
echo "48000000" > control/header/h/dwClockFrequency
ln -s control/header/h/ control/class/fs/
ln -s control/header/h/ control/class/ss/

cat <<EOF> control/terminal/camera/default/bmControls
$CamControl1
$CamControl2
$CamControl3
EOF

cat <<EOF> control/processing/default/bmControls
$ProcControl1
$ProcControl2
EOF

mkdir streaming/header/h/

set_format

ln -s streaming/header/h/ streaming/class/fs/
ln -s streaming/header/h/ streaming/class/hs/
ln -s streaming/header/h/ streaming/class/ss/

#-Create and setup configuration
cd ../../

mkdir functions/uac1.usb0
echo 8 > functions/uac1.usb0/req_number

mkdir configs/c.1/
echo "0x01" > configs/c.1/MaxPower
echo "0xc0" > configs/c.1/bmAttributes
mkdir configs/c.1/strings/0x409/
echo "Config 1" > configs/c.1/strings/0x409/configuration

ln -s functions/uvc.usb0/ configs/c.1/
ln -s functions/uac1.usb0/ configs/c.1/

echo "$(ls /sys/class/udc)" > UDC


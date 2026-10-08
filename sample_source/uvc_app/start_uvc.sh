#!/bin/sh

export VID="0x1D6B"
export PID="0x0004"
export MANUFACTURER="Vendor"
export PRODUCT="Camera"
export SERIALNUMBER="123456789012"
export CamControl1=0x0A
export CamControl2=0x00
export CamControl3=0x00
export ProcControl1=0x4F
export ProcControl2=0x14
export NV21="360p 720p 1080p 1440p"
export MJPEG="360p 720p 1080p 1440p"
export H264="360p 720p 1080p 2160p"

#cd /root
./ConfigUVC.sh
sleep 1
./sample_uvc

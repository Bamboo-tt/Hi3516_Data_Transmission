sample默认不包含aidetect

添加aidetect步骤:
1.下载智能检测模型和库(https://aimarket.megvii.com/auth/register)
2.将头文件放置到SDK/smp/a7_linux/source/out/include中，库放置到SDK/smp/a7_linux/source/out/lib，model文件夹放置到sample_smart_ae同级目录
3.打开Makefile中的宏AIDETECT_SUPPORT和aidetect库
4.编译sample

链接库：
export SDK_PATH=/Aidetect_Vx.x.x/
export SMP_PACKAGE=/home/Hi3516CV610_SDK_V1.0.x.x_B0xx/smp/
export LD_LIBRARY_PATH=$SDK_PATH/aidetect/lib/ss_lib/musl:$SMP_PACKAGE/a7_linux/source/out/lib:$LD_LIBRARY_PATH

运行命令
./sample_smart_ae class_type
其中，
class_type为0时，单独检测人脸
class_type为1时，单独检测人型
class_type为2时，同时检测人脸和人型

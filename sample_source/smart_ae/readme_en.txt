this sample does not contain aidetect

Steps to add aidetect:

1. Download the aidetect model and library. (https://aimarket.megvii.com/auth/register)
2. Place the header file in SDK/smp/a7_linux/source/out/include，library file in SDK/smp/a7_linux/source/out/lib，put the model file in the same directory as sample_smart_ae.
3. Open macro AIDETECT_SUPPORT and aidetect library in Makefile.
4. Compile the sample

Link libraries:
export SDK_PATH=/Aidetect_Vx.x.x/
export SMP_PACKAGE=/home/Hi3516CV610_SDK_V1.0.x.x_B0xx/smp/
export LD_LIBRARY_PATH=$SDK_PATH/aidetect/lib/ss_lib/musl:$SMP_PACKAGE/a7_linux/source/out/lib:$LD_LIBRARY_PATH

command:
./sample_smart_ae class_type

if class_type is 0，only detect face
if class_type is 1，only detect human
if class_type is 2，detect face and human

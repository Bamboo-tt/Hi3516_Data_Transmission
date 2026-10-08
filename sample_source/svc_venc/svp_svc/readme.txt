sample usage:

Step 1: compile sample code
    make;

Step 2: set LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=/home/xxx/mpp/out/lib:$LD_LIBRARY_PATH
    xxx is sdk package path.

Step 3: run sample
Usage : ./sample_svp_svc_main (a) [8]
index:
         (a) [8] yolov8.(VI->VPSS->SVP_NPU->VGS->SVC_VENC)
         input char
         (0): Rect mode
         (1): Mask mode 

Note:
1、 Currently only suitable for 4336P
2、 Only support yolov8
3、 Mask mode only support Hi3516CV610_00g、Hi3516CV610_20g

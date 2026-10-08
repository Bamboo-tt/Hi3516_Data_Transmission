sample usage:

Step 1: compile sample code
    make;

Step 2: set LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=xxx/source/out/lib:$LD_LIBRARY_PATH
    xxx is sdk package path.

Step 3: run sample
Usage : ./sample_svp_npu_main <index> [yolo_version]
index:
         0) svp_acl_resnet50.
         1) svp_acl_resnet50_multi_thread.
         2) svp_acl_resnet50_dynamic_batch_with_mem_cached.
         3) svp_acl_lstm.
         4) svp_acl_preemption.
         a) [8] yolov8.(VI->VPSS->SVP_NPU->VGS->VENC).

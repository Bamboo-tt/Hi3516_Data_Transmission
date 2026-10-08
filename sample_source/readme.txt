Notice:
1. If the module parameter "mem_process_isolation" of the MMZ is set to "1", the process isolation attribute of the MMZ buffer is enabled.
When you run the sample and then run the tools, tools may fail to run due to process isolation problems. In consideration of this,
the "share all" operation is reserved in the sample, you can set "MEM_SHARE" to "y" during compilation to enable this function,
such as "make MEM_SHARE=y".

2. Due to the limitation of the DDR size, the following sample list cannot run on Hi3516CV610_10B or Hi3516CV608. If running is needed, it is recommended to reduce the number of VB or enable wrap.
    sample_vie (sample_vie 0/8 VPSS_OFFLINE mode can not run, sample_vie 1/3/4/5/6/7/9 cannot run)
    sample_venc(sample_venc 6/7/8/9/10 cannot run)
    sample_quick_start
    sample_uvc
    sample_calcflicker
    sample_svp_npu_main (sample_svp_npu_main 4/a cannot run)
    sample_ive_main (sample_ive_main 6/7 cannot run)
    sample_smart_ae
    sample_svp_svc

3. In the sample_vie, if using the old sensor OS04D10, mipi_attr.lane_id needs to be configured as {0, 1, -1, -1}.
If using the new one, mipi_attr.lane_id needs to be configured as {0, 2, -1, -1}.

4. Hi3516CV610_10B&Hi3516CV608 not supports WDR mode, so the following samples cannot run:
    sample_vie 1/7/9

5. Hi3516CV608 supports only sns_type=SC4336P_MIPI_3M_30FPS_10BIT, which is cropped from SC4336P 4M through the MIPI_RX.

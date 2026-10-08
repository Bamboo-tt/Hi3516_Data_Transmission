1. Before using this sample, you should modify  smp/a7_linux/source/out/ko/load3516cv610
1.1 modify the insert_ko() function "insmod ot_vgs" into the following:
    insmod ot_vgs.ko g_max_vgs_task=200 g_max_vgs_node=200

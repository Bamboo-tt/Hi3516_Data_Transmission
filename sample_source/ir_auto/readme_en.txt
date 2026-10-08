linux:
1. Pin_mux needs to be configured for ir_auto
2. When insmod sys_config.ko, the default value of -ir_auto is 0. To use IR_AUTO, need to enable it, eg:
./load3516cv610_20s -i -ir_auto 1
3. This sample will open devices like ISP, VPSS and so on first, then run the IR_CUT switch thread. It will run right when the settings are correct.
4. This sample supports the QFN board by default. To supports the BGA board, modify the BOARD_TYPE parameter in Makefile.param in the sample directory.

Using the below commands for more information:
linux:./sample_ir_auto -h

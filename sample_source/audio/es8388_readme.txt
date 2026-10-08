This document applies only to socket boards. The usage method is as follows:
1. switch to I2C2 route
	You need to manually configure the DIP switches SW1301 and SW1302:
	1) SW1301 bit[4] set to 0
	2) SW1302 bit[4] set to 1

2. switch to i2s route
	You need to manually configure the DIP switches SW1301:
	1) SW1301 bit[1] set to 1
	2) SW1301 bit[2] set to 0

3. enable I2S0
	The data transmission interface of the ES8388 is I2S0, it need to enable I2S0. As follows：
	3.1 update the file: smp/a7_linux/source/interdrv/sysconfig/pin_mux.h
		#define I2S0_EN 1

	3.2 enter the directory and compile:
		cd smp/a7_linux/source/interdrv;
		make clean; make

4. insmod ES8388's driver on board.
	cd smp/a7_linux/source/out/ko;
	chmod +x extdrv/ot_es8388.ko;
	insmod extdrv/ot_es8388.ko

5. connect external codec ES8388.
	5.1 set ACODEC_TYPE to ACODEC_TYPE_ES8388, update the file: smp/a7_linux/source/mpp/sample/Makefile.param
		ACODEC_TYPE ?= ACODEC_TYPE_ES8388
		#ACODEC_TYPE ?= ACODEC_TYPE_INNER

	5.2 enter the directory and compile:
		cd smp/a7_linux/source/mpp/sample/audio;
		make clean; make

export OPTEE_SAMPLE_DIR=$(PWD)/..
ifeq ($(PARAM_FILE), )
	include $(OPTEE_SAMPLE_DIR)/Makefile.param
	include $(PARAM_FILE)
endif

export OPTEE_DIR:=$(OPEN_SOURCE_PATH)/optee
TA_DEV_KIT_DIR = $(OPTEE_DIR)/$(OPTEE_OS_VER)/out/arm-plat-vendor/export-ta_arm32
ifeq ($(CROSS), arm-v01c02-linux-musleabi-)
	CROSS_COMPILE=arm-v01c02-linux-musleabi-
else
	CROSS_COMPILE=$(CROSS)
endif

TARGET_PATH := $(PWD)

.PHONY: all host ta clean host_clean ta_clean

all: host ta

host:
	@cd host && make CROSS_COMPILE="$(CROSS_COMPILE)" TA_DEV_KIT_DIR="$(TA_DEV_KIT_DIR)"

ta:
	@if [ ! -f $(OPTEE_DIR)/$(OPTEE_OS_VER)/out/arm-plat-vendor/export-ta_arm32/keys/default_ta.pem ]; then \
		echo "optee does not compile, compiling..."; \
		pushd $(OPTEE_DIR); make CROSS_COMPILE=$(CROSS_COMPILE) CHIP=hi3516cv610 all -j; popd; \
	else \
		echo "optee already compile."; \
	fi
	@cd ta && make CROSS_COMPILE="$(CROSS_COMPILE)" TA_DEV_KIT_DIR="$(TA_DEV_KIT_DIR)"

clean: host_clean ta_clean

host_clean:
	@cd host && make clean

ta_clean:
	@cd ta && make clean TA_DEV_KIT_DIR="$(TA_DEV_KIT_DIR)"


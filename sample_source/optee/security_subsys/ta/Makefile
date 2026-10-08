ifeq ($(PARAM_FILE), )
	OPTEE_SAMPLE_DIR:=$(PWD)/../..
	include $(OPTEE_SAMPLE_DIR)/Makefile.param
	include $(PARAM_FILE)
	TA_DEV_KIT_DIR = $(OPTEE_DIR)/$(OPTEE_OS_VER)/out/arm-plat-vendor/export-ta_arm32
endif

CFG_TEE_TA_LOG_LEVEL ?= 4

# The UUID for the Trusted Application
BINARY=8aabf300-2450-11e4-abe2-9f02a5d5bc2f

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@rm -f *.ta
	@rm -f .sample_*.d
	@rm -f .ta*.d
	@rm -f .user_*.d
	@rm -f .sample_*.cmd
	@rm -f .ta_*.cmd
	@rm -f .user_*.cmd
	@rm -f *.o
	@rm -f *.dmp
	@rm -f *.elf
	@rm -f *.map
	@rm -f *.lds
	@rm -f dyn_list
endif

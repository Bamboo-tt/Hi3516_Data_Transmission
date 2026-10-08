include ../Makefile.param

target=$(shell ls -d */ | sed "s;/;;g" | grep -v "common")
target_clean=$(addsuffix _clean,$(target))
target_cleanall=$(addsuffix _cleanall,$(target))

export OPTEE_OS_VER = optee_os-3.20.0
export OPTEE_CLIENT_VER = optee_client-3.20.0
export OPTEE_DIR:=$(OPEN_SOURCE_PATH)/optee
OPTEE_PATH = 

ifeq ($(CROSS), arm-v01c02-linux-musleabi-)
	CROSS_COMPILE=arm-v01c02-linux-musleabi-
else
	CROSS_COMPILE=$(CROSS)
endif

.PHONY:all clean cleanall $(target) $(target_clean) target_cleanall common common_clean rsa_pem

all:$(target)
clean:$(target_clean)

common:
	@cd ../common && make
common_clean:
	@cd ../common && make clean
rsa_pem:
	@if [ ! -f $(OPTEE_DIR)/$(OPTEE_OS_VER)/out/arm-plat-vendor/export-ta_arm32/keys/default_ta.pem ]; then \
		echo "optee does not compile, compiling..."; \
		pushd $(OPTEE_DIR); make CROSS_COMPILE=$(CROSS_COMPILE) CHIP=hi3516cv610 all -j; popd; \
	else \
		echo "optee already compile."; \
	fi

$(target): common rsa_pem
	@cd $@ && make
$(target_clean): common_clean
	@cd $(subst _clean,,$@) && make clean
	@cd $(OPTEE_DIR) && make CHIP=hi3516cv610 clean

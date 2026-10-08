target=$(shell ls -d */ | sed "s;/;;g" | grep -v "common")
target_clean=$(addsuffix _clean,$(target))
target_cleanall=$(addsuffix _cleanall,$(target))

.PHONY:all clean cleanall $(target) $(target_clean) target_cleanall common common_clean

all:$(target)
clean:$(target_clean)
cleanall:$(target_cleanall)

common:
	@cd common && make
	@cd ../common && make
common_clean:
	@cd common && make clean
	@cd ../common && make clean

$(target): common
	@cd $@ && make
$(target_clean): common_clean
	@cd $(subst _clean,,$@) && make clean
$(target_cleanall): common_clean
	@cd $(subst _cleanall,,$@) && make clean && make cleanstream

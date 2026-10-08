param_file=$(realpath $(dir $(firstword $(MAKEFILE_LIST))))/../Makefile.param
include $(param_file)

SVP_COMM_SRC=$(wildcard *.c)
SVP_COMM_OBJ := $(SVP_COMM_SRC:%.c=%.o)

.PHONY : clean all
all:$(SVP_COMM_OBJ)

clean:
	@rm $(SVP_COMM_OBJ) -f

%.o:%.c
	@$(CC) $(COMM_INC) $(CFLAGS) -c $< -o $@

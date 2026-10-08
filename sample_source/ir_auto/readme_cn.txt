1. 此sample需要配置管脚复用
2. 加载sys_config.ko时, 参数-ir_auto默认配置为0，如果需要使用IR_AUTO，需要配置ir_auto为1，例如：
./load3516cv610_20s -i -ir_auto 1
3. 本sample会启动ISP，VPSS等业务，无需额外启动，只需要按照sample提示说明使用即可。
4. 本sample默认支持QFN单板，如需支持BGA板，需要修改sample目录下Makefile.param的BOARD_TYPE。

详情可以通过以下命令查看：
linux:./sample_ir_auto -h

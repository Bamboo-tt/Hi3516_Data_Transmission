This document applies only to socket and demo boards. The usage method is as follows:

AMP is controlled by GPIO1_4 multiplexed with I2C1_SCL, PWM0_OUT3, UART2_TXD and LSADC_CH0. If you need audio output from audio line out, the AMP must be enabled.

When load sys_config.ko, the amp_unmute_pin_mux function will be called to unmute the SGM8903.

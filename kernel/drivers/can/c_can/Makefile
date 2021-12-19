
ccflags-y += -Idrivers/xenomai/can -Idrivers/xenomai/can/c_can

obj-$(CONFIG_XENO_DRIVERS_CAN_C_CAN) += xeno_can_c_can.o
obj-$(CONFIG_XENO_DRIVERS_CAN_C_CAN_PLATFORM) += xeno_can_c_can_platform.o

xeno_can_c_can-y := rtcan_c_can.o rtcan_c_can_ethtool.o
xeno_can_c_can_platform-y := rtcan_c_can_platform.o

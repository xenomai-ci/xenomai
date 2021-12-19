
ccflags-y += -I$(srctree)/drivers/xenomai/can -I$(srctree)/kernel/

obj-$(CONFIG_XENO_DRIVERS_CAN) += xeno_can.o sja1000/ peak_canfd/ c_can/
obj-$(CONFIG_XENO_DRIVERS_CAN_FLEXCAN) += xeno_can_flexcan.o
obj-$(CONFIG_XENO_DRIVERS_CAN_VIRT) += xeno_can_virt.o

xeno_can-y := corectl.o rtcan_dev.o rtcan_socket.o rtcan_module.o rtcan_raw.o rtcan_raw_dev.o rtcan_raw_filter.o rtcan_ethtool.o
xeno_can_virt-y := rtcan_virt.o
xeno_can_flexcan-y := rtcan_flexcan.o

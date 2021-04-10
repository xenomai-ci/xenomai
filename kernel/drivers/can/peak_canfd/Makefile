#
# Makefile for the PEAK-System CAN-FD IP module drivers
#
ccflags-y += -I$(srctree)/drivers/xenomai/can

obj-$(CONFIG_XENO_DRIVERS_CAN_PEAK_CANFD) += xeno_can_peak_pciefd.o

xeno_can_peak_pciefd-y := rtcan_peak_pciefd.o rtcan_peak_canfd.o

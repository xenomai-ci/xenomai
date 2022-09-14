# SPDX-License-Identifier: GPL-2.0
# Copyright (c)  2018 Intel Corporation

#
# Intel(R) I225-LM/I225-V 2.5G Ethernet Controller
#

ccflags-y += -I$(srctree)/drivers/xenomai/net/stack/include

obj-$(CONFIG_XENO_DRIVERS_NET_DRV_IGC) += rt_igc.o

rt_igc-y :=  					\
	igc_i225.o				\
	igc_mac.o				\
	igc_base.o				\
	igc_dump.o				\
	igc_nvm.o				\
	igc_phy.o				\
	igc_main.o

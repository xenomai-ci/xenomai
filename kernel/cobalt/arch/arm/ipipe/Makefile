obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-$(CONFIG_IPIPE) := machine.o thread.o switch.o syscall.o

ccflags-y := -I$(srctree)/arch/arm/xenomai/include -I$(srctree)/include/xenomai

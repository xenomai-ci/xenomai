
obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-y := machine.o thread.o

xenomai-$(CONFIG_XENO_ARCH_FPU) += fpu.o

ccflags-y := -I$(srctree)/arch/powerpc/xenomai/include -I$(srctree)/include/xenomai

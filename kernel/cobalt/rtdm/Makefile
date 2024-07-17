
obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-y :=	core.o		\
		device.o	\
		drvlib.o	\
		fd.o

ccflags-y += -I$(srctree)/kernel/xenomai -I$(srctree)/kernel

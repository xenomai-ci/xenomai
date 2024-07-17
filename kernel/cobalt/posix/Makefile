
ccflags-y += -I$(srctree)/kernel

obj-$(CONFIG_XENOMAI) += xenomai.o

xenomai-y :=		\
	clock.o		\
	cond.o		\
	corectl.o	\
	event.o		\
	io.o		\
	memory.o	\
	monitor.o	\
	mqueue.o	\
	mutex.o		\
	nsem.o		\
	process.o	\
	sched.o		\
	sem.o		\
	signal.o	\
	syscall.o	\
	thread.o	\
	timer.o		\
	timerfd.o

syscall_entries := $(srctree)/kernel/xenomai/posix/gen-syscall-entries.sh

quiet_cmd_syscall_entries = GEN     $@
      cmd_syscall_entries = $(CONFIG_SHELL) '$(syscall_entries)' $(filter-out FORCE,$^) > $@

$(obj)/syscall_entries.h: $(syscall_entries) $(wildcard $(srctree)/kernel/xenomai/posix/*.c) FORCE
	$(call if_changed,syscall_entries)

target += syscall_entries.h
clean-files += syscall_entries.h

$(obj)/syscall.o: $(obj)/syscall_entries.h

xenomai-$(CONFIG_XENO_ARCH_SYS3264) += compat.o syscall32.o

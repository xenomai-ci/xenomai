/*
 * SPDX-License-Identifier: GPL-2.0
 */
#ifndef _COBALT_KERNEL_IPIPE_WRAPPERS_H
#define _COBALT_KERNEL_IPIPE_WRAPPERS_H

#include <linux/ipipe.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
#define cobalt_set_task_state(tsk, state_value)	\
	set_task_state(tsk, state_value)
#else
/*
 * The co-kernel can still set the current task state safely if it
 * runs on the head stage.
 */
#define cobalt_set_task_state(tsk, state_value)	\
	smp_store_mb((tsk)->state, (state_value))
#endif

#ifndef ipipe_root_nr_syscalls
#define ipipe_root_nr_syscalls(ti)	NR_syscalls
#endif

#endif /* !_COBALT_KERNEL_IPIPE_WRAPPERS_H */

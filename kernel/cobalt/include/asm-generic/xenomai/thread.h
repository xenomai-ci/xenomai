/*
 * Copyright (C) 2021 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _COBALT_ASM_GENERIC_THREAD_H
#define _COBALT_ASM_GENERIC_THREAD_H

#include <linux/dovetail.h>

struct xnarchtcb {
	struct dovetail_altsched_context altsched;
};

static inline
struct task_struct *xnarch_host_task(struct xnarchtcb *tcb)
{
	return tcb->altsched.task;
}

int xnarch_setup_trap_info(unsigned int vector, struct pt_regs *regs,
			   int *sig, struct kernel_siginfo *info);

#endif /* !_COBALT_ASM_GENERIC_THREAD_H */

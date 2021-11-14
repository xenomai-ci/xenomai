/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 */

#include <linux/irqstage.h>
#include <pipeline/pipeline.h>
#include <pipeline/kevents.h>
#include <cobalt/kernel/assert.h>
#include <xenomai/posix/syscall.h>

int handle_pipelined_syscall(struct irq_stage *stage, struct pt_regs *regs)
{
	if (unlikely(running_inband()))
		return handle_root_syscall(regs);

	return handle_head_syscall(stage == &inband_stage, regs);
}

int handle_oob_syscall(struct pt_regs *regs)
{
	return handle_head_syscall(false, regs);
}

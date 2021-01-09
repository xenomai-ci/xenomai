/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>
 * Copyright (C) 2005 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 */

#include <pipeline/pipeline.h>
#include <pipeline/kevents.h>
#include <cobalt/kernel/assert.h>
#include <xenomai/posix/syscall.h>

int ipipe_syscall_hook(struct ipipe_domain *ipd, struct pt_regs *regs)
{
	if (unlikely(is_secondary_domain()))
		return handle_root_syscall(regs);

	return handle_head_syscall(ipd != &xnsched_primary_domain, regs);
}

int ipipe_fastcall_hook(struct pt_regs *regs)
{
	int ret;

	ret = handle_head_syscall(false, regs);
	XENO_BUG_ON(COBALT, ret == KEVENT_PROPAGATE);

	return ret;
}

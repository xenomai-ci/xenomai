/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_IPIPE_TICK_H
#define _COBALT_KERNEL_IPIPE_TICK_H

int pipeline_install_tick_proxy(void);

void pipeline_uninstall_tick_proxy(void);

struct xnsched;
static inline bool pipeline_must_force_program_tick(struct xnsched *sched)
{
	return false;
}

#endif /* !_COBALT_KERNEL_IPIPE_TICK_H */

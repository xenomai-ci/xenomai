/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_DOVETAIL_TICK_H
#define _COBALT_KERNEL_DOVETAIL_TICK_H

int pipeline_install_tick_proxy(void);

void pipeline_uninstall_tick_proxy(void);

struct xnsched;

bool pipeline_must_force_program_tick(struct xnsched *sched);

#endif /* !_COBALT_KERNEL_DOVETAIL_TICK_H */

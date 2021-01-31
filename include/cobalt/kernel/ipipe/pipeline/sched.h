/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_IPIPE_SCHED_H
#define _COBALT_KERNEL_IPIPE_SCHED_H

struct xnthread;
struct xnsched;
struct task_struct;

void pipeline_init_shadow_tcb(struct xnthread *thread);

void pipeline_init_root_tcb(struct xnthread *thread);

int pipeline_schedule(struct xnsched *sched);

void pipeline_prep_switch_oob(struct xnthread *root);

bool pipeline_switch_to(struct xnthread *prev,
			struct xnthread *next,
			bool leaving_inband);

int pipeline_leave_inband(void);

int pipeline_leave_oob_prepare(void);

void pipeline_leave_oob_finish(void);

void pipeline_finalize_thread(struct xnthread *thread);

void pipeline_raise_mayday(struct task_struct *tsk);

void pipeline_clear_mayday(void);

#endif /* !_COBALT_KERNEL_IPIPE_SCHED_H */

/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_DOVETAIL_SCHED_H
#define _COBALT_KERNEL_DOVETAIL_SCHED_H

struct xnthread;
struct xnsched;
struct task_struct;

void pipeline_init_shadow_tcb(struct xnthread *thread);

void pipeline_init_root_tcb(struct xnthread *thread);

int ___xnsched_run(struct xnsched *sched);

static inline int pipeline_schedule(struct xnsched *sched)
{
	return run_oob_call((int (*)(void *))___xnsched_run, sched);
}

static inline void pipeline_prep_switch_oob(struct xnthread *root)
{
	/* N/A */
}

bool pipeline_switch_to(struct xnthread *prev,
			struct xnthread *next,
			bool leaving_inband);

int pipeline_leave_inband(void);

int pipeline_leave_oob_prepare(void);

void pipeline_leave_oob_finish(void);

static inline
void pipeline_finalize_thread(struct xnthread *thread)
{
	/* N/A */
}

void pipeline_raise_mayday(struct task_struct *tsk);

void pipeline_clear_mayday(void);

#endif /* !_COBALT_KERNEL_DOVETAIL_SCHED_H */

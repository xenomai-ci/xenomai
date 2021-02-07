/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_DOVETAIL_THREAD_H
#define _COBALT_KERNEL_DOVETAIL_THREAD_H

#include <linux/dovetail.h>

struct xnthread;

#define cobalt_threadinfo oob_thread_state

static inline struct cobalt_threadinfo *pipeline_current(void)
{
	return dovetail_current_state();
}

static inline
struct xnthread *pipeline_thread_from_task(struct task_struct *p)
{
	return dovetail_task_state(p)->thread;
}

#endif /* !_COBALT_KERNEL_DOVETAIL_THREAD_H */

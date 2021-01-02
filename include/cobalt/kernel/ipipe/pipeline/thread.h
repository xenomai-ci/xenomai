/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_IPIPE_THREAD_H
#define _COBALT_KERNEL_IPIPE_THREAD_H

#include <linux/ipipe.h>
#include <linux/sched.h>

struct xnthread;

#define cobalt_threadinfo ipipe_threadinfo

static inline struct cobalt_threadinfo *pipeline_current(void)
{
	return ipipe_current_threadinfo();
}

static inline struct xnthread *pipeline_thread_from_task(struct task_struct *p)
{
	return ipipe_task_threadinfo(p)->thread;
}

#endif /* !_COBALT_KERNEL_IPIPE_THREAD_H */

/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2017 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_DOVETAIL_IRQ_H
#define _COBALT_DOVETAIL_IRQ_H

#ifdef CONFIG_XENOMAI

#include <cobalt/kernel/sched.h>

/* hard irqs off. */
static inline void irq_enter_pipeline(void)
{
	struct xnsched *sched = xnsched_current();

	sched->lflags |= XNINIRQ;
}

/* hard irqs off. */
static inline void irq_exit_pipeline(void)
{
	struct xnsched *sched = xnsched_current();

	sched->lflags &= ~XNINIRQ;

	/*
	 * CAUTION: Switching stages as a result of rescheduling may
	 * re-enable irqs, shut them off before returning if so.
	 */
	if ((sched->status|sched->lflags) & XNRESCHED) {
		xnsched_run();
		if (!hard_irqs_disabled())
			hard_local_irq_disable();
	}
}

#else  /* !CONFIG_XENOMAI */

static inline void irq_enter_pipeline(void)
{
}

static inline void irq_exit_pipeline(void)
{
}

#endif	/* !CONFIG_XENOMAI */

#endif /* !_COBALT_DOVETAIL_IRQ_H */

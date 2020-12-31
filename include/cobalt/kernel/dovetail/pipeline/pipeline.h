/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_DOVETAIL_PIPELINE_H
#define _COBALT_KERNEL_DOVETAIL_PIPELINE_H

#include <linux/irq_pipeline.h>
#include <cobalt/kernel/assert.h>
#include <asm/xenomai/features.h>

typedef unsigned long spl_t;

/*
 * We only keep the LSB when testing in SMP mode in order to strip off
 * the recursion marker (0x2) the nklock may store there.
 */
#define splhigh(x)  ((x) = oob_irq_save() & 1)
#ifdef CONFIG_SMP
#define splexit(x)  oob_irq_restore(x & 1)
#else /* !CONFIG_SMP */
#define splexit(x)  oob_irq_restore(x)
#endif /* !CONFIG_SMP */
#define splmax()    oob_irq_disable()
#define splnone()   oob_irq_enable()
#define spltest()   oob_irqs_disabled()

#define is_secondary_domain()	running_inband()
#define is_primary_domain()	running_oob()

#ifdef CONFIG_SMP

static inline int pipeline_request_resched_ipi(void (*handler)(void))
{
	/* Trap the out-of-band rescheduling interrupt. */
	TODO();

	return 0;
}

static inline void pipeline_free_resched_ipi(void)
{
	/* Release the out-of-band rescheduling interrupt. */
	TODO();
}

static inline void pipeline_send_resched_ipi(const struct cpumask *dest)
{
	/*
	 * Trigger the out-of-band rescheduling interrupt on remote
	 * CPU(s).
	 */
	TODO();
}

static inline int pipeline_request_timer_ipi(void (*handler)(void))
{
	/* Trap the out-of-band timer interrupt. */
	TODO();

	return 0;
}

static inline void pipeline_free_timer_ipi(void)
{
	/* Release the out-of-band timer interrupt. */
	TODO();
}

static inline void pipeline_send_timer_ipi(const struct cpumask *dest)
{
	/*
	 * Trigger the out-of-band timer interrupt on remote CPU(s).
	 */
	TODO();
}

#else  /* !CONFIG_SMP */

static inline int pipeline_request_resched_ipi(void (*handler)(void))
{
	return 0;
}


static inline void pipeline_free_resched_ipi(void)
{
}

static inline int pipeline_request_timer_ipi(void (*handler)(void))
{
	return 0;
}

static inline void pipeline_free_timer_ipi(void)
{
}

#endif	/* CONFIG_SMP */

static inline void pipeline_prepare_panic(void)
{
	/* N/A */
}

static inline void pipeline_collect_features(struct cobalt_featinfo *f)
{
	f->clock_freq = 0;	/* N/A */
}

#endif /* !_COBALT_KERNEL_DOVETAIL_PIPELINE_H */

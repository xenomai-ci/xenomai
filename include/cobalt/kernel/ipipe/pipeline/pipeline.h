/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_IPIPE_PIPELINE_H
#define _COBALT_KERNEL_IPIPE_PIPELINE_H

#ifdef CONFIG_IPIPE_LEGACY
#error "CONFIG_IPIPE_LEGACY must be switched off"
#endif

#include <pipeline/machine.h>
#include <asm/xenomai/features.h>

#define xnsched_primary_domain  cobalt_pipeline.domain

#define PIPELINE_NR_IRQS  IPIPE_NR_IRQS

typedef unsigned long spl_t;

#define splhigh(x)  ((x) = ipipe_test_and_stall_head() & 1)
#ifdef CONFIG_SMP
#define splexit(x)  ipipe_restore_head(x & 1)
#else /* !CONFIG_SMP */
#define splexit(x)  ipipe_restore_head(x)
#endif /* !CONFIG_SMP */
#define splmax()    ipipe_stall_head()
#define splnone()   ipipe_unstall_head()
#define spltest()   ipipe_test_head()

#define is_secondary_domain()	ipipe_root_p
#define is_primary_domain()	(!ipipe_root_p)

#ifdef CONFIG_SMP

static inline int pipeline_request_resched_ipi(void (*handler)(void))
{
	return ipipe_request_irq(&cobalt_pipeline.domain,
				IPIPE_RESCHEDULE_IPI,
				(ipipe_irq_handler_t)handler,
				NULL, NULL);
}

static inline void pipeline_free_resched_ipi(void)
{
	ipipe_free_irq(&cobalt_pipeline.domain,
		IPIPE_RESCHEDULE_IPI);
}

static inline void pipeline_send_resched_ipi(const struct cpumask *dest)
{
	ipipe_send_ipi(IPIPE_RESCHEDULE_IPI, *dest);
}

static inline void pipeline_send_timer_ipi(const struct cpumask *dest)
{
	ipipe_send_ipi(IPIPE_HRTIMER_IPI, *dest);
}

#else  /* !CONFIG_SMP */

static inline int pipeline_request_resched_ipi(void (*handler)(void))
{
	return 0;
}


static inline void pipeline_free_resched_ipi(void)
{
}

#endif	/* CONFIG_SMP */

static inline void pipeline_prepare_panic(void)
{
	ipipe_prepare_panic();
}

static inline void pipeline_collect_features(struct cobalt_featinfo *f)
{
	f->clock_freq = cobalt_pipeline.clock_freq;
}

#endif /* !_COBALT_KERNEL_IPIPE_PIPELINE_H */

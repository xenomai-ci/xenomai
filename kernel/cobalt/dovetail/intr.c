/*
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/interrupt.h>
#include <linux/irq_pipeline.h>
#include <linux/tick.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/lock.h>
#include <cobalt/kernel/intr.h>

void xnintr_host_tick(struct xnsched *sched) /* hard irqs off */
{
	sched->lflags &= ~XNHTICK;
	tick_notify_proxy();
}

/*
 * Low-level core clock irq handler. This one forwards ticks from the
 * Xenomai platform timer to nkclock exclusively.
 */
void xnintr_core_clock_handler(void)
{
	struct xnsched *sched;

	xnlock_get(&nklock);
	xnclock_tick(&nkclock);
	xnlock_put(&nklock);

	/*
	 * If the core clock interrupt preempted a real-time thread,
	 * any transition to the root thread has already triggered a
	 * host tick propagation from xnsched_run(), so at this point,
	 * we only need to propagate the host tick in case the
	 * interrupt preempted the root thread.
	 */
	sched = xnsched_current();
	if ((sched->lflags & XNHTICK) &&
	    xnthread_test_state(sched->curr, XNROOT))
		xnintr_host_tick(sched);
}

static irqreturn_t xnintr_irq_handler(int irq, void *dev_id)
{
	struct xnintr *intr = dev_id;
	int ret;

	ret = intr->isr(intr);
	XENO_WARN_ON_ONCE(USER, (ret & XN_IRQ_STATMASK) == 0);

	if (ret & XN_IRQ_DISABLE)
		disable_irq(irq);
	else if (ret & XN_IRQ_PROPAGATE)
		irq_post_inband(irq);

	return ret & XN_IRQ_NONE ? IRQ_NONE : IRQ_HANDLED;
}

int xnintr_init(struct xnintr *intr, const char *name,
		unsigned int irq, xnisr_t isr, xniack_t iack,
		int flags)
{
	secondary_mode_only();

	intr->irq = irq;
	intr->isr = isr;
	intr->iack = NULL;	/* unused */
	intr->cookie = NULL;
	intr->name = name ? : "<unknown>";
	intr->flags = flags;
	intr->status = 0;
	intr->unhandled = 0;	/* unused */
	raw_spin_lock_init(&intr->lock); /* unused */

	return 0;
}
EXPORT_SYMBOL_GPL(xnintr_init);

void xnintr_destroy(struct xnintr *intr)
{
	secondary_mode_only();
	xnintr_detach(intr);
}
EXPORT_SYMBOL_GPL(xnintr_destroy);

int xnintr_attach(struct xnintr *intr, void *cookie, const cpumask_t *cpumask)
{
	cpumask_t tmp_mask, *effective_mask;
	int ret;

	secondary_mode_only();

	intr->cookie = cookie;

	if (!cpumask) {
		effective_mask = &xnsched_realtime_cpus;
	} else {
		effective_mask = &tmp_mask;
		cpumask_and(effective_mask, &xnsched_realtime_cpus, cpumask);
		if (cpumask_empty(effective_mask))
			return -EINVAL;
	}
	ret = irq_set_affinity_hint(intr->irq, effective_mask);
	if (ret)
		return ret;

	return request_irq(intr->irq, xnintr_irq_handler, IRQF_OOB,
			intr->name, intr);
}
EXPORT_SYMBOL_GPL(xnintr_attach);

void xnintr_detach(struct xnintr *intr)
{
	secondary_mode_only();
	irq_set_affinity_hint(intr->irq, NULL);
	free_irq(intr->irq, intr);
}
EXPORT_SYMBOL_GPL(xnintr_detach);

void xnintr_enable(struct xnintr *intr)
{
}
EXPORT_SYMBOL_GPL(xnintr_enable);

void xnintr_disable(struct xnintr *intr)
{
}
EXPORT_SYMBOL_GPL(xnintr_disable);

int xnintr_affinity(struct xnintr *intr, const cpumask_t *cpumask)
{
	cpumask_t effective_mask;

	secondary_mode_only();

	cpumask_and(&effective_mask, &xnsched_realtime_cpus, cpumask);
	if (cpumask_empty(&effective_mask))
		return -EINVAL;

	return irq_set_affinity_hint(intr->irq, &effective_mask);
}
EXPORT_SYMBOL_GPL(xnintr_affinity);

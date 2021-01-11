/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_DOVETAIL_SIRQ_H
#define _COBALT_KERNEL_DOVETAIL_SIRQ_H

#include <linux/irq_pipeline.h>
#include <cobalt/kernel/assert.h>

/*
 * Wrappers to create "synthetic IRQs" the Dovetail way. Those
 * interrupt channels can only be trigged by software, in order to run
 * a handler on the in-band execution stage.
 */

static inline
int pipeline_create_inband_sirq(irqreturn_t (*handler)(int irq, void *dev_id))
{
	/*
	 * Allocate an IRQ from the synthetic interrupt domain then
	 * trap it to @handler, to be fired from the in-band stage.
	 */
	int sirq, ret;

	sirq = irq_create_direct_mapping(synthetic_irq_domain);
	if (sirq == 0)
		return -EAGAIN;

	ret = __request_percpu_irq(sirq,
			handler,
			IRQF_NO_THREAD,
			"Inband sirq",
			&cobalt_machine_cpudata);

	if (ret) {
		irq_dispose_mapping(sirq);
		return ret;
	}

	return sirq;
}

static inline
void pipeline_delete_inband_sirq(int sirq)
{
	/*
	 * Free the synthetic IRQ then deallocate it to its
	 * originating domain.
	 */
	free_percpu_irq(sirq,
		&cobalt_machine_cpudata);

	irq_dispose_mapping(sirq);
}

static inline void pipeline_post_sirq(int sirq)
{
	/* Trigger the synthetic IRQ */
	irq_post_inband(sirq);
}

#endif /* !_COBALT_KERNEL_DOVETAIL_SIRQ_H */

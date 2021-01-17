/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_IPIPE_SIRQ_H
#define _COBALT_KERNEL_IPIPE_SIRQ_H

#include <linux/ipipe.h>
#include <pipeline/machine.h>

/*
 * Wrappers to create "synthetic IRQs" the I-pipe way (used to be
 * called "virtual IRQs" there). Those interrupt channels can only be
 * triggered by software; they have per-CPU semantics. We use them to
 * schedule handlers to be run on the in-band execution stage, meaning
 * "secondary mode" in the Cobalt jargon.
 */

static inline
int pipeline_create_inband_sirq(irqreturn_t (*handler)(int irq, void *dev_id))
{
	int sirq, ret;

	sirq = ipipe_alloc_virq();
	if (sirq == 0)
		return -EAGAIN;

	/*
	 * ipipe_irq_handler_t is close enough to the signature of a
	 * regular IRQ handler: use the latter in the generic code
	 * shared with Dovetail.  The extraneous return code will be
	 * ignored by the I-pipe core.
	 */
	ret = ipipe_request_irq(ipipe_root_domain, sirq,
				(ipipe_irq_handler_t)handler,
				NULL, NULL);
	if (ret) {
		ipipe_free_virq(sirq);
		return ret;
	}

	return sirq;
}

static inline
void pipeline_delete_inband_sirq(int sirq)
{
	ipipe_free_irq(ipipe_root_domain, sirq);
	ipipe_free_virq(sirq);
}

static inline void pipeline_post_sirq(int sirq)
{
	ipipe_post_irq_root(sirq);
}

#endif /* !_COBALT_KERNEL_IPIPE_SIRQ_H */

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
	TODO();

	return 0;
}

static inline
void pipeline_delete_inband_sirq(int sirq)
{
	/*
	 * Free the synthetic IRQ then deallocate it to its
	 * originating domain.
	 */
	TODO();
}

static inline void pipeline_post_sirq(int sirq)
{
	/* Trigger the synthetic IRQ */
	TODO();
}

#endif /* !_COBALT_KERNEL_IPIPE_SIRQ_H */

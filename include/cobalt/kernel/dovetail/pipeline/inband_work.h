/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_DOVETAIL_INBAND_WORK_H
#define _COBALT_KERNEL_DOVETAIL_INBAND_WORK_H

#include <linux/irq_work.h>

/*
 * This field must be named inband_work and appear first in the
 * container work struct.
 */
struct pipeline_inband_work {
	struct irq_work work;
};

#define PIPELINE_INBAND_WORK_INITIALIZER(__work, __handler)		\
	{								\
		.work = IRQ_WORK_INIT((void (*)(struct irq_work *))__handler), \
	}

#define pipeline_post_inband_work(__work)				\
			irq_work_queue(&(__work)->inband_work.work)

#endif /* !_COBALT_KERNEL_DOVETAIL_INBAND_WORK_H */

/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_IPIPE_INBAND_WORK_H
#define _COBALT_KERNEL_IPIPE_INBAND_WORK_H

#include <linux/ipipe.h>

/*
 * This field must be named inband_work and appear first in the
 * container work struct.
 */
struct pipeline_inband_work {
	struct ipipe_work_header work;
};

#define PIPELINE_INBAND_WORK_INITIALIZER(__work, __handler)		\
	{								\
		.work = {						\
			.size = sizeof(__work),				\
			.handler = (void (*)(struct ipipe_work_header *)) \
			__handler,					\
		},							\
	}

#define pipeline_post_inband_work(__work)	\
	ipipe_post_work_root(__work, inband_work.work)

#endif /* !_COBALT_KERNEL_IPIPE_INBAND_WORK_H */

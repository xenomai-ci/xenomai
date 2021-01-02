/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_IPIPE_PIPELINE_H
#define _COBALT_KERNEL_IPIPE_PIPELINE_H

#include <linux/ipipe.h>

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

#endif /* !_COBALT_KERNEL_IPIPE_PIPELINE_H */

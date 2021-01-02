/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_IPIPE_LOCK_H
#define _COBALT_KERNEL_IPIPE_LOCK_H

#include <pipeline/pipeline.h>

typedef ipipe_spinlock_t pipeline_spinlock_t;

#define PIPELINE_SPIN_LOCK_UNLOCKED(__name)  IPIPE_SPIN_LOCK_UNLOCKED

#ifdef CONFIG_XENO_OPT_DEBUG_LOCKING
/* Disable UP-over-SMP kernel optimization in debug mode. */
#define __locking_active__  1
#else
#define __locking_active__  ipipe_smp_p
#endif

#endif /* !_COBALT_KERNEL_IPIPE_LOCK_H */

/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_DOVETAIL_LOCK_H
#define _COBALT_KERNEL_DOVETAIL_LOCK_H

#include <linux/spinlock.h>

typedef hard_spinlock_t pipeline_spinlock_t;

#define PIPELINE_SPIN_LOCK_UNLOCKED(__name)  __HARD_SPIN_LOCK_INITIALIZER(__name)

#ifdef CONFIG_XENO_OPT_DEBUG_LOCKING
/* Disable UP-over-SMP kernel optimization in debug mode. */
#define __locking_active__  1

#else

#ifdef CONFIG_SMP
#define __locking_active__  1
#else
#define __locking_active__  IS_ENABLED(CONFIG_SMP)
#endif

#endif

#endif /* !_COBALT_KERNEL_DOVETAIL_LOCK_H */

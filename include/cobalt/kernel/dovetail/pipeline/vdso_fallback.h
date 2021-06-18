/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 * Copyright (c) Siemens AG, 2021
 */

#ifndef _COBALT_KERNEL_PIPELINE_VDSO_FALLBACK_H
#define _COBALT_KERNEL_PIPELINE_VDSO_FALLBACK_H

#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/timer.h>
#include <xenomai/posix/clock.h>

#define is_clock_gettime(__nr)		((__nr) == __NR_clock_gettime)

#ifndef __NR_clock_gettime64
#define is_clock_gettime64(__nr)	0
#else
#define is_clock_gettime64(__nr)	((__nr) == __NR_clock_gettime64)
#endif

static __always_inline bool 
pipeline_handle_vdso_fallback(int nr, struct pt_regs *regs)
{
	struct __kernel_old_timespec __user *u_old_ts;
	struct __kernel_timespec uts, __user *u_uts;
	struct __kernel_old_timespec old_ts;
	struct timespec64 ts64;
	int clock_id, ret = 0;
	unsigned long args[6];

	if (!is_clock_gettime(nr) && !is_clock_gettime64(nr))
		return false;

	/*
	 * We need to fetch the args again because not all archs use the same
	 * calling convention for Linux and Xenomai syscalls.
	 */
	syscall_get_arguments(current, regs, args);

	clock_id = (int)args[0];
	switch (clock_id) {
	case CLOCK_MONOTONIC:
		ns2ts(&ts64, xnclock_read_monotonic(&nkclock));
		break;
	case CLOCK_REALTIME:
		ns2ts(&ts64, xnclock_read_realtime(&nkclock));
		break;
	default:
		return false;
	}

	if (is_clock_gettime(nr)) {
		old_ts.tv_sec = (__kernel_old_time_t)ts64.tv_sec;
		old_ts.tv_nsec = ts64.tv_nsec;
		u_old_ts = (struct __kernel_old_timespec __user *)args[1];
		if (raw_copy_to_user(u_old_ts, &old_ts, sizeof(old_ts)))
			ret = -EFAULT;
	} else if (is_clock_gettime64(nr)) {
		uts.tv_sec = ts64.tv_sec;
		uts.tv_nsec = ts64.tv_nsec;
		u_uts = (struct __kernel_timespec __user *)args[1];
		if (raw_copy_to_user(u_uts, &uts, sizeof(uts)))
			ret = -EFAULT;
	}

	__xn_status_return(regs, ret);

	return true;
}

#endif /* !_COBALT_KERNEL_PIPELINE_VDSO_FALLBACK_H */

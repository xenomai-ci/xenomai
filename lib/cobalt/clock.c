/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>		/* For pthread_setcanceltype. */
#include <time.h>
#include <cobalt/uapi/time.h>
#include <cobalt/ticks.h>
#include <asm/xenomai/syscall.h>
#include <asm/xenomai/tsc.h>
#include "sem_heap.h"
#include "internal.h"

COBALT_IMPL(int, clock_getres, (clockid_t clock_id, struct timespec *tp))
{
	int err = -XENOMAI_SKINCALL2(__cobalt_muxid,
				     sc_cobalt_clock_getres,
				     clock_id,
				     tp);
	if (!err)
		return 0;

	errno = err;
	return -1;
}

static int __do_clock_host_realtime(struct timespec *ts)
{
	unsigned long long now, base, mask, cycle_delta;
	struct xnvdso_hostrt_data *hostrt_data;
	unsigned long mult, shift, nsec, rem;
	urwstate_t tmp;

	if (!xnvdso_test_feature(vdso, XNVDSO_FEAT_HOST_REALTIME))
		return -1;

	hostrt_data = &vdso->hostrt_data;

	if (!hostrt_data->live)
		return -1;

	/*
	 * The following is essentially a verbatim copy of the
	 * mechanism in the kernel.
	 */
	unsynced_read_block(&tmp, &hostrt_data->lock) {
		now = __xn_rdtsc();
		base = hostrt_data->cycle_last;
		mask = hostrt_data->mask;
		mult = hostrt_data->mult;
		shift = hostrt_data->shift;
		ts->tv_sec = hostrt_data->wall_time_sec;
		nsec = hostrt_data->wall_time_nsec;
	}

	cycle_delta = (now - base) & mask;
	nsec += (cycle_delta * mult) >> shift;

	ts->tv_sec += cobalt_divrem_billion(nsec, &rem);
	ts->tv_nsec = rem;

	return 0;
}

COBALT_IMPL(int, clock_gettime, (clockid_t clock_id, struct timespec *tp))
{
	unsigned long long ns;
	unsigned long rem;
	int ret;

	switch (clock_id) {
	case CLOCK_HOST_REALTIME:
		ret = __do_clock_host_realtime(tp);
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		ns = cobalt_ticks_to_ns(__xn_rdtsc());
		tp->tv_sec = cobalt_divrem_billion(ns, &rem);
		tp->tv_nsec = rem;
		return 0;
	case CLOCK_REALTIME:
		ns = cobalt_ticks_to_ns(__xn_rdtsc());
		ns += vdso->wallclock_offset;
		tp->tv_sec = cobalt_divrem_billion(ns, &rem);
		tp->tv_nsec = rem;
		return 0;
	default:
		ret = -XENOMAI_SKINCALL2(__cobalt_muxid,
					 sc_cobalt_clock_gettime,
					 clock_id,
					 tp);
	}

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

COBALT_IMPL(int, clock_settime, (clockid_t clock_id, const struct timespec *tp))
{
	int err = -XENOMAI_SKINCALL2(__cobalt_muxid,
				     sc_cobalt_clock_settime,
				     clock_id,
				     tp);

	if (!err)
		return 0;

	errno = err;
	return -1;
}

COBALT_IMPL(int, clock_nanosleep, (clockid_t clock_id,
				   int flags,
				   const struct timespec *rqtp, struct timespec *rmtp))
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = -XENOMAI_SKINCALL4(__cobalt_muxid,
				 sc_cobalt_clock_nanosleep,
				 clock_id, flags, rqtp, rmtp);

	pthread_setcanceltype(oldtype, NULL);

	return err;
}

COBALT_IMPL(int, nanosleep, (const struct timespec *rqtp, struct timespec *rmtp))
{
	int err = __wrap_clock_nanosleep(CLOCK_REALTIME, 0, rqtp, rmtp);

	if (!err)
		return 0;

	errno = err;
	return -1;
}
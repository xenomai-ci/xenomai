/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 */

#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/vdso.h>

int pipeline_get_host_time(struct timespec *tp)
{
#ifdef CONFIG_IPIPE_HAVE_HOSTRT
	struct xnvdso_hostrt_data *hostrt_data;
	u64 now, base, mask, cycle_delta;
	__u32 mult, shift;
	unsigned long rem;
	urwstate_t tmp;
	__u64 nsec;

	hostrt_data = get_hostrt_data();
	BUG_ON(!hostrt_data);

	if (unlikely(!hostrt_data->live))
		return -1;

	/*
	 * Note: Disabling HW interrupts around writes to hostrt_data
	 * ensures that a reader (on the Xenomai side) cannot
	 * interrupt a writer (on the Linux kernel side) on the same
	 * CPU.  The urw block is required when a reader is
	 * interleaved by a writer on a different CPU. This follows
	 * the approach from userland, where taking the spinlock is
	 * not possible.
	 */
	unsynced_read_block(&tmp, &hostrt_data->lock) {
		now = xnclock_read_raw(&nkclock);
		base = hostrt_data->cycle_last;
		mask = hostrt_data->mask;
		mult = hostrt_data->mult;
		shift = hostrt_data->shift;
		tp->tv_sec = hostrt_data->wall_sec;
		nsec = hostrt_data->wall_nsec;
	}

	/*
	 * At this point, we have a consistent copy of the fundamental
	 * data structure - calculate the interval between the current
	 * and base time stamp cycles, and convert the difference
	 * to nanoseconds.
	 */
	cycle_delta = (now - base) & mask;
	nsec += (cycle_delta * mult) >> shift;

	/* Convert to the desired sec, usec representation */
	tp->tv_sec += xnclock_divrem_billion(nsec, &rem);
	tp->tv_nsec = rem;

	return 0;
#else
	return -EINVAL;
#endif
}

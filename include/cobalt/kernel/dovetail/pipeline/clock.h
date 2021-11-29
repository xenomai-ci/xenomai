/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_DOVETAIL_CLOCK_H
#define _COBALT_KERNEL_DOVETAIL_CLOCK_H

#include <cobalt/uapi/kernel/types.h>
#include <cobalt/kernel/assert.h>
#include <linux/ktime.h>
#include <linux/errno.h>

struct timespec64;

static inline u64 pipeline_read_cycle_counter(void)
{
	/*
	 * With Dovetail, our idea of time is directly based on a
	 * refined count of nanoseconds since the epoch, the hardware
	 * time counter is transparent to us. For this reason,
	 * xnclock_ticks_to_ns() and xnclock_ns_to_ticks() are
	 * idempotent when building for Dovetail.
	 */
	return ktime_get_mono_fast_ns();
}

static inline xnticks_t pipeline_read_wallclock(void)
{
	return ktime_get_real_fast_ns();
}

static inline int pipeline_set_wallclock(xnticks_t epoch_ns)
{
	return -EOPNOTSUPP;
}

void pipeline_set_timer_shot(unsigned long cycles);

const char *pipeline_timer_name(void);

static inline const char *pipeline_clock_name(void)
{
	return "<Linux clocksource>";
}

static inline int pipeline_get_host_time(struct timespec64 *tp)
{
	/* Convert ktime_get_real_fast_ns() to timespec. */
	*tp = ktime_to_timespec64(ktime_get_real_fast_ns());

	return 0;
}

static inline void pipeline_init_clock(void)
{
	/* N/A */
}

static inline xnsticks_t xnclock_core_ticks_to_ns(xnsticks_t ticks)
{
	return ticks;
}

static inline xnsticks_t xnclock_core_ticks_to_ns_rounded(xnsticks_t ticks)
{
	return ticks;
}

static inline xnsticks_t xnclock_core_ns_to_ticks(xnsticks_t ns)
{
	return ns;
}

#endif /* !_COBALT_KERNEL_DOVETAIL_CLOCK_H */

/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_IPIPE_CLOCK_H
#define _COBALT_KERNEL_IPIPE_CLOCK_H

#include <linux/ipipe_tickdev.h>

struct timespec;

static inline u64 pipeline_read_cycle_counter(void)
{
	u64 t;
	ipipe_read_tsc(t);
	return t;
}

static inline void pipeline_set_timer_shot(unsigned long cycles)
{
	ipipe_timer_set(cycles);
}

static inline const char *pipeline_timer_name(void)
{
	return ipipe_timer_name();
}

static inline const char *pipeline_clock_name(void)
{
	return ipipe_clock_name();
}

int pipeline_get_host_time(struct timespec *tp);

#endif /* !_COBALT_KERNEL_IPIPE_CLOCK_H */

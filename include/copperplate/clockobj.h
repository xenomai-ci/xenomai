/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _COPPERPLATE_CLOCKOBJ_H
#define _COPPERPLATE_CLOCKOBJ_H

#include <pthread.h>
#include <xeno_config.h>
#include <boilerplate/time.h>
#include <boilerplate/list.h>
#include <boilerplate/lock.h>
#include <boilerplate/limits.h>

/*
 * The Copperplate clock shall be monotonic unless the threading
 * library has restrictions to support this over Mercury.
 *
 * In the normal case, this means that ongoing delays and timeouts
 * won't be affected when the host system date is changed. In the
 * restricted case by contrast, ongoing delays and timeouts may be
 * impacted by changes to the host system date.
 *
 * The implementation maintains a per-clock epoch value, so that
 * different emulators can have different (virtual) system dates.
 */
#ifdef CONFIG_XENO_COPPERPLATE_CLOCK_RESTRICTED
#define CLOCK_COPPERPLATE  CLOCK_REALTIME
#else
#define CLOCK_COPPERPLATE  CLOCK_MONOTONIC
#endif

struct clockobj {
	pthread_mutex_t lock;
	struct timespec epoch;
	struct timespec offset;
#ifndef CONFIG_XENO_LORES_CLOCK_DISABLED
	unsigned int resolution;
	unsigned int frequency;
#endif
};

#define zero_time	((struct timespec){ .tv_sec = 0, .tv_nsec = 0 })

#ifdef __cplusplus
extern "C" {
#endif

void clockobj_set_date(struct clockobj *clkobj, ticks_t ticks);

void clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks);

ticks_t clockobj_get_time(struct clockobj *clkobj);

void clockobj_get_distance(struct clockobj *clkobj,
			   const struct itimerspec *itm,
			   struct timespec *delta);

void clockobj_caltime_to_timeout(struct clockobj *clkobj, const struct tm *tm,
				 unsigned long rticks, struct timespec *ts);

void clockobj_caltime_to_ticks(struct clockobj *clkobj, const struct tm *tm,
			       unsigned long rticks, ticks_t *pticks);

void clockobj_ticks_to_caltime(struct clockobj *clkobj,
			       ticks_t ticks,
			       struct tm *tm,
			       unsigned long *rticks);

void clockobj_convert_clocks(struct clockobj *clkobj,
			     const struct timespec *in,
			     clockid_t clk_id,
			     struct timespec *out);

int clockobj_set_resolution(struct clockobj *clkobj,
			    unsigned int resolution_ns);

int clockobj_init(struct clockobj *clkobj,
		  unsigned int resolution_ns);

int clockobj_destroy(struct clockobj *clkobj);

#ifndef CONFIG_XENO_LORES_CLOCK_DISABLED

void __clockobj_ticks_to_timeout(struct clockobj *clkobj, clockid_t clk_id,
				 ticks_t ticks, struct timespec *ts);

void __clockobj_ticks_to_timespec(struct clockobj *clkobj,
				  ticks_t ticks, struct timespec *ts);
#endif /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

#ifdef __cplusplus
}
#endif

#ifdef CONFIG_XENO_COBALT

#include <cobalt/ticks.h>
#include <cobalt/sys/cobalt.h>

/*
 * The Cobalt core exclusively deals with aperiodic timings, so a
 * Cobalt _tick_ is actually a nanosecond unit. In contrast, Copperplate
 * deals with both nanosecond units and periodic _ticks_ which duration
 * depend on the clock resolution. Copperplate ticks are periods of the
 * reference clockobj which Cobalt does not know about.
 */

static inline ticks_t clockobj_get_ns(void)
{
	/* Guaranteed to be the source of CLOCK_COPPERPLATE. */
	return cobalt_read_ns();
}

static inline
void clockobj_ns_to_timespec(ticks_t ns, struct timespec *ts)
{
	unsigned long rem;

	ts->tv_sec = (time_t)cobalt_divrem_billion(ns, &rem);
	ts->tv_nsec = (long)rem;
}

#else /* CONFIG_XENO_MERCURY */

ticks_t clockobj_get_ns(void);

static inline
void clockobj_ns_to_timespec(ticks_t ns, struct timespec *ts)
{
	ts->tv_sec = ns / 1000000000ULL;
	ts->tv_nsec = ns - (ts->tv_sec * 1000000000ULL);
}

#endif /* CONFIG_XENO_MERCURY */

#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED

static inline
void __clockobj_ticks_to_timeout(struct clockobj *clkobj,
				 clockid_t clk_id,
				 ticks_t ticks, struct timespec *ts)
{
	struct timespec now, delta;

	__RT(clock_gettime(clk_id, &now));
	clockobj_ns_to_timespec(ticks, &delta);
	timespec_add(ts, &now, &delta);
}

static inline
void __clockobj_ticks_to_timespec(struct clockobj *clkobj,
				  ticks_t ticks, struct timespec *ts)
{
	clockobj_ns_to_timespec(ticks, ts);
}

static inline
void clockobj_ticks_to_timespec(struct clockobj *clkobj,
				ticks_t ticks, struct timespec *ts)
{
	__clockobj_ticks_to_timespec(clkobj, ticks, ts);
}

static inline
unsigned int clockobj_get_resolution(struct clockobj *clkobj)
{
	return 1;
}

static inline
unsigned int clockobj_get_frequency(struct clockobj *clkobj)
{
	return 1000000000;
}

static inline sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj,
					    sticks_t ns)
{
	return ns;
}

static inline sticks_t clockobj_ticks_to_ns(struct clockobj *clkobj,
					    sticks_t ticks)
{
	return ticks;
}

#else /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

static inline
void clockobj_ticks_to_timespec(struct clockobj *clkobj,
				ticks_t ticks, struct timespec *ts)
{
	__clockobj_ticks_to_timespec(clkobj, ticks, ts);
}

static inline
unsigned int clockobj_get_resolution(struct clockobj *clkobj)
{
	return clkobj->resolution;
}

static inline
unsigned int clockobj_get_frequency(struct clockobj *clkobj)
{
	return clkobj->frequency;
}

sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj,
			      sticks_t ns);

static inline sticks_t clockobj_ticks_to_ns(struct clockobj *clkobj,
					    sticks_t ticks)
{
	return ticks * clkobj->resolution;
}

#endif /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

static inline
void clockobj_ticks_to_timeout(struct clockobj *clkobj,
			       ticks_t ticks, struct timespec *ts)
{
	__clockobj_ticks_to_timeout(clkobj, CLOCK_COPPERPLATE, ticks, ts);
}

#endif /* _COPPERPLATE_CLOCKOBJ_H */

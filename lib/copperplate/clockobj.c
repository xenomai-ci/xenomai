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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <string.h>
#include "boilerplate/lock.h"
#include "boilerplate/time.h"
#include "copperplate/clockobj.h"
#include "copperplate/debug.h"
#include "internal.h"

#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED

static inline
int __clockobj_set_resolution(struct clockobj *clkobj,
			      unsigned int resolution_ns)
{
	if (resolution_ns > 1) {
		warning("low resolution clock disabled [--enable-lores-clock]");
		return __bt(-EINVAL);
	}

	return 0;
}

#else  /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

void __clockobj_ticks_to_timespec(struct clockobj *clkobj,
				  ticks_t ticks,
				  struct timespec *ts)
{
	unsigned int freq;

	if (clockobj_get_resolution(clkobj) > 1) {
		freq = clockobj_get_frequency(clkobj);
		ts->tv_sec = ticks / freq;
		ts->tv_nsec = ticks - (ts->tv_sec * freq);
		ts->tv_nsec *= clockobj_get_resolution(clkobj);
	} else
		clockobj_ns_to_timespec(ticks, ts);
}

void __clockobj_ticks_to_timeout(struct clockobj *clkobj,
				 clockid_t clk_id,
				 ticks_t ticks, struct timespec *ts)
{
	struct timespec now, delta;

	__RT(clock_gettime(clk_id, &now));
	__clockobj_ticks_to_timespec(clkobj, ticks, &delta);
	timespec_add(ts, &now, &delta);
}

static inline
int __clockobj_set_resolution(struct clockobj *clkobj,
			      unsigned int resolution_ns)
{
	clkobj->resolution = resolution_ns;
	clkobj->frequency = 1000000000 / resolution_ns;

	return 0;
}

#endif /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

static const int mdays[] = {
	31, 28, 31, 30, 31, 30,	31, 31, 30, 31, 30, 31
};

void clockobj_caltime_to_ticks(struct clockobj *clkobj, const struct tm *tm,
			       unsigned long rticks, ticks_t *pticks)
{
	ticks_t t = 0;
	int n;	/* Must be signed. */

	/*
	 * We expect tick counts to be based on the time(2) epoch,
	 * i.e. 00:00:00 UTC, January 1, 1970.
	 */
	for (n = 1970; n <  1900 + tm->tm_year; n++)
		t += ((n % 4) ? 365 : 366);

	if (!(tm->tm_year % 4) && tm->tm_mon >= 2)
		/* Add one day for leap year after February. */
		t++;

	for (n = tm->tm_mon - 1; n >= 0; n--)
		t += mdays[n];

	t += tm->tm_mday - 1;
	t *= 24;
	t += tm->tm_hour;
	t *= 60;
	t += tm->tm_min;
	t *= 60;
	t += tm->tm_sec;
	t *= clockobj_get_frequency(clkobj);
	t += rticks;

	/* XXX: we currently don't care about DST. */

	*pticks = t;
}

#define SECBYMIN	60
#define SECBYHOUR	(SECBYMIN * 60)
#define SECBYDAY	(SECBYHOUR * 24)

void clockobj_ticks_to_caltime(struct clockobj *clkobj,
			       ticks_t ticks,
			       struct tm *tm,
			       unsigned long *rticks)
{
	unsigned long year, month, day, hour, min, sec;
	unsigned int freq;
	time_t nsecs;

	freq = clockobj_get_frequency(clkobj);
	nsecs = ticks / freq;
	*rticks = ticks % freq;

	for (year = 1970;; year++) { /* Years since 1970. */
		int ysecs = ((year % 4) ? 365 : 366) * SECBYDAY;
		if (ysecs > nsecs)
			break;
		nsecs -= ysecs;
	}

	for (month = 0;; month++) {
		int secbymon = mdays[month] * SECBYDAY;
		if (month == 1 && (year % 4) == 0)
			/* Account for leap year on February. */
			secbymon += SECBYDAY;
		if (secbymon > nsecs)
			break;
		nsecs -= secbymon;
	}

	day = nsecs / SECBYDAY;
	nsecs -= (day * SECBYDAY);
	hour = (nsecs / SECBYHOUR);
	nsecs -= (hour * SECBYHOUR);
	min = (nsecs / SECBYMIN);
	nsecs -= (min * SECBYMIN);
	sec = nsecs;

	memset(tm, 0, sizeof(*tm));
	tm->tm_year = year - 1900;
	tm->tm_mon = month;
	tm->tm_mday = day + 1;
	tm->tm_hour = hour;
	tm->tm_min = min;
	tm->tm_sec = sec;
}

void clockobj_caltime_to_timeout(struct clockobj *clkobj, const struct tm *tm,
				 unsigned long rticks, struct timespec *ts)
{
	struct timespec date;
	ticks_t ticks;

	clockobj_caltime_to_ticks(clkobj, tm, rticks, &ticks);
	__clockobj_ticks_to_timespec(clkobj, ticks, &date);
	timespec_sub(ts, &date, &clkobj->offset);
}

void clockobj_set_date(struct clockobj *clkobj, ticks_t ticks)
{
	struct timespec now;

	/*
	 * XXX: we grab the lock to exclude other threads from reading
	 * the clock offset while we update it, so that they either
	 * compute against the old value, or the new one, but always
	 * see a valid offset.
	 */
	read_lock_nocancel(&clkobj->lock);

	__RT(clock_gettime(CLOCK_COPPERPLATE, &now));

	__clockobj_ticks_to_timespec(clkobj, ticks, &clkobj->epoch);
	timespec_sub(&clkobj->offset, &clkobj->epoch, &now);

	read_unlock(&clkobj->lock);
}

/*
 * CAUTION: clockobj_set_resolution() may be called during the init
 * phase only, not after. IOW, the resolution is perceived as a
 * constant when the application code executes. For performance
 * reason, we want to run locklessly for common time unit conversions,
 * so the clockobj implementation does assume that the clock
 * resolution will NOT be updated after the init phase.
 */
int clockobj_set_resolution(struct clockobj *clkobj, unsigned int resolution_ns)
{
#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED
	assert(resolution_ns == 1);
#else
	__clockobj_set_resolution(clkobj, resolution_ns);

	/* Changing the resolution implies resetting the epoch. */
	clockobj_set_date(clkobj, 0);
#endif
	return 0;
}

#ifdef CONFIG_XENO_COBALT

#include <cobalt/arith.h>
#include <cobalt/sys/cobalt.h>

#ifdef CONFIG_XENO_COPPERPLATE_CLOCK_RESTRICTED
#error "restricted CLOCK_COPPERPLATE not available"
#endif

ticks_t clockobj_get_time(struct clockobj *clkobj)
{
	ticks_t ns = cobalt_read_ns();
	return clockobj_ns_to_ticks(clkobj, ns);
}

#ifndef CONFIG_XENO_LORES_CLOCK_DISABLED

sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj, sticks_t ns)
{
	/* Cobalt has optimized arith ops, use them. */
	return xnarch_ulldiv(ns, clkobj->resolution, NULL);
}

#endif /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

void clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks)
{
	ticks_t ns;

	read_lock_nocancel(&clkobj->lock);

	ns = clockobj_get_tsc();
	/* Add offset to epoch. */
	ns += (ticks_t)clkobj->offset.tv_sec * 1000000000ULL;
	ns += clkobj->offset.tv_nsec;
	if (clockobj_get_resolution(clkobj) > 1)
		ns /= clockobj_get_resolution(clkobj);
	*pticks = ns;

	read_unlock(&clkobj->lock);
}

#else /* CONFIG_XENO_MERCURY */

ticks_t clockobj_get_tsc(void)
{
	struct timespec now;
	clock_gettime(CLOCK_COPPERPLATE, &now);
	return (ticks_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

ticks_t clockobj_get_time(struct clockobj *clkobj)
{
	ticks_t ns = clockobj_get_tsc();

	if (clockobj_get_resolution(clkobj) > 1)
		return ns / clockobj_get_resolution(clkobj);

	return ns;
}

#ifndef CONFIG_XENO_LORES_CLOCK_DISABLED

sticks_t clockobj_ns_to_ticks(struct clockobj *clkobj, sticks_t ns)
{
	return ns / clkobj->resolution;
}

#endif /* !CONFIG_XENO_LORES_CLOCK_DISABLED */

void clockobj_get_date(struct clockobj *clkobj, ticks_t *pticks)
{
	struct timespec now, date;

	read_lock_nocancel(&clkobj->lock);

	clock_gettime(CLOCK_COPPERPLATE, &now);

	/* Add offset from epoch to current system time. */
	timespec_add(&date, &clkobj->offset, &now);

	/* Convert the time value to ticks,. */
	*pticks = (ticks_t)date.tv_sec * clockobj_get_frequency(clkobj)
		+ (ticks_t)date.tv_nsec / clockobj_get_resolution(clkobj);

	read_unlock(&clkobj->lock);
}

#endif /* CONFIG_XENO_MERCURY */

/* Conversion from CLOCK_COPPERPLATE to clk_id. */
void clockobj_convert_clocks(struct clockobj *clkobj,
			     const struct timespec *in,
			     clockid_t clk_id,
			     struct timespec *out)
{
	struct timespec now, delta;

	__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
	/* Offset from CLOCK_COPPERPLATE epoch. */
	timespec_sub(&delta, in, &now);
	/* Current time for clk_id. */
	__RT(clock_gettime(clk_id, &now));
	/* Absolute timeout again, clk_id-based this time. */
	timespec_add(out, &delta, &now);
}

void clockobj_get_distance(struct clockobj *clkobj,
			   const struct itimerspec *itm,
			   struct timespec *delta)
{
	ticks_t now, start, interval, dist;

	now = clockobj_get_time(clkobj);
	start = timespec_scalar(&itm->it_value);

	if (start >= now)
		/* Distance to first shot. */
		dist = start - now;
	else {
		interval = timespec_scalar(&itm->it_interval);
		/* Distance to next shot. */
		dist = interval - ((now - start) % interval);
	}

	__clockobj_ticks_to_timespec(clkobj, dist, delta);
}

int clockobj_init(struct clockobj *clkobj,
		  unsigned int resolution_ns)
{
	pthread_mutexattr_t mattr;
	struct timespec now;
	int ret;

	if (resolution_ns == 0)
		return __bt(-EINVAL);

	memset(clkobj, 0, sizeof(*clkobj));
	ret = __clockobj_set_resolution(clkobj, resolution_ns);
	if (ret)
		return __bt(ret);

	/*
	 * FIXME: this lock is only used to protect the wallclock
	 * offset readings from updates. We should replace this by a
	 * confirmed reading loop.
	 */
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&clkobj->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
	timespec_sub(&clkobj->offset, &clkobj->epoch, &now);

	return 0;
}

int clockobj_destroy(struct clockobj *clkobj)
{
	__RT(pthread_mutex_destroy(&clkobj->lock));
	return 0;
}

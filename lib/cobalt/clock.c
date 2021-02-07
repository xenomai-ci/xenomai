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
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <cobalt/uapi/time.h>
#include <cobalt/ticks.h>
#include <asm/xenomai/syscall.h>
#include <asm/xenomai/tsc.h>
#include "umm.h"
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_time Clocks and timers
 *
 * Cobalt/POSIX clock and timer services
 *
 * Cobalt supports three built-in clocks:
 *
 * CLOCK_REALTIME maps to the nucleus system clock, keeping time as the amount
 * of time since the Epoch, with a resolution of one nanosecond.
 *
 * CLOCK_MONOTONIC maps to an architecture-dependent high resolution
 * counter, so is suitable for measuring short time
 * intervals. However, when used for sleeping (with
 * clock_nanosleep()), the CLOCK_MONOTONIC clock has a resolution of
 * one nanosecond, like the CLOCK_REALTIME clock.
 *
 * CLOCK_MONOTONIC_RAW is Linux-specific, and provides monotonic time
 * values from a hardware timer which is not adjusted by NTP. This is
 * strictly equivalent to CLOCK_MONOTONIC with Cobalt, which is not
 * NTP adjusted either.
 *
 * In addition, external clocks can be dynamically registered using
 * the cobalt_clock_register() service. These clocks are fully managed
 * by Cobalt extension code, which should advertise each incoming tick
 * by calling xnclock_tick() for the relevant clock, from an interrupt
 * context.
 *
 * Timer objects may be created with the timer_create() service using
 * any of the built-in or external clocks. The resolution of these
 * timers is clock-specific. However, built-in clocks all have
 * nanosecond resolution, as specified for clock_nanosleep().
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_08.html#tag_02_08_05">
 * Specification.</a>
 *
 *@{
 */

/**
 * Get the resolution of the specified clock.
 *
 * This service returns, at the address @a res, if it is not @a NULL, the
 * resolution of the clock @a clock_id.
 *
 * For both CLOCK_REALTIME and CLOCK_MONOTONIC, this resolution is the duration
 * of one system clock tick. No other clock is supported.
 *
 * @param clock_id clock identifier, either CLOCK_REALTIME or CLOCK_MONOTONIC;
 *
 * @param tp the address where the resolution of the specified clock will be
 * stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a clock_id is invalid;
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/clock_getres.html">
 * Specification.</a>
 *
 * @apitags{unrestricted}
 */
COBALT_IMPL(int, clock_getres, (clockid_t clock_id, struct timespec *tp))
{
	int ret;

#ifdef __USE_TIME_BITS64
	ret = -XENOMAI_SYSCALL2(sc_cobalt_clock_getres64, clock_id, tp);
#else
	ret = -XENOMAI_SYSCALL2(sc_cobalt_clock_getres, clock_id, tp);
#endif

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

static int __do_clock_host_realtime(struct timespec *ts)
{
	uint64_t now, base, mask, cycle_delta, nsec;
	struct xnvdso_hostrt_data *hostrt_data;
	uint32_t mult, shift;
	unsigned long rem;
	urwstate_t tmp;

	if (!xnvdso_test_feature(cobalt_vdso, XNVDSO_FEAT_HOST_REALTIME))
		return -1;

	hostrt_data = &cobalt_vdso->hostrt_data;

	if (!hostrt_data->live)
		return -1;

	/*
	 * The following is essentially a verbatim copy of the
	 * mechanism in the kernel.
	 */
	unsynced_read_block(&tmp, &hostrt_data->lock) {
		now = cobalt_read_legacy_tsc();
		base = hostrt_data->cycle_last;
		mask = hostrt_data->mask;
		mult = hostrt_data->mult;
		shift = hostrt_data->shift;
		ts->tv_sec = hostrt_data->wall_sec;
		nsec = hostrt_data->wall_nsec;
	}

	cycle_delta = (now - base) & mask;
	nsec += (cycle_delta * mult) >> shift;

	ts->tv_sec += cobalt_divrem_billion(nsec, &rem);
	ts->tv_nsec = rem;

	return 0;
}

static int __do_clock_gettime(clockid_t clock_id, struct timespec *tp)
{
#ifdef __USE_TIME_BITS64
	return -XENOMAI_SYSCALL2(sc_cobalt_clock_gettime64, clock_id, tp);
#else
	return -XENOMAI_SYSCALL2(sc_cobalt_clock_gettime, clock_id, tp);
#endif
}

static int gettime_via_tsc(clockid_t clock_id, struct timespec *tp)
{
	unsigned long rem;
	xnticks_t ns;
	int ret;

	switch (clock_id) {
	case CLOCK_HOST_REALTIME:
		ret = __do_clock_host_realtime(tp);
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		ns = cobalt_ticks_to_ns(cobalt_read_legacy_tsc());
		tp->tv_sec = cobalt_divrem_billion(ns, &rem);
		tp->tv_nsec = rem;
		return 0;
	case CLOCK_REALTIME:
		ns = cobalt_ticks_to_ns(cobalt_read_legacy_tsc());
		ns += cobalt_vdso->wallclock_offset;
		tp->tv_sec = cobalt_divrem_billion(ns, &rem);
		tp->tv_nsec = rem;
		return 0;
	default:
		ret = __do_clock_gettime(clock_id, tp);
	}

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

static int gettime_via_vdso(clockid_t clock_id, struct timespec *tp)
{
	int ret;

	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_HOST_REALTIME:
		ret = __cobalt_vdso_gettime(CLOCK_REALTIME, tp);
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		ret = __cobalt_vdso_gettime(clock_id, tp);
		break;
	default:
		ret = __do_clock_gettime(clock_id, tp);
	}

	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

/**
 * Read the specified clock.
 *
 * This service returns, at the address @a tp the current value of the clock @a
 * clock_id. If @a clock_id is:
 * - CLOCK_REALTIME, the clock value represents the amount of time since the
 *   Epoch, with a precision of one system clock tick;
 * - CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW, the clock value is given
 *   by an architecture-dependent high resolution counter, with a
 *   precision independent from the system clock tick duration.
 * - CLOCK_HOST_REALTIME, the clock value as seen by the host, typically
 *   Linux. Resolution and precision depend on the host, but it is guaranteed
 *   that both, host and Cobalt, see the same information.
 *
 * @param clock_id clock identifier, either CLOCK_REALTIME, CLOCK_MONOTONIC,
 *        or CLOCK_HOST_REALTIME;
 *
 * @param tp the address where the value of the specified clock will be stored.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a clock_id is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/clock_gettime.html">
 * Specification.</a>
 *
 * @apitags{unrestricted}
 */
COBALT_IMPL(int, clock_gettime, (clockid_t clock_id, struct timespec *tp))
{
	if (cobalt_use_legacy_tsc())
		return gettime_via_tsc(clock_id, tp);

	return gettime_via_vdso(clock_id, tp);
}

/**
 * Set the specified clock.
 *
 * Set the CLOCK_REALTIME or Cobalt-specific clocks.
 *
 * @param clock_id the id of the clock to be set. CLOCK_REALTIME,
 * and Cobalt-specific clocks are supported.
 *
 * @param tp the address of a struct timespec specifying the new date.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a clock_id is undefined;
 * - EINVAL, the date specified by @a tp is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/clock_settime.html">
 * Specification.</a>
 *
 * @note Setting CLOCK_REALTIME may cause the caller to switch to
 * secondary mode.
 *
 * @apitags{unrestricted, switch-secondary}
 */
COBALT_IMPL(int, clock_settime, (clockid_t clock_id, const struct timespec *tp))
{
	int ret;

	if (clock_id == CLOCK_REALTIME && !cobalt_use_legacy_tsc())
		return __STD(clock_settime(CLOCK_REALTIME, tp));

#ifdef __USE_TIME_BITS64
	ret = -XENOMAI_SYSCALL2(sc_cobalt_clock_settime64, clock_id, tp);
#else
	ret = -XENOMAI_SYSCALL2(sc_cobalt_clock_settime, clock_id, tp);
#endif
	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

/* @apitags{unrestricted} */

COBALT_IMPL(int, clock_adjtime, (clockid_t clock_id, struct timex *tx))
{
	int ret;

#ifdef __USE_TIME_BITS64
	ret = -XENOMAI_SYSCALL2(sc_cobalt_clock_adjtime64, clock_id, tx);
#else
	ret = -XENOMAI_SYSCALL2(sc_cobalt_clock_adjtime, clock_id, tx);
#endif

	if (ret < 0) {
		errno = ret;
		return -1;
	}

	return ret;
}

/**
 * Sleep some amount of time.
 *
 * This service suspends the calling thread until the wakeup time specified by
 * @a rqtp, or a signal is delivered to the caller. If the flag TIMER_ABSTIME is
 * set in the @a flags argument, the wakeup time is specified as an absolute
 * value of the clock @a clock_id. If the flag TIMER_ABSTIME is not set, the
 * wakeup time is specified as a time interval.
 *
 * If this service is interrupted by a signal, the flag TIMER_ABSTIME is not
 * set, and @a rmtp is not @a NULL, the time remaining until the specified
 * wakeup time is returned at the address @a rmtp.
 *
 * The resolution of this service is one system clock tick.
 *
 * @param clock_id clock identifier, either CLOCK_REALTIME or CLOCK_MONOTONIC.
 *
 * @param flags one of:
 * - 0 meaning that the wakeup time @a rqtp is a time interval;
 * - TIMER_ABSTIME, meaning that the wakeup time is an absolute value of the
 *   clock @a clock_id.
 *
 * @param rqtp address of the wakeup time.
 *
 * @param rmtp address where the remaining time before wakeup will be stored if
 * the service is interrupted by a signal.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - ENOTSUP, the specified clock is unsupported;
 * - EINVAL, the specified wakeup time is invalid;
 * - EINTR, this service was interrupted by a signal.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/clock_nanosleep.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(int, clock_nanosleep, (clockid_t clock_id,
				   int flags,
				   const struct timespec *rqtp, struct timespec *rmtp))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

#ifdef __USE_TIME_BITS64
	ret = -XENOMAI_SYSCALL4(sc_cobalt_clock_nanosleep64,
				clock_id, flags, rqtp, rmtp);
#else
	ret = -XENOMAI_SYSCALL4(sc_cobalt_clock_nanosleep,
				clock_id, flags, rqtp, rmtp);
#endif

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

/**
 * Sleep some amount of time.
 *
 * This service suspends the calling thread until the wakeup time specified by
 * @a rqtp, or a signal is delivered. The wakeup time is specified as a time
 * interval.
 *
 * If this service is interrupted by a signal and @a rmtp is not @a NULL, the
 * time remaining until the specified wakeup time is returned at the address @a
 * rmtp.
 *
 * The resolution of this service is one system clock tick.
 *
 * @param rqtp address of the wakeup time.
 *
 * @param rmtp address where the remaining time before wakeup will be stored if
 * the service is interrupted by a signal.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the specified wakeup time is invalid;
 * - EINTR, this service was interrupted by a signal.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/nanosleep.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(int, nanosleep, (const struct timespec *rqtp, struct timespec *rmtp))
{
	int ret;

	ret = __WRAP(clock_nanosleep(CLOCK_REALTIME, 0, rqtp, rmtp));
	if (ret) {
		errno = ret;
		return -1;
	}

	return 0;
}

/* @apitags{thread-unrestricted, switch-primary} */

COBALT_IMPL(unsigned int, sleep, (unsigned int seconds))
{
	struct timespec rqt, rem;
	int ret;

	if (cobalt_get_current_fast() == XN_NO_HANDLE)
		return __STD(sleep(seconds));

	rqt.tv_sec = seconds;
	rqt.tv_nsec = 0;
	ret = __WRAP(clock_nanosleep(CLOCK_MONOTONIC, 0, &rqt, &rem));
	if (ret)
		return rem.tv_sec;

	return 0;
}

/* @apitags{thread-unrestricted, switch-primary} */

COBALT_IMPL(int, usleep, (useconds_t usec))
{
	struct timespec rqt;

	if (cobalt_get_current_fast() == XN_NO_HANDLE)
		return __STD(usleep(usec));

	rqt.tv_sec = usec / 1000000;
	rqt.tv_nsec = (usec % 1000000) * 1000;
	return __WRAP(clock_nanosleep(CLOCK_MONOTONIC, 0, &rqt, NULL));
}

/* @apitags{unrestricted} */

COBALT_IMPL(int, gettimeofday, (struct timeval *tv, struct timezone *tz))
{
	struct timespec ts;
	int ret = __WRAP(clock_gettime(CLOCK_REALTIME, &ts));
	if (ret == 0) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}
	return ret;
}

/* @apitags{unrestricted} */

COBALT_IMPL(time_t, time, (time_t *t))
{
	struct timespec ts;
	int ret = __WRAP(clock_gettime(CLOCK_REALTIME, &ts));
	if (ret)
		return (time_t)-1;

	if (t)
		*t = ts.tv_sec;
	return ts.tv_sec;
}
/** @} */

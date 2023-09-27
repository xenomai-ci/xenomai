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

#include <errno.h>
#include <time.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

/**
 * @addtogroup cobalt_api_time
 * @{
 */

/**
 * @fn int timer_create(clockid_t clockid, const struct sigevent *__restrict__ evp, timer_t * __restrict__ timerid)
 * @brief Create a timer
 *
 * This service creates a timer based on the clock @a clockid.
 *
 * If @a evp is not @a NULL, it describes the notification mechanism
 * used on timer expiration. Only thread-directed notification is
 * supported (evp->sigev_notify set to @a SIGEV_THREAD_ID).
 *
 * If @a evp is NULL, the current Cobalt thread will receive the
 * notifications with signal SIGALRM.
 *
 * The recipient thread is delivered notifications when it calls any
 * of the sigwait(), sigtimedwait() or sigwaitinfo() services.
 *
 * If this service succeeds, an identifier for the created timer is
 * returned at the address @a timerid. The timer is unarmed until
 * started with the timer_settime() service.
 *
 * @param clockid clock used as a timing base;
 *
 * @param evp description of the asynchronous notification to occur
 * when the timer expires;
 *
 * @param timerid address where the identifier of the created timer
 * will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the clock @a clockid is invalid;
 * - EINVAL, the member @a sigev_notify of the @b sigevent structure at the
 *   address @a evp is not SIGEV_THREAD_ID;
 * - EINVAL, the  member @a sigev_signo of the @b sigevent structure is an
 *   invalid signal number;
 * - EAGAIN, the maximum number of timers was exceeded, recompile with a larger
 *   value.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_create.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, timer_create, (clockid_t clockid,
				const struct sigevent *__restrict__ evp,
				timer_t * __restrict__ timerid))
{
	int ret;

	ret = -XENOMAI_SYSCALL3(sc_cobalt_timer_create,	clockid, evp, timerid);
	if (ret == 0)
		return 0;

	errno = ret;

	return -1;
}

/**
 * Delete a timer object.
 *
 * This service deletes the timer @a timerid.
 *
 * @param timerid identifier of the timer to be removed;
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_delete.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, timer_delete, (timer_t timerid))
{
	int ret;

	ret = -XENOMAI_SYSCALL1(sc_cobalt_timer_delete, timerid);
	if (ret == 0)
		return 0;

	errno = ret;

	return -1;
}

/**
 * @fn timer_settime(timer_t timerid, int flags, const struct itimerspec *__restrict__ value, struct itimerspec *__restrict__ ovalue)
 * @brief Start or stop a timer
 *
 * This service sets a timer expiration date and reload value of the
 * timer @a timerid. If @a ovalue is not @a NULL, the current
 * expiration date and reload value are stored at the address @a
 * ovalue as with timer_gettime().
 *
 * If the member @a it_value of the @b itimerspec structure at @a
 * value is zero, the timer is stopped, otherwise the timer is
 * started. If the member @a it_interval is not zero, the timer is
 * periodic. The current thread must be a Cobalt thread (created with
 * pthread_create()) and will be notified via signal of timer
 * expirations.
 *
 * When starting the timer, if @a flags is TIMER_ABSTIME, the expiration value
 * is interpreted as an absolute date of the clock passed to the timer_create()
 * service. Otherwise, the expiration value is interpreted as a time interval.
 *
 * Expiration date and reload value are rounded to an integer count of
 * nanoseconds.
 *
 * @param timerid identifier of the timer to be started or stopped;
 *
 * @param flags one of 0 or TIMER_ABSTIME;
 *
 * @param value address where the specified timer expiration date and reload
 * value are read;
 *
 * @param ovalue address where the specified timer previous expiration date and
 * reload value are stored if not @a NULL.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the specified timer identifier, expiration date or reload value is
 *   invalid. For @a timerid to be valid, it must belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_settime.html">
 * Specification.</a>
 *
 * @apitags{xcontext, switch-primary}
 */
COBALT_IMPL_TIME64(int, timer_settime, __timer_settime64,
		   (timer_t timerid, int flags,
		    const struct itimerspec *__restrict__ value,
		    struct itimerspec *__restrict__ ovalue))
{
	int ret;

#ifdef XN_USE_TIME64_SYSCALL
	long sc_nr = sc_cobalt_timer_settime64;
#else
	long sc_nr = sc_cobalt_timer_settime;
#endif

	ret = -XENOMAI_SYSCALL4(sc_nr, timerid, flags, value, ovalue);
	if (ret == 0)
		return 0;

	errno = ret;

	return -1;
}

/**
 * @fn int timer_gettime(timer_t timerid, struct itimerspec *value)
 * @brief Get timer next expiration date and reload value.
 *
 * This service stores, at the address @a value, the expiration date
 * (member @a it_value) and reload value (member @a it_interval) of
 * the timer @a timerid. The values are returned as time intervals,
 * and as multiples of the system clock tick duration (see note in
 * section @ref cobalt_api_time "Clocks and timers services" for details
 * on the duration of the system clock tick). If the timer was not
 * started, the returned members @a it_value and @a it_interval of @a
 * value are zero.
 *
 * @param timerid timer identifier;
 *
 * @param value address where the timer expiration date and reload value are
 * stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid. For @a timerid to be valid, it
 * must belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_gettime.html">
 * Specification.</a>
 *
 * @apitags{unrestricted}
 */
COBALT_IMPL_TIME64(int, timer_gettime, __timer_gettime64,
		   (timer_t timerid, struct itimerspec *value))
{
	int ret;

#ifdef XN_USE_TIME64_SYSCALL
	long sc_nr = sc_cobalt_timer_gettime64;
#else
	long sc_nr = sc_cobalt_timer_gettime;
#endif

	ret = -XENOMAI_SYSCALL2(sc_nr, timerid, value);
	if (ret == 0)
		return 0;

	errno = ret;

	return -1;
}

/**
 * Get expiration overruns count since the most recent timer expiration
 * signal delivery.
 *
 * This service returns @a timerid expiration overruns count since the most
 * recent timer expiration signal delivery. If this count is more than @a
 * DELAYTIMER_MAX expirations, @a DELAYTIMER_MAX is returned.
 *
 * @param timerid Timer identifier.
 *
 * @return the overruns count on success;
 * @return -1 with @a errno set if:
 * - EINVAL, @a timerid is invalid;
 * - EPERM, the timer @a timerid does not belong to the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/timer_getoverrun.html">
 * Specification.</a>
 *
 * @apitags{unrestricted}
 */
COBALT_IMPL(int, timer_getoverrun, (timer_t timerid))
{
	int overrun;

	overrun = XENOMAI_SYSCALL1(sc_cobalt_timer_getoverrun, timerid);
	if (overrun >= 0)
		return overrun;

	errno = -overrun;

	return -1;
}

/** @} */

/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/clocksource.h>
#include <linux/bitmap.h>
#include <cobalt/kernel/clock.h>
#include "internal.h"
#include "thread.h"
#include "clock.h"
#include <trace/events/cobalt-posix.h>
#include <cobalt/kernel/time.h>

static struct xnclock *external_clocks[COBALT_MAX_EXTCLOCKS];

DECLARE_BITMAP(cobalt_clock_extids, COBALT_MAX_EXTCLOCKS);

#define do_ext_clock(__clock_id, __handler, __ret, __args...)	\
({								\
	struct xnclock *__clock;				\
	int __val = 0, __nr;					\
	spl_t __s;						\
								\
	if (!__COBALT_CLOCK_EXT_P(__clock_id))			\
		__val = -EINVAL;				\
	else {							\
		__nr = __COBALT_CLOCK_EXT_INDEX(__clock_id);	\
		xnlock_get_irqsave(&nklock, __s);		\
		if (!test_bit(__nr, cobalt_clock_extids)) {	\
			xnlock_put_irqrestore(&nklock, __s);	\
			__val = -EINVAL;			\
		} else {					\
			__clock = external_clocks[__nr];	\
			(__ret) = xnclock_ ## __handler(__clock, ##__args); \
			xnlock_put_irqrestore(&nklock, __s);	\
		}						\
	}							\
	__val;							\
})

int __cobalt_clock_getres(clockid_t clock_id, struct timespec64 *ts)
{
	xnticks_t ns;
	int ret;

	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		ns2ts(ts, 1);
		break;
	default:
		ret = do_ext_clock(clock_id, get_resolution, ns);
		if (ret)
			return ret;
		ns2ts(ts, ns);
	}

	trace_cobalt_clock_getres(clock_id, ts);

	return 0;
}

COBALT_SYSCALL(clock_getres, current,
	       (clockid_t clock_id, struct __kernel_old_timespec __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = __cobalt_clock_getres(clock_id, &ts);
	if (ret)
		return ret;

	if (u_ts && cobalt_put_old_timespec(u_ts, &ts))
		return -EFAULT;

	trace_cobalt_clock_getres(clock_id, &ts);

	return 0;
}

COBALT_SYSCALL(clock_getres64, current,
	       (clockid_t clock_id, struct __kernel_timespec __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = __cobalt_clock_getres(clock_id, &ts);
	if (ret)
		return ret;

	if (cobalt_put_timespec64(&ts, u_ts))
		return -EFAULT;

	trace_cobalt_clock_getres(clock_id, &ts);

	return 0;
}

int __cobalt_clock_gettime(clockid_t clock_id, struct timespec64 *ts)
{
	xnticks_t ns;
	int ret;

	switch (clock_id) {
	case CLOCK_REALTIME:
		ns2ts(ts, xnclock_read_realtime(&nkclock));
		break;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
		ns2ts(ts, xnclock_read_monotonic(&nkclock));
		break;
	case CLOCK_HOST_REALTIME:
		if (pipeline_get_host_time(ts) != 0)
			return -EINVAL;
		break;
	default:
		ret = do_ext_clock(clock_id, read_monotonic, ns);
		if (ret)
			return ret;
		ns2ts(ts, ns);
	}

	trace_cobalt_clock_gettime(clock_id, ts);

	return 0;
}

COBALT_SYSCALL(clock_gettime, current,
	       (clockid_t clock_id, struct __kernel_old_timespec __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = __cobalt_clock_gettime(clock_id, &ts);
	if (ret)
		return ret;

	if (cobalt_put_old_timespec(u_ts, &ts))
		return -EFAULT;

	return 0;
}

COBALT_SYSCALL(clock_gettime64, current,
	       (clockid_t clock_id, struct __kernel_timespec __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = __cobalt_clock_gettime(clock_id, &ts);
	if (ret)
		return ret;

	if (cobalt_put_timespec64(&ts, u_ts))
		return -EFAULT;

	return 0;
}

int __cobalt_clock_settime(clockid_t clock_id, const struct timespec64 *ts)
{
	int _ret, ret = 0;

	if ((unsigned long)ts->tv_nsec >= ONE_BILLION)
		return -EINVAL;

	switch (clock_id) {
	case CLOCK_REALTIME:
		ret = pipeline_set_wallclock(ts2ns(ts));
		break;
	default:
		_ret = do_ext_clock(clock_id, set_time, ret, ts);
		if (_ret || ret)
			return _ret ?: ret;
	}

	trace_cobalt_clock_settime(clock_id, ts);

	return ret;
}

int __cobalt_clock_adjtime(clockid_t clock_id, struct __kernel_timex *tx)
{
	int _ret, ret = 0;

	switch (clock_id) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
	case CLOCK_HOST_REALTIME:
		return -EOPNOTSUPP;
	default:
		_ret = do_ext_clock(clock_id, adjust_time, ret, tx);
		if (_ret || ret)
			return _ret ?: ret;
	}

	trace_cobalt_clock_adjtime(clock_id, tx);

	return 0;
}

COBALT_SYSCALL(clock_settime, current,
	       (clockid_t clock_id, const struct __kernel_old_timespec __user *u_ts))
{
	struct timespec64 ts;

	if (cobalt_get_old_timespec(&ts, u_ts))
		return -EFAULT;

	return __cobalt_clock_settime(clock_id, &ts);
}

COBALT_SYSCALL(clock_settime64, current,
	       (clockid_t clock_id,
		const struct __kernel_timespec __user *u_ts))
{
	struct timespec64 ts64;

	if (cobalt_get_timespec64(&ts64, u_ts))
		return -EFAULT;

	return __cobalt_clock_settime(clock_id, &ts64);
}

/* Only used by 32 bit applications without time64_t support */
COBALT_SYSCALL(clock_adjtime, current,
	       (clockid_t clock_id, struct old_timex32 __user *u_tx))
{
	struct __kernel_timex tx;
	int ret;

	if (cobalt_copy_from_user(&tx, u_tx, sizeof(tx)))
		return -EFAULT;

	ret = __cobalt_clock_adjtime(clock_id, &tx);
	if (ret)
		return ret;

	return cobalt_copy_to_user(u_tx, &tx, sizeof(tx));
}

COBALT_SYSCALL(clock_adjtime64, current,
	       (clockid_t clock_id, struct __kernel_timex __user *u_tx))
{
	struct __kernel_timex tx;
	int ret;

	if (cobalt_copy_from_user(&tx, u_tx, sizeof(tx)))
		return -EFAULT;

	ret = __cobalt_clock_adjtime(clock_id, &tx);
	if (ret)
		return ret;

	return cobalt_copy_to_user(u_tx, &tx, sizeof(tx));
}

int __cobalt_clock_nanosleep(clockid_t clock_id, int flags,
			     const struct timespec64 *rqt,
			     struct timespec64 *rmt)
{
	struct restart_block *restart;
	struct xnthread *cur;
	xnsticks_t timeout, rem;
	spl_t s;

	trace_cobalt_clock_nanosleep(clock_id, flags, rqt);

	if (clock_id != CLOCK_MONOTONIC &&
	    clock_id != CLOCK_MONOTONIC_RAW &&
	    clock_id != CLOCK_REALTIME)
		return -EOPNOTSUPP;

	if (!timespec64_valid(rqt))
		return -EINVAL;

	if (flags & ~TIMER_ABSTIME)
		return -EINVAL;

	cur = xnthread_current();

	if (xnthread_test_localinfo(cur, XNSYSRST)) {
		xnthread_clear_localinfo(cur, XNSYSRST);

		restart = &current->restart_block;

		if (restart->fn != cobalt_restart_syscall_placeholder) {
			if (rmt) {
				xnlock_get_irqsave(&nklock, s);
				rem = xntimer_get_timeout_stopped(&cur->rtimer);
				xnlock_put_irqrestore(&nklock, s);
				ns2ts(rmt, rem > 1 ? rem : 0);
			}
			return -EINTR;
		}

		timeout = restart->nanosleep.expires;
	} else
		timeout = ts2ns(rqt);

	xnlock_get_irqsave(&nklock, s);

	xnthread_suspend(cur, XNDELAY, timeout + 1,
			 clock_flag(flags, clock_id), NULL);

	if (xnthread_test_info(cur, XNBREAK)) {
		if (signal_pending(current)) {
			restart = &current->restart_block;
			restart->nanosleep.expires =
				(flags & TIMER_ABSTIME) ? timeout :
				    xntimer_get_timeout_stopped(&cur->rtimer);
			xnlock_put_irqrestore(&nklock, s);
			restart->fn = cobalt_restart_syscall_placeholder;

			xnthread_set_localinfo(cur, XNSYSRST);

			return -ERESTARTSYS;
		}

		if (flags == 0 && rmt) {
			rem = xntimer_get_timeout_stopped(&cur->rtimer);
			xnlock_put_irqrestore(&nklock, s);
			ns2ts(rmt, rem > 1 ? rem : 0);
		} else
			xnlock_put_irqrestore(&nklock, s);

		return -EINTR;
	}

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

COBALT_SYSCALL(clock_nanosleep, primary,
	       (clockid_t clock_id, int flags,
		const struct __kernel_old_timespec __user *u_rqt,
		struct __kernel_old_timespec __user *u_rmt))
{
	struct timespec64 rqt, rmt, *rmtp = NULL;
	int ret;

	if (u_rmt)
		rmtp = &rmt;

	if (cobalt_get_old_timespec(&rqt, u_rqt))
		return -EFAULT;

	ret = __cobalt_clock_nanosleep(clock_id, flags, &rqt, rmtp);
	if (ret == -EINTR && flags == 0 && rmtp) {
		if (cobalt_put_old_timespec(u_rmt, rmtp))
			return -EFAULT;
	}

	return ret;
}

COBALT_SYSCALL(clock_nanosleep64, primary,
	       (clockid_t clock_id, int flags,
		const struct __kernel_timespec __user *u_rqt,
		struct __kernel_timespec __user *u_rmt))
{
	struct timespec64 rqt, rmt, *rmtp = NULL;
	int ret;

	if (u_rmt)
		rmtp = &rmt;

	if (cobalt_get_timespec64(&rqt, u_rqt))
		return -EFAULT;

	ret = __cobalt_clock_nanosleep(clock_id, flags, &rqt, rmtp);
	if (ret == -EINTR && flags == 0 && rmtp) {
		if (cobalt_put_timespec64(rmtp, u_rmt))
			return -EFAULT;
	}

	return ret;
}

int cobalt_clock_register(struct xnclock *clock, const cpumask_t *affinity,
			  clockid_t *clk_id)
{
	int ret, nr;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	nr = find_first_zero_bit(cobalt_clock_extids, COBALT_MAX_EXTCLOCKS);
	if (nr >= COBALT_MAX_EXTCLOCKS) {
		xnlock_put_irqrestore(&nklock, s);
		return -EAGAIN;
	}

	/*
	 * CAUTION: a bit raised in cobalt_clock_extids means that the
	 * corresponding entry in external_clocks[] is valid. The
	 * converse assumption is NOT true.
	 */
	__set_bit(nr, cobalt_clock_extids);
	external_clocks[nr] = clock;

	xnlock_put_irqrestore(&nklock, s);

	ret = xnclock_register(clock, affinity);
	if (ret)
		return ret;

	clock->id = nr;
	*clk_id = __COBALT_CLOCK_EXT(clock->id);

	trace_cobalt_clock_register(clock->name, *clk_id);

	return 0;
}
EXPORT_SYMBOL_GPL(cobalt_clock_register);

void cobalt_clock_deregister(struct xnclock *clock)
{
	trace_cobalt_clock_deregister(clock->name, clock->id);
	clear_bit(clock->id, cobalt_clock_extids);
	smp_mb__after_atomic();
	external_clocks[clock->id] = NULL;
	xnclock_deregister(clock);
}
EXPORT_SYMBOL_GPL(cobalt_clock_deregister);

struct xnclock *cobalt_clock_find(clockid_t clock_id)
{
	struct xnclock *clock = ERR_PTR(-EINVAL);
	spl_t s;
	int nr;

	if (clock_id == CLOCK_MONOTONIC ||
	    clock_id == CLOCK_MONOTONIC_RAW ||
	    clock_id == CLOCK_REALTIME)
		return &nkclock;

	if (__COBALT_CLOCK_EXT_P(clock_id)) {
		nr = __COBALT_CLOCK_EXT_INDEX(clock_id);
		xnlock_get_irqsave(&nklock, s);
		if (test_bit(nr, cobalt_clock_extids))
			clock = external_clocks[nr];
		xnlock_put_irqrestore(&nklock, s);
	}

	return clock;
}
EXPORT_SYMBOL_GPL(cobalt_clock_find);

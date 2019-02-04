/*
 * Copyright (C) 2014 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <smokey/smokey.h>
#include <sys/timerfd.h>

smokey_test_plugin(posix_clock,
		   SMOKEY_NOARGS,
		   "Check POSIX clock services."
);

static int clock_increase_before_oneshot_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	int t, ret;

	smokey_trace(__func__);
	
	t = smokey_check_errno(timerfd_create(CLOCK_REALTIME, 0));
	if (t < 0)
		return t;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 0;

	ret = smokey_check_errno(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	if (ret)
		return ret;

	now.tv_sec += 5;

	ret = smokey_check_errno(clock_settime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	timer.it_value = now;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;
	
	if (!smokey_assert(now.tv_sec * 1000000000ULL + now.tv_nsec -
			   (timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec)
			   < 1000000000))
		return -EINVAL;
	
	return smokey_check_errno(close(t));
}

static int clock_increase_before_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	int t, ret;

	smokey_trace(__func__);
	
	t = smokey_check_errno(timerfd_create(CLOCK_REALTIME, 0));
	if (t < 0)
		return t;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;
	
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;
	ret = smokey_check_errno(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	if (ret)
		return ret;

	now.tv_sec += 5;

	ret = smokey_check_errno(clock_settime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;
	
	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;
	
	if (!smokey_assert(ticks == 5))
		return -EINVAL;
	
	timer.it_value = now;
	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;
	
	if (!smokey_assert(now.tv_sec * 1000000000ULL + now.tv_nsec -
			   (timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec)
			   < 1000000000))
		return -EINVAL;
	
	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;
	
	if (!smokey_assert(ticks == 1))
		return -EINVAL;
	
	return smokey_check_errno(close(t));
}

static int clock_increase_after_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	int t, ret;

	smokey_trace(__func__);
	
	t = smokey_check_errno(timerfd_create(CLOCK_REALTIME, 0));
	if (t < 0)
		return t;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;
	
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;

	ret = smokey_check_errno(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	if (ret)
		return ret;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;
	
	if (!smokey_assert(ticks == 1))
		return -EINVAL;
	
	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;
	
	now.tv_sec += 5;

	ret = smokey_check_errno(clock_settime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;
	
	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 5))
		return -EINVAL;
	
	timer.it_value = now;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	if (!smokey_assert(now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec)
			   < 1000000000))
		return -EINVAL;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	return smokey_check_errno(close(t));
}

static int clock_decrease_before_oneshot_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	long long diff;
	int t, ret;

	smokey_trace(__func__);
	
	t = smokey_check_errno(timerfd_create(CLOCK_REALTIME, 0));
	if (t < 0)
		return t;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_nsec = 0;

	ret = smokey_check_errno(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	if (ret)
		return ret;

	now.tv_sec -= 5;

	ret = smokey_check_errno(clock_settime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;
	
	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	timer.it_value = now;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	diff = now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec);

	if (!smokey_assert(diff >= 5500000000LL && diff <= 6500000000LL))
		return -EINVAL;
	
	return smokey_check_errno(close(t));
}

static int clock_decrease_before_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	long long diff;
	int t, ret;

	smokey_trace(__func__);
	
	t = smokey_check_errno(timerfd_create(CLOCK_REALTIME, 0));
	if (t < 0)
		return t;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;
	
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;

	ret = smokey_check_errno(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	if (ret)
		return ret;

	now.tv_sec -= 5;

	ret = smokey_check_errno(clock_settime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	timer.it_value = now;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	diff = now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec);

	if (!smokey_assert(diff >= 5500000000LL && diff <= 6500000000LL))
		return -EINVAL;
	
	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;
	
	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	return smokey_check_errno(close(t));
}

static int clock_decrease_after_periodic_timer_first_tick(void)
{
	unsigned long long ticks;
	struct itimerspec timer;
	struct timespec now;
	long long diff;
	int t, ret;

	smokey_trace(__func__);

	t = smokey_check_errno(timerfd_create(CLOCK_REALTIME, 0));
	if (t < 0)
		return t;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;
	
	timer.it_value = now;
	timer.it_value.tv_sec++;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_nsec = 0;

	ret = smokey_check_errno(timerfd_settime(t, TFD_TIMER_ABSTIME, &timer, NULL));
	if (ret)
		return ret;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	ret = smokey_check_errno(clock_gettime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	now.tv_sec -= 5;

	ret = smokey_check_errno(clock_settime(CLOCK_REALTIME, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	timer.it_value = now;

	ret = smokey_check_errno(clock_gettime(CLOCK_MONOTONIC, &now));
	if (ret)
		return ret;

	diff = now.tv_sec * 1000000000ULL + now.tv_nsec -
		(timer.it_value.tv_sec * 1000000000ULL + timer.it_value.tv_nsec);
	if (!smokey_assert(diff < 2000000000))
		return -EINVAL;
	
	ret = smokey_check_errno(read(t, &ticks, sizeof(ticks)));
	if (ret < 0)
		return ret;

	if (!smokey_assert(ticks == 1))
		return -EINVAL;

	return smokey_check_errno(close(t));
}

static int run_posix_clock(struct smokey_test *t, int argc, char *const argv[])
{
	int ret;

	ret = clock_increase_before_oneshot_timer_first_tick();
	if (ret)
		return ret;
	
	ret = clock_increase_before_periodic_timer_first_tick();
	if (ret)
		return ret;

	ret = clock_increase_after_periodic_timer_first_tick();
	if (ret)
		return ret;

	ret = clock_decrease_before_oneshot_timer_first_tick();
	if (ret)
		return ret;

	ret = clock_decrease_before_periodic_timer_first_tick();
	if (ret)
		return ret;

	return clock_decrease_after_periodic_timer_first_tick();
}

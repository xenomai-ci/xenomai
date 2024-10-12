/*
 * Copyright (C) 2013 Gilles Chanteperdrix <gch@xenomai.org>
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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <smokey/smokey.h>

smokey_test_plugin(timerfd,
		   SMOKEY_NOARGS,
		   "Check timerfd support."
);

#ifndef TFD_NONBLOCK
#define TFD_NONBLOCK O_NONBLOCK
#endif

static int timerfd_basic_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	int fd, i, ret;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, 0));
	if (fd < 0)
		return fd;

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;
	
	for (i = 0; i < 10; i++) {
		ret = smokey_check_errno(read(fd, &ticks, sizeof(ticks)));
		if (ret < 0)
			return ret;
		if (!smokey_assert(ret == 8))
			return -EINVAL;
		smokey_trace("%Ld direct read ticks", ticks);
		if (!smokey_assert(ticks >= 1))
			return -EINVAL;
	}
	
	return smokey_check_errno(close(fd));
}

static int timerfd_select_check(void)
{
	unsigned long long ticks;
	fd_set tmp_inset, inset;
	struct itimerspec its;
	int fd, i, ret;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
	if (fd < 0)
		return fd;

	FD_ZERO(&inset);
	FD_SET(fd, &inset); 

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;

	ret = read(fd, &ticks, sizeof(ticks));
	if (!smokey_assert(ret == -1 && errno == EAGAIN))
		return -EINVAL;
	
	for (i = 0; i < 10; i++) {
		tmp_inset = inset;
		ret = smokey_check_errno(select(fd + 1, &tmp_inset, NULL, NULL, NULL));
		if (ret < 0)
			return ret;
		ret = smokey_check_errno(read(fd, &ticks, sizeof(ticks)));
		if (ret < 0)
			return ret;
		smokey_assert(ret == 8);
		smokey_trace("%Ld select+read ticks", ticks);
		if (!smokey_assert(ticks >= 1))
			return -EINVAL;
	}
	
	return smokey_check_errno(close(fd));
}

static int timerfd_basic_overruns_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	int fd, ret, i;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, 0));
	if (fd < 0)
		return fd;

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;
	
	for (i = 0; i < 3; i++) {
		sleep(1);
		ret = smokey_check_errno(read(fd, &ticks, sizeof(ticks)));
		if (ret < 0)
			return ret;
		smokey_assert(ret == 8);
		smokey_trace("%Ld direct read ticks", ticks);
		if (!smokey_assert(ticks >= 10))
			return -EINVAL;
	}
	
	return smokey_check_errno(close(fd));
}

static int timerfd_select_overruns_check(void)
{
	unsigned long long ticks;
	fd_set tmp_inset, inset;
	struct itimerspec its;
	int fd, ret, i;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
	if (fd < 0)
		return fd;

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;

	ret = read(fd, &ticks, sizeof(ticks));
	if (!smokey_assert(ret == -1 && errno == EAGAIN))
		return -EINVAL;
	
	for (i = 0; i < 3; i++) {
		tmp_inset = inset;
		sleep(1);
		ret = smokey_check_errno(select(fd + 1, &tmp_inset, NULL, NULL, NULL));
		if (ret < 0)
			return ret;
		ret = smokey_check_errno(read(fd, &ticks, sizeof(ticks)));
		if (ret < 0)
			return ret;
		smokey_assert(ret == 8);
		smokey_trace("%Ld select+read ticks", ticks);
		if (!smokey_assert(ticks >= 10))
			return -EINVAL;
	}
	
	return smokey_check_errno(close(fd));
}

static int timerfd_select_overruns2_check(void)
{
	unsigned long long ticks;
	fd_set tmp_inset, inset;
	struct itimerspec its;
	int fd, ret, i;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
	if (fd < 0)
		return fd;

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;

	ret = read(fd, &ticks, sizeof(ticks));
	if (!smokey_assert(ret == -1 && errno == EAGAIN))
		return -EINVAL;
	
	for (i = 0; i < 3; i++) {
		tmp_inset = inset;
		ret = smokey_check_errno(select(fd + 1, &tmp_inset, NULL, NULL, NULL));
		if (ret < 0)
			return ret;
		sleep(1);
		ret = smokey_check_errno(read(fd, &ticks, sizeof(ticks)));
		if (ret < 0)
			return ret;
		smokey_assert(ret == 8);
		smokey_trace("%Ld select+read ticks", ticks);
		if (!smokey_assert(ticks >= 11))
			return -EINVAL;
	}
	
	return smokey_check_errno(close(fd));
}

static int timerfd_select_overruns_before_check(void)
{
	unsigned long long ticks;
	fd_set tmp_inset, inset;
	struct itimerspec its;
	int fd, ret, i;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
	if (fd < 0)
		return fd;

	FD_ZERO(&inset);
	FD_SET(fd, &inset);

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 100000000;

	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;

	ret = read(fd, &ticks, sizeof(ticks));
	if (!smokey_assert(ret == -1 && errno == EAGAIN))
		return -EINVAL;

	sleep(1);

	for (i = 0; i < 3; i++) {
		tmp_inset = inset;
		ret = smokey_check_errno(select(fd + 1, &tmp_inset, NULL, NULL, NULL));
		if (ret < 0)
			return ret;
		ret = smokey_check_errno(read(fd, &ticks, sizeof(ticks)));
		if (ret < 0)
			return ret;
		smokey_assert(ret == 8);
		smokey_trace("%Ld select+read ticks", ticks);
		if (!smokey_assert(ticks >= 10))
			return -EINVAL;
		sleep(1);
	}
	
	return smokey_check_errno(close(fd));
}

static ssize_t
timed_read(int fd, void *buf, size_t len, struct timespec *ts)
{
	unsigned long long ticks;
	struct itimerspec its;
	int tfd, ret;
	ssize_t err;
	
	tfd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
	if (tfd < 0)
		return tfd;
	
	its.it_value = *ts;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	
	ret = smokey_check_errno(timerfd_settime(tfd, TFD_WAKEUP, &its, NULL));
	if (ret)
		return ret;
	
	err = read(fd, buf, len);
	if (err < 0)
		err = -errno;

	if (err == -EINTR) {
		err = read(tfd, &ticks, sizeof(ticks));
		if (err > 0)
			err = -ETIMEDOUT;
		else
			err = -EINTR;
	}
	
	ret = smokey_check_errno(close(tfd));
	if (ret)
		return ret;

	if (err >= 0)
		return err;
	
	errno = -err;
	
	return -1;
}

static int timerfd_unblock_check(void)
{
	unsigned long long ticks;
	struct itimerspec its;
	int fd, ret;
	
	fd = smokey_check_errno(timerfd_create(CLOCK_MONOTONIC, 0));
	if (fd < 0)
		return fd;
	
	its.it_value.tv_sec = 5;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	
	ret = smokey_check_errno(timerfd_settime(fd, 0, &its, NULL));
	if (ret)
		return ret;

	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = 100000000;

	if (!smokey_assert(timed_read(fd, &ticks, sizeof(ticks), &its.it_value) < 0 && 
			   errno == ETIMEDOUT))
		return -EINVAL;

	return smokey_check_errno(close(fd));
}

static int run_timerfd(struct smokey_test *t, int argc, char *const argv[])
{
	int ret;
	
	ret = timerfd_basic_check();
	if (ret)
		return ret;
	
	timerfd_select_check();
	if (ret)
		return ret;

	timerfd_basic_overruns_check();
	if (ret)
		return ret;

	timerfd_select_overruns_check();
	if (ret)
		return ret;

	timerfd_select_overruns2_check();
	if (ret)
		return ret;

	timerfd_select_overruns_before_check();
	if (ret)
		return ret;

	return timerfd_unblock_check();
}

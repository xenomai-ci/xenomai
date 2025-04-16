// SPDX-License-Identifier: GPL-2.0
/*
 * y2038 tests
 *
 * Copyright (c) Siemens AG 2021
 *
 * Authors:
 *  Florian Bezdeka <florian.bezdeka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */
#include <asm/xenomai/syscall.h>
#include <smokey/smokey.h>
#include <sys/utsname.h>
#include <mqueue.h>
#include <rtdm/ipc.h>

smokey_test_plugin(y2038, SMOKEY_NOARGS, "Validate correct y2038 support");

/*
 * libc independent data type representing a time64_t based struct timespec
 */
struct xn_timespec64 {
	int64_t tv_sec;
	int64_t tv_nsec;
};

struct xn_timex_timeval {
	int64_t tv_sec;
	int64_t	tv_usec;
};

struct xn_timex64 {
	int32_t modes;		/* mode selector */
				/* pad */
	int64_t offset;		/* time offset (usec) */
	int64_t freq;		/* frequency offset (scaled ppm) */
	int64_t maxerror;	/* maximum error (usec) */
	int64_t esterror;	/* estimated error (usec) */
	int32_t status;		/* clock command/status */
				/* pad */
	int64_t constant;	/* pll time constant */
	int64_t precision;	/* clock precision (usec) (read only) */
	int64_t tolerance;	/* clock frequency tolerance (ppm) (read only) */
	struct xn_timex_timeval time;	/* (read only, except for ADJ_SETOFFSET) */
	int64_t tick;		/* (modified) usecs between clock ticks */

	int64_t ppsfreq;	/* pps frequency (scaled ppm) (ro) */
	int64_t jitter;		/* pps jitter (us) (ro) */
	int32_t shift;		/* interval duration (s) (shift) (ro) */
				/* pad */
	int64_t stabil;		/* pps stability (scaled ppm) (ro) */
	int64_t jitcnt;		/* jitter limit exceeded (ro) */
	int64_t calcnt;		/* calibration intervals (ro) */
	int64_t errcnt;		/* calibration errors (ro) */
	int64_t stbcnt;		/* stability limit exceeded (ro) */

	int32_t tai;		/* TAI offset (ro) */
};

#define NSEC_PER_SEC 1000000000

static void ts_normalise(struct xn_timespec64 *ts)
{
	while (ts->tv_nsec >= NSEC_PER_SEC) {
		ts->tv_nsec += 1;
		ts->tv_nsec -= NSEC_PER_SEC;
	}

	while (ts->tv_nsec <= -NSEC_PER_SEC) {
		ts->tv_sec -= 1;
		ts->tv_nsec += NSEC_PER_SEC;
	}

	if (ts->tv_nsec < 0) {
		/*
		 * Negative nanoseconds isn't valid according to POSIX.
		 * Decrement tv_sec and roll tv_nsec over.
		 */
		ts->tv_sec -= 1;
		ts->tv_nsec = (NSEC_PER_SEC + ts->tv_nsec);
	}
}

static inline void ts_add_ns(struct xn_timespec64 *ts, int ns)
{
	ts->tv_nsec += ns;
	ts_normalise(ts);
}

/**
 * Compare two struct timespec instances
 *
 * @param a
 * @param b
 * @return True if a < b, false otherwise
 */
static inline bool ts_less(const struct xn_timespec64 *a,
			   const struct xn_timespec64 *b)
{
	if (a->tv_sec < b->tv_sec)
		return true;

	if (a->tv_sec > b->tv_sec)
		return false;

	/* a->tv_sec == b->tv_sec */

	if (a->tv_nsec < b->tv_nsec)
		return true;

	return false;
}

/**
 * Simple helper data structure for holding a thread context
 */
struct thread_context {
	int sc_nr;
	pthread_mutex_t *mutex;
	struct xn_timespec64 *ts;
	bool timedwait_timecheck;
};

/**
 * Start the supplied function inside a separate thread, wait for completion
 * and check the thread return value.
 *
 * @param thread The thread entry point
 * @param arg The thread arguments
 * @param exp_result The expected return value
 *
 * @return 0 if the thread reported @exp_result as return value, the thread's
 * return value otherwise
 */
static int run_thread(void *(*thread)(void *), void *arg, int exp_result)
{
	pthread_t tid;
	void *status;
	long res;
	int ret;

	if (!__T(ret, pthread_create(&tid, NULL, thread, arg)))
		return ret;

	if (!__T(ret, pthread_join(tid, &status)))
		return ret;

	res = (long)status;

	return (res == exp_result) ? 0 : ret;
}

static int test_sc_cobalt_sem_timedwait64(void)
{
	int ret;
	sem_t sem;
	int sc_nr = sc_cobalt_sem_timedwait64;
	struct xn_timespec64 ts64, ts_wu;
	struct timespec ts_nat;

	sem_init(&sem, 0, 0);

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL2(sc_nr, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("sem_timedwait64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/* Timeout is never read by the kernel, so NULL should be OK */
	sem_post(&sem);
	ret = XENOMAI_SYSCALL2(sc_nr, &sem, NULL);
	if (!smokey_assert(!ret))
		return ret;

	/*
	 * The semaphore is already exhausted, so calling again will validate
	 * the provided timeout now. Providing NULL has to deliver EFAULT
	 */
	ret = XENOMAI_SYSCALL2(sc_nr, &sem, NULL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/*
	 * The semaphore is already exhausted, so calling again will validate
	 * the provided timeout now. Providing an invalid address has to deliver
	 * EFAULT
	 */
	ret = XENOMAI_SYSCALL2(sc_nr, &sem, (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/*
	 * The semaphore is still exhausted, calling again will validate the
	 * timeout, providing an invalid timeout has to deliver EINVAL
	 */
	ts64.tv_sec = -1;
	ret = XENOMAI_SYSCALL2(sc_nr, &sem, &ts64);
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a valid timeout, waiting for it to time out and check
	 * that we didn't come back to early.
	 */
	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	ts64.tv_sec = ts_nat.tv_sec;
	ts64.tv_nsec = ts_nat.tv_nsec;
	ts_add_ns(&ts64, 500000);

	ret = XENOMAI_SYSCALL2(sc_nr, &sem, &ts64);
	if (!smokey_assert(ret == -ETIMEDOUT))
		return ret ? ret : -EINVAL;

	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	ts_wu.tv_sec = ts_nat.tv_sec;
	ts_wu.tv_nsec = ts_nat.tv_nsec;

	if (ts_less(&ts_wu, &ts64))
		smokey_warning("sem_timedwait64 returned to early!\n"
			       "Expected wakeup at: %lld sec %lld nsec\n"
			       "Back at           : %lld sec %lld nsec\n",
			       ts64.tv_sec, ts64.tv_nsec, ts_wu.tv_sec,
			       ts_wu.tv_nsec);

	return 0;
}

static int test_sc_cobalt_clock_gettime64(void)
{
	int ret;
	int sc_nr = sc_cobalt_clock_gettime64;
	struct xn_timespec64 ts64 = {0};

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL2(sc_nr, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("clock_gettime64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_MONOTONIC, (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Provide a valid 64bit timespec */
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_MONOTONIC, &ts64);
	if (!smokey_assert(!ret))
		return ret;

	/* Validate seconds only, nanoseconds might still be zero */
	smokey_assert(ts64.tv_sec != 0);

	return 0;
}

static int test_sc_cobalt_clock_settime64(void)
{
	int ret;
	int sc_nr = sc_cobalt_clock_settime64;
	struct xn_timespec64 ts64, now64;
	struct timespec now;

	if (!cobalt_use_legacy_tsc())
		return 0; // Not implemented, nothing to test, success

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL2(sc_nr, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("clock_settime64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_MONOTONIC, (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	ret = clock_gettime(CLOCK_REALTIME, &now);
	if (ret)
		return -errno;

	/* Provide a valid 64bit timespec */
	ts64.tv_sec  = now.tv_sec + 1;
	ts64.tv_nsec = now.tv_nsec;
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_REALTIME, &ts64);
	if (!smokey_assert(!ret))
		return ret;

	ret = clock_gettime(CLOCK_REALTIME, &now);
	if (ret)
		return -errno;

	now64.tv_sec = now.tv_sec;
	now64.tv_nsec = now.tv_nsec;

	if (ts_less(&now64, &ts64))
		smokey_warning("clock_settime() reported no error but no new time seen");

	return 0;
}

static int test_sc_cobalt_clock_nanosleep64(void)
{
	int ret;
	int sc_nr = sc_cobalt_clock_nanosleep64;
	struct xn_timespec64 next, rmt;
	struct timespec ts1, ts2, delta;
	long interval = 1;

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL4(sc_nr, NULL, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("clock_nanosleep64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL4(sc_nr, CLOCK_MONOTONIC, TIMER_ABSTIME,
			       (void *)0xdeadbeefUL, &rmt);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Provide a valid 64bit timespec, round 1 */
	ret = clock_gettime(CLOCK_MONOTONIC, &ts1);
	if (ret)
		return -errno;

	next.tv_sec  = ts1.tv_sec + interval;
	next.tv_nsec = ts1.tv_nsec;

	ret = XENOMAI_SYSCALL4(sc_nr, CLOCK_MONOTONIC, TIMER_ABSTIME,
			       &next, (void *)0xdeadbeefUL);
	if (!smokey_assert(!ret))
		return ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts2);
	if (ret)
		return -errno;

	timespec_sub(&delta, &ts2, &ts1);
	if (delta.tv_sec < interval)
		smokey_warning("nanosleep didn't sleep long enough.");

	/* Provide a valid 64bit timespec, round 2*/
	next.tv_sec  = ts2.tv_sec + interval;
	next.tv_nsec = ts2.tv_nsec;

	ret = XENOMAI_SYSCALL4(sc_nr, CLOCK_MONOTONIC, TIMER_ABSTIME, &next,
			       &rmt);
	if (!smokey_assert(!ret))
		return ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts1);
	if (ret)
		return -errno;

	timespec_sub(&delta, &ts1, &ts2);
	if (delta.tv_sec < interval)
		smokey_warning("nanosleep didn't sleep long enough.");

	return 0;
}

static int test_sc_cobalt_clock_getres64(void)
{
	int ret;
	int sc_nr = sc_cobalt_clock_getres64;
	struct xn_timespec64 ts64;

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL2(sc_nr, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("clock_getres64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_MONOTONIC, (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Provide a valid 64bit timespec */
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_MONOTONIC, &ts64);
	if (!smokey_assert(!ret))
		return ret;

	if (ts64.tv_sec != 0 || ts64.tv_nsec != 1)
		smokey_warning("High resolution timers not available\n");

	return 0;
}

static int test_sc_cobalt_clock_adjtime64(void)
{
	int ret;
	int sc_nr = sc_cobalt_clock_adjtime64;
	struct xn_timex64 tx64 = {};

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL2(sc_nr, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("clock_adjtime64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_REALTIME, (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Provide a valid 64bit timex */
	tx64.modes = ADJ_SETOFFSET;
	tx64.time.tv_usec = 123;
	ret = XENOMAI_SYSCALL2(sc_nr, CLOCK_REALTIME, &tx64);

	/* adjtime is supported for external clocks only, expect EOPNOTSUPP */
	if (!smokey_assert(ret == -EOPNOTSUPP))
		return ret ? ret : -EINVAL;

	return 0;
}

static void *timedlock64_thread(void *arg)
{
	struct thread_context *ctx = (struct thread_context *) arg;
	struct xn_timespec64 t1 = {}; /* silence compiler warning */
	struct xn_timespec64 t2;
	struct timespec ts_nat;
	int ret;

	if (ctx->timedwait_timecheck) {
		if (!__T(ret, clock_gettime(CLOCK_REALTIME, &ts_nat)))
			return (void *)(long)ret;

		t1.tv_sec = ts_nat.tv_sec;
		t1.tv_nsec = ts_nat.tv_nsec;
		ts_add_ns(&t1, ctx->ts->tv_nsec);
		ts_add_ns(&t1, ctx->ts->tv_sec * NSEC_PER_SEC);
	}

	ret = XENOMAI_SYSCALL2(ctx->sc_nr, ctx->mutex, (void *) ctx->ts);
	if (ret)
		return (void *)(long)ret;

	if (ctx->timedwait_timecheck) {
		if (!__T(ret, clock_gettime(CLOCK_REALTIME, &ts_nat)))
			return (void *)(long)ret;

		t2.tv_sec = ts_nat.tv_sec;
		t2.tv_nsec = ts_nat.tv_nsec;

		if (ts_less(&t2, &t1))
			smokey_warning("mutex_timedlock64 returned too early!\n"
				       "Expected wakeup at: %lld sec %lld nsec\n"
				       "Back at           : %lld sec %lld nsec\n",
				       t1.tv_sec, t1.tv_nsec, t2.tv_sec,
				       t2.tv_nsec);
	}

	return (void *)(long)pthread_mutex_unlock(ctx->mutex);
}

static int test_sc_cobalt_mutex_timedlock64(void)
{
	int ret;
	pthread_mutex_t mutex;
	int sc_nr = sc_cobalt_mutex_timedlock64;
	struct xn_timespec64 ts64;
	struct thread_context ctx = {0};

	ret = pthread_mutex_init(&mutex, NULL);

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL2(sc_nr, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("mutex_timedlock64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * mutex can be taken immediately, no need to validate
	 * NULL should be allowed
	 */
	ret = XENOMAI_SYSCALL2(sc_nr, &mutex, NULL);
	if (!smokey_assert(!ret))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/*
	 * mutex can be taken immediately, no need to validate
	 * an invalid address should be allowed
	 */
	ret = XENOMAI_SYSCALL2(sc_nr, &mutex, 0xdeadbeef);
	if (!smokey_assert(!ret))
		return ret;

	/*
	 * mutex still locked, second thread has to fail with -EINVAL when
	 * submitting NULL as timeout
	 */
	ctx.sc_nr = sc_nr;
	ctx.mutex = &mutex;
	ctx.ts = NULL;
	if (!__T(ret, run_thread(timedlock64_thread, &ctx, -EINVAL)))
		return ret;

	/*
	 * mutex still locked, second thread has to fail with -EFAULT when
	 * submitting an invalid address as timeout
	 */
	ctx.ts = (void *) 0xdeadbeef;
	if (!__T(ret, run_thread(timedlock64_thread, &ctx, -EFAULT)))
		return ret;

	/*
	 * mutex still locked, second thread has to fail with -EFAULT when
	 * submitting an invalid timeout (while the address is valid)
	 */
	ts64.tv_sec = -1;
	ctx.ts = &ts64;
	if (!__T(ret, run_thread(timedlock64_thread, &ctx, -EFAULT)))
		return ret;

	/*
	 * mutex still locked, second thread has to fail with -ETIMEOUT when
	 * submitting a valid timeout
	 */
	ts64.tv_sec = 0;
	ts64.tv_nsec = 500;
	if (!__T(ret, run_thread(timedlock64_thread, &ctx, -ETIMEDOUT)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/* mutex available, second thread should be able to lock and unlock */
	ts64.tv_sec = 0;
	ts64.tv_nsec = 500;
	if (!__T(ret, run_thread(timedlock64_thread, &ctx, 0)))
		return ret;

	/*
	 * Locking the mutex here so the second thread has to deliver -ETIMEOUT.
	 * Timechecks will now be enabled to make sure we don't give up to early
	 */
	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	ts64.tv_sec = 0;
	ts64.tv_nsec = 500;
	ctx.timedwait_timecheck = true;
	if (!__T(ret, run_thread(timedlock64_thread, &ctx, -ETIMEDOUT)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	return 0;
}

static int test_sc_cobalt_mq_timedsend64(void)
{
	int ret;
	int sc_nr = sc_cobalt_mq_timedsend64;
	struct xn_timespec64 t1, t2;
	struct timespec ts_nat;

	mqd_t mq;
	struct mq_attr qa;
	char msg[64] = "Xenomai is cool!";

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL5(sc_nr, NULL, NULL, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("mq_timedsend64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EBADF))
		return ret ? ret : -EBADF;

	mq_unlink("/xenomai_mq_send");
	qa.mq_maxmsg = 1;
	qa.mq_msgsize = 64;

	mq = mq_open("/xenomai_mq_send", O_RDWR | O_CREAT, 0, &qa);
	if (mq < 0)
		return mq;

	/* Timeout is never read by the kernel, so NULL should be OK */
	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, strlen(msg), 0, NULL);
	if (!smokey_assert(!ret))
		return ret;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, strlen(msg), 0,
			       (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/*
	 * providing an invalid timeout has to deliver EINVAL
	 */
	t1.tv_sec = -1;
	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, strlen(msg), 0, &t1);
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a valid timeout, waiting for it to time out and check
	 * that we didn't come back to early.
	 */
	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t1.tv_sec = ts_nat.tv_sec;
	t1.tv_nsec = ts_nat.tv_nsec;
	ts_add_ns(&t1, 500000);

	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, strlen(msg), 0, &t1);
	if (!smokey_assert(ret == -ETIMEDOUT))
		return ret ? ret : -EINVAL;

	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t2.tv_sec = ts_nat.tv_sec;
	t2.tv_nsec = ts_nat.tv_nsec;

	if (ts_less(&t2, &t1))
		smokey_warning("mq_timedsend64 returned too early!\n"
			       "Expected wakeup at: %lld sec %lld nsec\n"
			       "Back at           : %lld sec %lld nsec\n",
			       t1.tv_sec, t1.tv_nsec, t2.tv_sec, t2.tv_nsec);

	ret = mq_unlink("/xenomai_mq_send");
	if (ret)
		return ret;

	return 0;
}

static int test_sc_cobalt_mq_timedreceive64(void)
{
	int ret;
	int sc_nr = sc_cobalt_mq_timedreceive64;
	struct xn_timespec64 t1, t2;
	struct timespec ts_nat;

	mqd_t mq;
	struct mq_attr qa;
	char msg[64];
	unsigned int prio = 0;
	size_t size = 64;

	mq_unlink("/xenomai_mq_recv");
	qa.mq_maxmsg = 1;
	qa.mq_msgsize = 64;

	mq = mq_open("/xenomai_mq_recv", O_RDWR | O_CREAT, 0, &qa);
	if (mq < 0)
		return mq;

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL5(sc_nr, mq, NULL, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("mq_timedreceive64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Send something, we want to receive it later */
	ret = mq_send(mq, "msg", 4, 0);
	if (ret)
		return ret;

	/*
	 * Timeout is never read by the kernel, so NULL should be OK, the queue
	 * is empty afterwards
	 */
	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, &size, &prio, NULL);
	if (!smokey_assert(!ret))
		return ret;

	/* Check the message content, we should have received "msg" */
	if (!smokey_assert(!memcmp(msg, "msg", 3)))
		return -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	size = 64;
	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, &size, &prio,
			       (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* providing an invalid timeout has to deliver EINVAL */
	t1.tv_sec = -1;
	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, &size, &prio, &t1);
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a valid timeout, waiting for it to time out and check
	 * that we didn't come back to early.
	 */
	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t1.tv_sec = ts_nat.tv_sec;
	t1.tv_nsec = ts_nat.tv_nsec;
	ts_add_ns(&t1, 500000);

	ret = XENOMAI_SYSCALL5(sc_nr, mq, msg, &size, &prio, &t1);
	if (!smokey_assert(ret == -ETIMEDOUT))
		return ret ? ret : -EINVAL;

	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t2.tv_sec = ts_nat.tv_sec;
	t2.tv_nsec = ts_nat.tv_nsec;

	if (ts_less(&t2, &t1))
		smokey_warning("mq_timedreceive64 returned too early!\n"
			       "Expected wakeup at: %lld sec %lld nsec\n"
			       "Back at           : %lld sec %lld nsec\n",
			       t1.tv_sec, t1.tv_nsec, t2.tv_sec, t2.tv_nsec);

	ret = mq_unlink("/xenomai_mq_recv");
	if (ret)
		return ret;

	return 0;
}

static int test_sc_cobalt_sigtimedwait64(void)
{
	int ret;
	int sc_nr = sc_cobalt_sigtimedwait64;
	struct xn_timespec64 t1, t2;
	struct timespec ts_nat;
	sigset_t set;

	sigaddset(&set, SIGINT);

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL3(sc_nr, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("sigtimedwait64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL3(sc_nr, &set, NULL, (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* providing an invalid timeout has to deliver EINVAL */
	t1.tv_sec = -1;
	ret = XENOMAI_SYSCALL3(sc_nr, &set, NULL, &t1);
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a zero timeout, should come back immediately, no signal
	 * will be received
	 */
	t1.tv_sec = 0;
	t1.tv_nsec = 0;
	ret = XENOMAI_SYSCALL3(sc_nr, &set, NULL, &t1);
	if (!smokey_assert(ret == -EAGAIN))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a valid timeout, waiting for it to time out and check
	 * that we didn't come back to early.
	 */
	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t1.tv_sec = 0;
	t1.tv_nsec = 500000;

	ret = XENOMAI_SYSCALL3(sc_nr, &set, NULL, &t1);
	if (!smokey_assert(ret == -EAGAIN))
		return ret;

	t1.tv_sec = ts_nat.tv_sec;
	t1.tv_nsec = ts_nat.tv_nsec;
	ts_add_ns(&t1, 500000);

	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t2.tv_sec = ts_nat.tv_sec;
	t2.tv_nsec = ts_nat.tv_nsec;

	if (ts_less(&t2, &t1))
		smokey_warning("sigtimedwait64 returned too early!\n"
			       "Expected wakeup at: %lld sec %lld nsec\n"
			       "Back at           : %lld sec %lld nsec\n",
			       t1.tv_sec, t1.tv_nsec, t2.tv_sec, t2.tv_nsec);

	return 0;
}

static int test_sc_cobalt_monitor_wait64(void)
{
	int ret, opret;
	int sc_nr = sc_cobalt_monitor_wait64;
	struct xn_timespec64 t1, t2, to;
	struct timespec ts_nat;
	struct cobalt_monitor_shadow mon;

	ret = cobalt_monitor_init(&mon, CLOCK_REALTIME, 0);
	if (ret)
		return -errno;

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL4(sc_nr, NULL, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("monitor_wait64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL4(sc_nr, &mon, COBALT_MONITOR_WAITGRANT,
			       (void *)0xdeadbeefUL, NULL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* providing an invalid timeout has to deliver EINVAL */
	t1.tv_sec = -1;
	ret = XENOMAI_SYSCALL4(sc_nr, &mon, COBALT_MONITOR_WAITGRANT, &t1,
			       NULL);
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a valid timeout, waiting for it to time out and check
	 * that we didn't come back to early.
	 */
	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t1.tv_sec = ts_nat.tv_sec;
	t1.tv_nsec = ts_nat.tv_nsec;

	to = t1;
	ts_add_ns(&to, 50000);

	ret = XENOMAI_SYSCALL4(sc_nr, &mon, COBALT_MONITOR_WAITGRANT, &to,
			       &opret);
	if (!smokey_assert(opret == -ETIMEDOUT))
		return ret;

	ret = clock_gettime(CLOCK_REALTIME, &ts_nat);
	if (ret)
		return -errno;

	t2.tv_sec = ts_nat.tv_sec;
	t2.tv_nsec = ts_nat.tv_nsec;

	if (ts_less(&t2, &to))
		smokey_warning("monitor_wait64 returned too early!\n"
			       "Expected wakeup at: %lld sec %lld nsec\n"
			       "Back at           : %lld sec %lld nsec\n",
			       to.tv_sec, to.tv_nsec, t2.tv_sec, t2.tv_nsec);

	if (!__T(ret, cobalt_monitor_destroy(&mon)))
		return ret;

	return 0;
}

static int test_sc_cobalt_event_wait64(void)
{
	int ret;
	int sc_nr = sc_cobalt_event_wait64;
	struct xn_timespec64 t1, t2;
	struct timespec ts_nat;
	struct cobalt_event_shadow evt;
	unsigned int flags;

	ret = cobalt_event_init(&evt, 0, COBALT_EVENT_FIFO);
	if (ret)
		return -errno;

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL5(sc_nr, NULL, NULL, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("event_wait64: skipped. (no kernel support)");
		return 0; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL5(sc_nr, &evt, 0x1, &flags, 0,
			       (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT))
		return ret ? ret : -EINVAL;

	/* providing an invalid timeout has to deliver EINVAL */
	t1.tv_sec = -1;
	ret = XENOMAI_SYSCALL5(sc_nr, &evt, 0x1, &flags, 0, &t1);
	if (!smokey_assert(ret == -EINVAL))
		return ret ? ret : -EINVAL;

	/*
	 * providing a zero timeout,
	 * should come back immediately with EWOULDBLOCK
	 */
	t1.tv_sec = 0;
	t1.tv_nsec = 0;
	ret = XENOMAI_SYSCALL5(sc_nr, &evt, 0x1, &flags, 0, &t1);
	if (!smokey_assert(ret == -EWOULDBLOCK))
		return ret ? ret : -EINVAL;

	/*
	 * Providing a valid timeout, waiting for it to time out and check
	 * that we didn't come back to early.
	 */
	ret = clock_gettime(CLOCK_MONOTONIC, &ts_nat);
	if (ret)
		return -errno;

	t1.tv_sec = ts_nat.tv_sec;
	t1.tv_nsec = ts_nat.tv_nsec;
	ts_add_ns(&t1, 500000);

	ret = XENOMAI_SYSCALL5(sc_nr, &evt, 0x1, &flags, 0, &t1);
	if (!smokey_assert(ret == -ETIMEDOUT))
		return ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts_nat);
	if (ret)
		return -errno;

	t2.tv_sec = ts_nat.tv_sec;
	t2.tv_nsec = ts_nat.tv_nsec;

	if (ts_less(&t2, &t1))
		smokey_warning("event_wait64 returned too early!\n"
			       "Expected wakeup at: %lld sec %lld nsec\n"
			       "Back at           : %lld sec %lld nsec\n",
			       t1.tv_sec, t1.tv_nsec, t2.tv_sec, t2.tv_nsec);

	if (!__T(ret, cobalt_event_destroy(&evt)))
		return ret;

	return 0;
}

static int test_sc_cobalt_recvmmsg64(void)
{
	int ret = 0;
	int sock;
	int sc_nr = sc_cobalt_recvmmsg64;
	long data;
	struct xn_timespec64 t1;
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;

	struct iovec iov = {
		.iov_base = &data,
		.iov_len = sizeof(data),
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
	};

	sock = smokey_check_errno(socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP));
	if (sock == -EAFNOSUPPORT) {
		smokey_note("recvmmsg64: skipped. (no kernel support)");
		return 0;
	}
	if (sock < 0)
		return sock;

	strcpy(plabel.label, "y2038:recvmmsg64");
	ret = smokey_check_errno(setsockopt(sock, SOL_XDDP, XDDP_LABEL, &plabel,
					    sizeof(plabel)));
	if (ret)
		goto out;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = -1;
	ret = smokey_check_errno(bind(sock, (struct sockaddr *)&saddr,
				      sizeof(saddr)));
	if (ret)
		goto out;

	/* Make sure we don't crash because of NULL pointers */
	ret = XENOMAI_SYSCALL5(sc_nr, NULL, NULL, NULL, NULL, NULL);
	if (ret == -ENOSYS) {
		smokey_note("recvmmsg64: skipped. (no kernel support)");
		goto out; // Not implemented, nothing to test, success
	}
	if (!smokey_assert(ret == -EADV)) {
		ret = ret ? ret : -EINVAL;
		goto out;
	}

	/* Providing an invalid address has to deliver EFAULT */
	ret = XENOMAI_SYSCALL5(sc_nr, sock, &msg, sizeof(msg), 0,
			       (void *)0xdeadbeefUL);
	if (!smokey_assert(ret == -EFAULT)) {
		ret = ret ? ret : -EINVAL;
		goto out;
	}

	/*
	 * providing a non-zero timeout has to deliver EINVAL
	 */
	t1.tv_nsec = 1;
	ret = XENOMAI_SYSCALL5(sc_nr, sock, &msg, sizeof(msg), 0, &t1);
	if (!smokey_assert(ret == -EINVAL)) {
		ret = ret ? ret : -EINVAL;
		goto out;
	}

	/*
	 * providing a zero timeout,
	 * should come back immediately with EWOULDBLOCK
	 */
	t1.tv_sec = 0;
	t1.tv_nsec = 0;
	ret = XENOMAI_SYSCALL5(sc_nr, sock, &msg, sizeof(msg), 0, &t1);
	if (!smokey_assert(ret == -EWOULDBLOCK)) {
		ret = ret ? ret : -EINVAL;
		goto out;
	}

	ret = 0;

out:
	close(sock);
	return ret;
}


static int check_kernel_version(void)
{
	int ret, major, minor;
	struct utsname uts;

	ret = smokey_check_errno(uname(&uts));
	if (ret)
		return ret;

	ret = sscanf(uts.release, "%d.%d", &major, &minor);
	if (!smokey_assert(ret == 2))
		return -EINVAL;

	/* We need a kernel with y2038 support, 5.4 onwards */
	if (!(major > 5 || (major == 5 && minor >= 4))) {
		smokey_note("y2038: skipped. (no y2038 safe kernel)");
		return 1;
	}

	return 0;
}

static int run_y2038(struct smokey_test *t, int argc, char *const argv[])
{
	int ret;

	ret = check_kernel_version();
	if (ret)
		return (ret < 0) ? ret : 0; /* skip if no y2038 safe kernel */

	ret = test_sc_cobalt_sem_timedwait64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_clock_gettime64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_clock_settime64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_clock_nanosleep64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_clock_getres64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_clock_adjtime64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_mutex_timedlock64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_mq_timedsend64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_mq_timedreceive64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_sigtimedwait64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_monitor_wait64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_event_wait64();
	if (ret)
		return ret;

	ret = test_sc_cobalt_recvmmsg64();
	if (ret)
		return ret;

	return 0;
}

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
#include <cobalt/uapi/syscall.h>
#include <smokey/smokey.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

smokey_test_plugin(y2038, SMOKEY_NOARGS, "Validate correct y2038 support");

/*
 * libc independent data type representing a time64_t based struct timespec
 */
struct xn_timespec64 {
	int64_t tv_sec;
	int64_t tv_nsec;
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
		return ret ? ret : -EINVAL;

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
		return ret;

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

static int run_y2038(struct smokey_test *t, int argc, char *const argv[])
{
	int ret;

	ret = test_sc_cobalt_sem_timedwait64();
	if (ret)
		return ret;

	return 0;
}

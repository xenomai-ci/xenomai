/*
 * Functional testing of the mutex implementation for Cobalt.
 *
 * Copyright (C) Gilles Chanteperdrix  <gilles.chanteperdrix@xenomai.org>,
 *               Marion Deveaud <marion.deveaud@siemens.com>,
 *               Jan Kiszka <jan.kiszka@siemens.com>
 *               Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <cobalt/sys/cobalt.h>
#include <smokey/smokey.h>

smokey_test_plugin(posix_mutex,
		   SMOKEY_NOARGS,
		   "Check POSIX mutex services"
);

static const char *reason_str[] = {
	[SIGDEBUG_UNDEFINED] = "received SIGDEBUG for unknown reason",
	[SIGDEBUG_MIGRATE_SIGNAL] = "received signal",
	[SIGDEBUG_MIGRATE_SYSCALL] = "invoked syscall",
	[SIGDEBUG_MIGRATE_FAULT] = "triggered fault",
	[SIGDEBUG_MIGRATE_PRIOINV] = "affected by priority inversion",
	[SIGDEBUG_NOMLOCK] = "process memory not locked",
	[SIGDEBUG_WATCHDOG] = "watchdog triggered (period too short?)",
	[SIGDEBUG_LOCK_BREAK] = "scheduler lock break",
};

static void sigdebug(int sig, siginfo_t *si, void *context)
{
	const char fmt[] = "%s, this is unexpected.\n"
		"(enabling CONFIG_XENO_OPT_DEBUG_TRACE_RELAX may help)\n";
	unsigned int reason = sigdebug_reason(si);
	int n __attribute__ ((unused));
	static char buffer[256];

	if (reason > SIGDEBUG_WATCHDOG)
		reason = SIGDEBUG_UNDEFINED;

	n = snprintf(buffer, sizeof(buffer), fmt, reason_str[reason]);
	n = write(STDERR_FILENO, buffer, n);
}

#define THREAD_PRIO_WEAK	0
#define THREAD_PRIO_LOW		1
#define THREAD_PRIO_MEDIUM	2
#define THREAD_PRIO_HIGH	3
#define THREAD_PRIO_VERY_HIGH	4

#define MAX_100_MS  100000000ULL

struct locker_context {
	pthread_mutex_t *mutex;
	struct smokey_barrier *barrier;
	int lock_acquired;
};

static void sleep_ms(unsigned int ms)	/* < 1000 */
{
	struct timespec ts;
	
	ts.tv_sec = 0;
	ts.tv_nsec = ms * 1000000;
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static int get_effective_prio(void) 
{
	struct cobalt_threadstat stat;
	int ret;

	ret = cobalt_thread_stat(0, &stat);
	if (ret)
		return ret;

	return stat.cprio;
}

static int create_thread(pthread_t *tid, int policy, int prio,
			 void *(*thread)(void *), void *arg)
{
	struct sched_param param;
	pthread_attr_t thattr;
	int ret;

	pthread_attr_init(&thattr);
	param.sched_priority = prio;
	pthread_attr_setschedpolicy(&thattr, policy);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);

	if (!__T(ret, pthread_create(tid, &thattr, thread, arg)))
		return ret;

	return 0;
}

static int do_init_mutexattr(pthread_mutexattr_t *mattr, int type, int protocol)
{
	int ret;

	if (!__T(ret, pthread_mutexattr_init(mattr)))
		return ret;
	
	if (!__T(ret, pthread_mutexattr_settype(mattr, type)))
		return ret;
	
	if (!__T(ret, pthread_mutexattr_setprotocol(mattr, protocol)))
		return ret;
	
	if (!__T(ret, pthread_mutexattr_setpshared(mattr, PTHREAD_PROCESS_PRIVATE)))
		return ret;

	return 0;
}

static int do_init_mutex(pthread_mutex_t *mutex, int type, int protocol)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = do_init_mutexattr(&mattr, type, protocol);
	if (ret)
		return ret;
	
	if (!__T(ret, pthread_mutex_init(mutex, &mattr)))
		return ret;

	if (!__T(ret, pthread_mutexattr_destroy(&mattr)))
		return ret;
	
	return 0;
}

static int do_init_mutex_ceiling(pthread_mutex_t *mutex, int type, int prio)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = do_init_mutexattr(&mattr, type, PTHREAD_PRIO_PROTECT);
	if (ret)
		return ret;
	
	if (!__T(ret, pthread_mutexattr_setprioceiling(&mattr, prio)))
		return ret;

	if (!__T(ret, pthread_mutex_init(mutex, &mattr)))
		return ret;

	if (!__T(ret, pthread_mutexattr_destroy(&mattr)))
		return ret;
	
	return 0;
}

static void *mutex_timed_locker(void *arg)
{
	struct locker_context *p = arg;
	struct timespec now, ts;
	int ret;

	clock_gettime(CLOCK_REALTIME, &now);
	/* 5ms (or 50ms in VM) from now */
	timespec_adds(&ts, &now, smokey_on_vm ? 50000000 : 5000000);

	if (p->barrier)
		smokey_barrier_release(p->barrier);
	
	if (__F(ret, pthread_mutex_timedlock(p->mutex, &ts)) &&
	    __Tassert(ret == -ETIMEDOUT))
		return (void *)1;

	return NULL;
}

static int do_timed_contend(pthread_mutex_t *mutex, int prio)
{
	struct locker_context args = { .barrier = NULL };
	pthread_t tid;
	void *status;
	int ret;

	if (!__T(ret, pthread_mutex_lock(mutex)))
		return ret;

	args.mutex = mutex;
	ret = create_thread(&tid, SCHED_FIFO, prio,
			    mutex_timed_locker, &args);
	if (ret)
		return ret;
	
	if (!__T(ret, pthread_join(tid, &status)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(mutex)))
		return ret;

	if (!__Fassert(status == NULL))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(mutex)))
		return ret;

	return 0;
}

static void *mutex_locker(void *arg)
{
	struct locker_context *p = arg;
	int ret;

	if (!__T(ret, pthread_mutex_lock(p->mutex)))
		return (void *)(long)ret;

	p->lock_acquired = 1;

	if (!__T(ret, pthread_mutex_unlock(p->mutex)))
		return (void *)(long)ret;

	smokey_barrier_release(p->barrier);

	return NULL;
}

static int do_contend(pthread_mutex_t *mutex, int type)
{
	struct smokey_barrier barrier;
	struct locker_context args;
	pthread_t tid;
	void *status;
	int ret;

	if (!__T(ret, pthread_mutex_lock(mutex)))
		return ret;

	if (type == PTHREAD_MUTEX_RECURSIVE) {
		if (!__T(ret, pthread_mutex_lock(mutex)))
			return ret;
	} else if (type == PTHREAD_MUTEX_ERRORCHECK) {
		if (!__F(ret, pthread_mutex_lock(mutex)) ||
		    !__Tassert(ret == -EDEADLK))
			return -EINVAL;
	}

	args.mutex = mutex;
	smokey_barrier_init(&barrier);
	args.barrier = &barrier;
	args.lock_acquired = 0;
	ret = create_thread(&tid, SCHED_FIFO, THREAD_PRIO_MEDIUM,
			    mutex_locker, &args);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_unlock(mutex)))
		return ret;

	if (type == PTHREAD_MUTEX_RECURSIVE) {
		if (!__T(ret, pthread_mutex_unlock(mutex)))
			return ret;
	} else if (type == PTHREAD_MUTEX_ERRORCHECK) {
		if (!__F(ret, pthread_mutex_unlock(mutex)) ||
		    !__Tassert(ret == -EPERM))
			return -EINVAL;
	}

	/* Wait until locker runs through. */
	if (!__T(ret, smokey_barrier_wait(&barrier)))
		return ret;

	if (!__T(ret, pthread_mutex_lock(mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_destroy(mutex)))
		return ret;

	if (!__T(ret, pthread_join(tid, &status)))
		return ret;

	if (!__Tassert(status == NULL))
		return -EINVAL;

	smokey_barrier_destroy(&barrier);

	return 0;
}

static int do_destroy(pthread_mutex_t *mutex, pthread_mutex_t *invalmutex,
	int type)
{
	int ret;

	if (!__T(ret, pthread_mutex_destroy(mutex)))
		return ret;
	if (!__F(ret, pthread_mutex_destroy(invalmutex)) &&
		__Tassert(ret == -EINVAL))
		return -1;
	return 0;
}

static int static_init_normal_destroy(void)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t invalmutex = PTHREAD_MUTEX_INITIALIZER;

	unsigned int invalmagic = ~0x86860303; // ~COBALT_MUTEX_MAGIC

	memcpy((char *)&invalmutex + sizeof(invalmutex) - sizeof(invalmagic),
		&invalmagic, sizeof(invalmagic));
	return do_destroy(&mutex, &invalmutex, PTHREAD_MUTEX_NORMAL);
}

static int static_init_normal_contend(void)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	return do_contend(&mutex, PTHREAD_MUTEX_NORMAL);
}

static int __dynamic_init_contend(int type)
{
	pthread_mutex_t mutex;
	int ret;

	ret = do_init_mutex(&mutex, type, PTHREAD_PRIO_NONE);
	if (ret)
		return ret;
	
	return do_contend(&mutex, type);
}

static int dynamic_init_normal_contend(void)
{
	return __dynamic_init_contend(PTHREAD_MUTEX_NORMAL);
}

static int static_init_recursive_destroy(void)
{
	pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
	pthread_mutex_t invalmutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

	unsigned int invalmagic = ~0x86860303; // ~COBALT_MUTEX_MAGIC

	memcpy((char *)&invalmutex + sizeof(invalmutex) - sizeof(invalmagic),
		&invalmagic, sizeof(invalmagic));
	return do_destroy(&mutex, &invalmutex, PTHREAD_MUTEX_RECURSIVE);
}

static int static_init_recursive_contend(void)
{
	pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

	return do_contend(&mutex, PTHREAD_MUTEX_RECURSIVE);
}

static int dynamic_init_recursive_contend(void)
{
	return __dynamic_init_contend(PTHREAD_MUTEX_RECURSIVE);
}

static int static_init_errorcheck_destroy(void)
{
	pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
	pthread_mutex_t invalmutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

	unsigned int invalmagic = ~0x86860303; // ~COBALT_MUTEX_MAGIC

	memcpy((char *)&invalmutex + sizeof(invalmutex) - sizeof(invalmagic),
		&invalmagic, sizeof(invalmagic));
	return do_destroy(&mutex, &invalmutex, PTHREAD_MUTEX_ERRORCHECK);
}

static int static_init_errorcheck_contend(void)
{
	pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

	return do_contend(&mutex, PTHREAD_MUTEX_ERRORCHECK);
}

static int dynamic_init_errorcheck_contend(void)
{
	return __dynamic_init_contend(PTHREAD_MUTEX_ERRORCHECK);
}

static int timed_contend(void)
{
	pthread_mutex_t mutex;
	int ret;

	ret = do_init_mutex(&mutex, PTHREAD_MUTEX_NORMAL,
			    PTHREAD_PRIO_INHERIT);
	if (ret)
		return ret;

	return do_timed_contend(&mutex, THREAD_PRIO_MEDIUM);
}

static int weak_mode_switch(void)
{
	struct sched_param old_param, param = { .sched_priority = 0 };
	int old_policy, ret, mode;
	pthread_mutex_t mutex;

	ret = do_init_mutex(&mutex, PTHREAD_MUTEX_NORMAL,
			    PTHREAD_PRIO_INHERIT);
	if (ret)
		return ret;

	/* Save old schedparams, then switch to weak mode. */

	if (!__T(ret, pthread_getschedparam(pthread_self(),
					    &old_policy, &old_param)))
		return ret;

	/* Assume we are running SCHED_FIFO. */

	mode = cobalt_thread_mode();
	if (!__Fassert(mode & XNWEAK))
		return -EINVAL;

	/* Enter SCHED_WEAK scheduling. */
	
	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    SCHED_OTHER, &param)))
		return ret;

	mode = cobalt_thread_mode();
	if (!__Tassert((mode & (XNWEAK|XNRELAX)) == (XNWEAK|XNRELAX)))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	/*
	 * Holding a mutex should have switched us out of relaxed
	 * mode despite being assigned to the SCHED_WEAK class.
	 */
	mode = cobalt_thread_mode();
	if (!__Tassert((mode & (XNWEAK|XNRELAX)) == XNWEAK))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/* Dropped it, we should have relaxed in the same move. */
	
	mode = cobalt_thread_mode();
	if (!__Tassert((mode & (XNWEAK|XNRELAX)) == (XNWEAK|XNRELAX)))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	/* Leaving the SCHED_WEAK class. */

	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    old_policy, &old_param)))
		return ret;

	mode = cobalt_thread_mode();
	if (!__Fassert(mode & XNWEAK))
		return -EINVAL;

	return 0;
}

static int do_pi_contend(int prio)
{
	struct smokey_barrier barrier;
	struct locker_context args;
	pthread_mutex_t mutex;
	pthread_t tid;
	void *status;
	int ret;

	ret = do_init_mutex(&mutex, PTHREAD_MUTEX_NORMAL,
			    PTHREAD_PRIO_INHERIT);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	args.mutex = &mutex;
	smokey_barrier_init(&barrier);
	args.barrier = &barrier;
	ret = create_thread(&tid, SCHED_FIFO, prio,
			    mutex_timed_locker, &args);
	if (ret)
		return ret;

	if (!__T(ret, smokey_barrier_wait(&barrier)))
		return ret;

	/*
	 * Back while mutex_timed_locker is waiting. We should have
	 * been boosted by now.
	 */
	if (!__Tassert(get_effective_prio() == prio))
		return -EINVAL;
	
	if (!__T(ret, pthread_join(tid, &status)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__Fassert(status == NULL))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	smokey_barrier_destroy(&barrier);

	return 0;
}

static int pi_contend(void)
{
	return do_pi_contend(THREAD_PRIO_HIGH);
}

static void *mutex_locker_steal(void *arg)
{
	struct locker_context *p = arg;
	int ret;

	smokey_barrier_release(p->barrier);
	
	if (!__T(ret, pthread_mutex_lock(p->mutex)))
		return (void *)(long)ret;

	p->lock_acquired = 1;

	if (!__T(ret, pthread_mutex_unlock(p->mutex)))
		return (void *)(long)ret;

	return NULL;
}

static int do_steal(int may_steal)
{
	struct smokey_barrier barrier;
	struct locker_context args;
	pthread_mutex_t mutex;
	pthread_t tid;
	void *status;
	int ret;

	ret = do_init_mutex(&mutex, PTHREAD_MUTEX_NORMAL,
			    PTHREAD_PRIO_NONE);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	args.mutex = &mutex;
	smokey_barrier_init(&barrier);
	args.barrier = &barrier;
	args.lock_acquired = 0;
	ret = create_thread(&tid, SCHED_FIFO, THREAD_PRIO_LOW,
			    mutex_locker_steal, &args);
	if (ret)
		return ret;

	/* Make sure the locker thread emerges... */
	if (!__T(ret, smokey_barrier_wait(&barrier)))
		return ret;

	/* ...and blocks waiting on the mutex. */
	sleep_ms(1);

	/*
	 * Back while mutex_locker should be blocking.
	 *
	 * If stealing is exercised, unlock then relock immediately:
	 * we should have kept the ownership of the mutex and the
	 * locker thread should not have grabbed it so far, because of
	 * our higher priority.
	 *
	 * If stealing should not happen, unlock, wait a moment then
	 * observe whether the locker thread was able to grab it as
	 * expected.
	 *
	 * CAUTION: don't use pthread_mutex_trylock() to re-grab the
	 * mutex, this is not going to do what you want, since there
	 * is no stealing from userland, so using a fast op which
	 * never enters the kernel won't help.
	 */
	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (may_steal) {
		if (!__T(ret, pthread_mutex_lock(&mutex)))
			return ret;

		if (!__Fassert(args.lock_acquired))
			return -EINVAL;
	} else {
		sleep_ms(1);

		if (!__T(ret, pthread_mutex_lock(&mutex)))
			return ret;

		if (!__Tassert(args.lock_acquired))
			return -EINVAL;
	}

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__T(ret, pthread_join(tid, &status)))
		return ret;

	if (!__Tassert(status == NULL))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	smokey_barrier_destroy(&barrier);

	return 0;
}

static int steal(void)
{
	return do_steal(1);
}

static int no_steal(void)
{
	return do_steal(0);
}

/*
 * NOTE: Cobalt implements a lazy enforcement scheme for priority
 * protection of threads running in primary mode, which only registers
 * a pending boost at locking time, committing it eventually when/if
 * the owner thread schedules away while holding it. Entering a short
 * sleep (in primary mode) right after a mutex is grabbed makes sure
 * the boost is actually applied.
 */
static int protect_raise(void)
{
	pthread_mutex_t mutex;
	int ret;

	ret = do_init_mutex_ceiling(&mutex, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	/* We should have been given a MEDIUM -> HIGH boost. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;
	
	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_MEDIUM))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	return 0;
}

static int protect_lower(void)
{
	pthread_mutex_t mutex;
	int ret;

	ret = do_init_mutex_ceiling(&mutex, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_LOW);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	/* No boost should be applied. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_MEDIUM))
		return -EINVAL;
	
	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_MEDIUM))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	return 0;
}

static int protect_weak(void)
{
	struct sched_param old_param, weak_param;
	pthread_mutex_t mutex;
	int ret, old_policy;

	if (!__T(ret, pthread_getschedparam(pthread_self(),
					    &old_policy, &old_param)))
		return ret;

	/*
	 * Switch to the SCHED_WEAK class if present. THREAD_PRIO_WEAK
	 * (0) is used to make this work even without SCHED_WEAK
	 * support.
	 */
	weak_param.sched_priority = THREAD_PRIO_WEAK;
	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    SCHED_WEAK, &weak_param)))
		return ret;

	ret = do_init_mutex_ceiling(&mutex, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	/* We should have been sent to SCHED_FIFO, THREAD_PRIO_HIGH. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;
	
	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/* Back to SCHED_WEAK, THREAD_PRIO_WEAK. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_WEAK))
		return -EINVAL;

	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    old_policy, &old_param)))
		return ret;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	return 0;
}

static int protect_nesting_protect(void)
{
	pthread_mutex_t mutex_high, mutex_very_high;
	int ret;

	ret = do_init_mutex_ceiling(&mutex_high, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	ret = do_init_mutex_ceiling(&mutex_very_high, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_VERY_HIGH);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex_high)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_lock(&mutex_very_high)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_VERY_HIGH))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_unlock(&mutex_very_high)))
		return ret;

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;
	
	if (!__T(ret, pthread_mutex_unlock(&mutex_high)))
		return ret;

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_MEDIUM))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex_high)) ||
	    !__T(ret, pthread_mutex_destroy(&mutex_very_high)))
		return ret;

	return 0;
}

static int protect_nesting_pi(void)
{
	pthread_mutex_t mutex_pp;
	int ret;

	ret = do_init_mutex_ceiling(&mutex_pp, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex_pp)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	/* PP ceiling: MEDIUM -> HIGH */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;
	
	/* PI boost expected: HIGH -> VERY_HIGH, then back to HIGH */
	ret = do_pi_contend(THREAD_PRIO_VERY_HIGH);
	if (ret)
		return ret;

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;
	
	if (!__T(ret, pthread_mutex_unlock(&mutex_pp)))
		return ret;

	/* PP boost just dropped: HIGH -> MEDIUM. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_MEDIUM))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex_pp)))
		return ret;

	return 0;
}

static int protect_dynamic(void)
{
	pthread_mutex_t mutex;
	int ret, old_ceiling;

	ret = do_init_mutex_ceiling(&mutex, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_setprioceiling(&mutex,
						   THREAD_PRIO_VERY_HIGH, &old_ceiling)))
		return ret;

	if (!__Tassert(old_ceiling == THREAD_PRIO_HIGH))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	sleep_ms(1);	/* Commit the pending PP request. */

	/* We should have been given a HIGH -> VERY_HIGH boost. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_VERY_HIGH))
		return -EINVAL;
	
	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/* Drop the boost: VERY_HIGH -> MEDIUM. */
	if (!__Tassert(get_effective_prio() == THREAD_PRIO_MEDIUM))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_getprioceiling(&mutex, &old_ceiling)))
		return ret;

	if (!__Tassert(old_ceiling == THREAD_PRIO_VERY_HIGH))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	return 0;
}

static int protect_trylock(void)
{
	pthread_mutex_t mutex;
	int ret;

	ret = do_init_mutex_ceiling(&mutex, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	/* make sure we are primary to take the fast-path */
	sleep_ms(1);

	if (!__T(ret, pthread_mutex_trylock(&mutex)))
		return ret;

	if (!__Tassert(pthread_mutex_trylock(&mutex) == EBUSY))
		return -EINVAL;

	sleep_ms(1);	/* Commit the pending PP request. */

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/* force to secondary to take the slow-path */
	__real_usleep(1);

	if (!__T(ret, pthread_mutex_trylock(&mutex)))
		return ret;

	/* force to secondary to take the slow-path */
	if (!__Tassert(pthread_mutex_trylock(&mutex) == EBUSY))
		return -EINVAL;

	sleep_ms(1);	/* Commit the pending PP request. */

	if (!__Tassert(get_effective_prio() == THREAD_PRIO_HIGH))
		return -EINVAL;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	return 0;
}

static int protect_handover(void)
{
	struct smokey_barrier barrier;
	struct locker_context args;
	pthread_mutex_t mutex;
	pthread_t tid;
	int ret;

	ret = do_init_mutex_ceiling(&mutex, PTHREAD_MUTEX_NORMAL,
				    THREAD_PRIO_HIGH);
	if (ret)
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	args.mutex = &mutex;
	smokey_barrier_init(&barrier);
	args.barrier = &barrier;
	args.lock_acquired = 0;
	ret = create_thread(&tid, SCHED_FIFO, THREAD_PRIO_LOW,
			    mutex_locker, &args);
	if (ret)
		return ret;

	sleep_ms(1);	/* Wait for the locker to contend on the mutex. */

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	/* Make sure the locker thread released the lock again. */
	if (!__T(ret, smokey_barrier_wait(&barrier)))
		return ret;

	if (!__T(ret, pthread_mutex_lock(&mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	smokey_barrier_destroy(&barrier);

	return 0;
}

static void *mutex_timed_locker_inv_timeout(void *arg)
{
	struct locker_context *p = arg;
	int ret;

	if (__F(ret, pthread_mutex_timedlock(p->mutex, (void *) 0xdeadbeef)) &&
	    __Tassert(ret == -EFAULT))
		return (void *)1;

	return NULL;
}

static int check_timedlock_abstime_validation(void)
{
	struct locker_context args;
	pthread_mutex_t mutex;
	pthread_t tid;
	void *status;
	int ret;

	if (!__T(ret, pthread_mutex_init(&mutex, NULL)))
		return ret;

	/*
	 * We don't own the mutex yet, so no need to validate the timeout as
	 * the mutex can be locked immediately.
	 *
	 * The second parameter of phtread_mutex_timedlock() is flagged as
	 * __nonnull so we take an invalid address instead of NULL.
	 */
	if (!__T(ret, pthread_mutex_timedlock(&mutex, (void *) 0xdeadbeef)))
		return ret;

	/*
	 * Create a second thread which will have to wait and therefore will
	 * validate the (invalid) timeout
	 */
	args.mutex = &mutex;
	ret = create_thread(&tid, SCHED_FIFO, THREAD_PRIO_LOW,
			    mutex_timed_locker_inv_timeout, &args);

	if (ret)
		return ret;

	if (!__T(ret, pthread_join(tid, &status)))
		return ret;

	if (!__T(ret, pthread_mutex_unlock(&mutex)))
		return ret;

	if (!__T(ret, pthread_mutex_destroy(&mutex)))
		return ret;

	if (!__Fassert(status == NULL))
		return -EINVAL;

	return 0;
}

/* Detect obviously wrong execution times. */
static int check_time_limit(const struct timespec *start,
			    xnticks_t limit_ns)
{
	struct timespec stop, delta;

	clock_gettime(CLOCK_MONOTONIC, &stop);
	timespec_sub(&delta, &stop, start);

	return timespec_scalar(&delta) <= limit_ns;
}

#define do_test(__fn, __limit_ns)					\
	do {								\
		struct timespec __start;				\
		int __ret;						\
		smokey_trace(".. " __stringify(__fn));			\
		clock_gettime(CLOCK_MONOTONIC, &__start);		\
		__ret = __fn();						\
		if (__ret)						\
			return __ret;					\
		if (!__Tassert(check_time_limit(&__start, __limit_ns)))	\
			return -ETIMEDOUT;				\
	} while (0)

static int run_posix_mutex(struct smokey_test *t, int argc, char *const argv[])
{
	struct sched_param param;
	struct sigaction sa;
	int ret;

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	param.sched_priority = THREAD_PRIO_MEDIUM;
	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    SCHED_FIFO, &param)))
		return ret;

	do_test(static_init_normal_destroy, MAX_100_MS);
	do_test(static_init_normal_contend, MAX_100_MS);
	do_test(dynamic_init_normal_contend, MAX_100_MS);
	do_test(static_init_recursive_destroy, MAX_100_MS);
	do_test(static_init_recursive_contend, MAX_100_MS);
	do_test(dynamic_init_recursive_contend, MAX_100_MS);
	do_test(static_init_errorcheck_destroy, MAX_100_MS);
	do_test(static_init_errorcheck_contend, MAX_100_MS);
	do_test(dynamic_init_errorcheck_contend, MAX_100_MS);
	do_test(timed_contend, MAX_100_MS);
	do_test(weak_mode_switch, MAX_100_MS);
	do_test(pi_contend, MAX_100_MS);
	do_test(steal, MAX_100_MS);
	do_test(no_steal, MAX_100_MS);
	do_test(protect_raise, MAX_100_MS);
	do_test(protect_lower, MAX_100_MS);
	do_test(protect_nesting_protect, MAX_100_MS);
	do_test(protect_nesting_pi, MAX_100_MS);
	do_test(protect_weak, MAX_100_MS);
	do_test(protect_dynamic, MAX_100_MS);
	do_test(protect_trylock, MAX_100_MS);
	do_test(protect_handover, MAX_100_MS);
	do_test(check_timedlock_abstime_validation, MAX_100_MS);

	return 0;
}

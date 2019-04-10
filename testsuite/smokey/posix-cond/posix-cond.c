/*
 * Functional testing of the condvar implementation for Cobalt.
 *
 * Copyright (C) Gilles Chanteperdrix  <gilles.chanteperdrix@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <smokey/smokey.h>

smokey_test_plugin(posix_cond,
		   SMOKEY_NOARGS,
		   "Check POSIX condition variable services"
);

#define NS_PER_MS (1000000)
#define NS_PER_S (1000000000)

static unsigned long long timer_read(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (unsigned long long)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

static inline unsigned long long timer_get_tsc(void)
{
	return clockobj_get_tsc();
}

static inline unsigned long long timer_tsc2ns(unsigned long long tsc)
{
	return clockobj_tsc_to_ns(tsc);
}

static int mutex_init(pthread_mutex_t *mutex, int type, int proto)
{
	pthread_mutexattr_t mattr;
	int err;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, type);
	err = pthread_mutexattr_setprotocol(&mattr, proto);
	if (err)
		goto out;
	if (proto == PTHREAD_PRIO_PROTECT)
		pthread_mutexattr_setprioceiling(&mattr, 3);

	err = pthread_mutex_init(mutex, &mattr);
  out:
	pthread_mutexattr_destroy(&mattr);

	return -err;
}
#define mutex_lock(mutex) (-pthread_mutex_lock(mutex))
#define mutex_unlock(mutex) (-pthread_mutex_unlock(mutex))
#define mutex_destroy(mutex) (-pthread_mutex_destroy(mutex))

static int cond_init(pthread_cond_t *cond, int absolute)
{
	pthread_condattr_t cattr;
	int ret;

	pthread_condattr_init(&cattr);
	ret = pthread_condattr_setclock(&cattr,
					absolute ? CLOCK_REALTIME : CLOCK_MONOTONIC);
	if (ret) {
		pthread_condattr_destroy(&cattr);
		return ENOSYS;
	}
	ret = pthread_cond_init(cond, &cattr);
	pthread_condattr_destroy(&cattr);

	return -ret;
}
#define cond_signal(cond) (-pthread_cond_signal(cond))

static int cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex, unsigned long long ns)
{
	struct timespec ts;

	if (ns == 0)
		return -pthread_cond_wait(cond, mutex);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ns += ts.tv_nsec;
	ts.tv_sec += ns / NS_PER_S;
	ts.tv_nsec = ns % NS_PER_S;

	return -pthread_cond_timedwait(cond, mutex, &ts);
}

static int cond_wait_until(pthread_cond_t *cond, pthread_mutex_t *mutex, unsigned long long date)
{
	struct timespec ts = {
		.tv_sec = date / NS_PER_S,
		.tv_nsec = date % NS_PER_S,
	};

	return -pthread_cond_timedwait(cond, mutex, &ts);
}
#define cond_destroy(cond) (-pthread_cond_destroy(cond))

static int thread_msleep(unsigned ms)
{
	struct timespec ts = {
		.tv_sec = (ms * NS_PER_MS) / NS_PER_S,
		.tv_nsec = (ms * NS_PER_MS) % NS_PER_S,
	};

	return -nanosleep(&ts, NULL);
}

static int thread_spawn(pthread_t *thread, int prio,
			void *(*handler)(void *cookie), void *cookie)
{
	struct sched_param param;
	pthread_attr_t tattr;
	int err;

	pthread_attr_init(&tattr);
	pthread_attr_setinheritsched(&tattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&tattr, SCHED_FIFO);
	param.sched_priority = prio;
	pthread_attr_setschedparam(&tattr, &param);
	pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);

	err = pthread_create(thread, &tattr, handler, cookie);

	pthread_attr_destroy(&tattr);

	return -err;
}
#define thread_yield() sched_yield()
#define thread_kill(thread, sig) (-__STD(pthread_kill(thread, sig)))
#define thread_self() pthread_self()
#define thread_join(thread) (-pthread_join(thread, NULL))

static void check_inner(const char *file, int line, const char *fn, const char *msg, int status, int expected)
{
	if (status == expected)
		return;

	fprintf(stderr, "FAILED %s %s: returned %d instead of %d - %s\n",
		fn, msg, status, expected, strerror(-status));
	exit(EXIT_FAILURE);
}
#define check(msg, status, expected) \
	check_inner(__FILE__, __LINE__, __func__, msg, status, expected)

#define check_unix(msg, status, expected)				\
	({								\
		int s = (status);					\
		check_inner(__FILE__, __LINE__, __func__, msg, s < 0 ? -errno : s, expected); \
	})

static void check_sleep_inner(const char *fn,
		       const char *prefix, unsigned long long start)
{
	unsigned long long diff = timer_tsc2ns(timer_get_tsc() - start);

	if (diff < 10 * NS_PER_MS) {
		fprintf(stderr, "%s waited %Ld.%03u us\n",
			prefix, diff / 1000, (unsigned)(diff % 1000));
		exit(EXIT_FAILURE);
	}
}
#define check_sleep(prefix, start) \
	check_sleep_inner(__func__, prefix, start)

struct cond_mutex {
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	pthread_t tid;
};

static void *cond_signaler(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = timer_get_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	thread_msleep(10);
	check("cond_signal", cond_signal(cm->cond), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

static void autoinit_simple_conddestroy(void)
{
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	pthread_cond_t cond2 = PTHREAD_COND_INITIALIZER;
	unsigned int invalmagic = ~0x86860505; // ~COBALT_COND_MAGIC

	memcpy((char *)&cond2 + sizeof(cond2) - sizeof(invalmagic),
		&invalmagic, sizeof(invalmagic));

	smokey_trace("%s", __func__);
	check("cond_destroy", cond_destroy(&cond), 0);
	check("cond_destroy invalid", cond_destroy(&cond2), -EINVAL);
}

static void autoinit_simple_condwait(void)
{
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	unsigned long long start;
	pthread_mutex_t mutex;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	pthread_t cond_signaler_tid;

	smokey_trace("%s", __func__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_signaler_tid, 2, cond_signaler, &cm), 0);
	thread_msleep(11);

	start = timer_get_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	thread_msleep(10);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_signaler_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void simple_condwait(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	pthread_t cond_signaler_tid;

	smokey_trace("%s", __func__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_signaler_tid, 2, cond_signaler, &cm), 0);
	thread_msleep(11);

	start = timer_get_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	thread_msleep(10);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_signaler_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void relative_condwait(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	smokey_trace("%s", __func__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);

	start = timer_get_tsc();
	check("cond_wait",
	      cond_wait(&cond, &mutex, 10 * NS_PER_MS), -ETIMEDOUT);
	check_sleep("cond_wait", start);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void autoinit_absolute_condwait(void)
{
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	unsigned long long start;
	pthread_mutex_t mutex;

	smokey_trace("%s", __func__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);

	start = timer_get_tsc();
	check("cond_wait",
	      cond_wait_until(&cond, &mutex, timer_read() + 10 * NS_PER_MS),
	      -ETIMEDOUT);
	check_sleep("cond_wait", start);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void absolute_condwait(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	smokey_trace("%s", __func__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 1), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);

	start = timer_get_tsc();
	check("cond_wait",
	      cond_wait_until(&cond, &mutex, timer_read() + 10 * NS_PER_MS),
	      -ETIMEDOUT);
	check_sleep("cond_wait", start);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void *cond_killer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = timer_get_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	thread_msleep(10);
	check("thread_kill", thread_kill(cm->tid, SIGRTMIN), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

static volatile int sig_seen;

static void sighandler(int sig)
{
	++sig_seen;
}

static void sig_norestart_condwait(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t cond_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_killer_tid, 2, cond_killer, &cm), 0);
	thread_msleep(11);

	start = timer_get_tsc();
	sig_seen = 0;
	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 1);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void sig_restart_condwait(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t cond_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_killer_tid, 2, cond_killer, &cm), 0);
	thread_msleep(11);

	start = timer_get_tsc();
	sig_seen = 0;
	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 1);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void *mutex_killer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = timer_get_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	check("cond_signal", cond_signal(cm->cond), 0);
	thread_msleep(10);
	check("thread_kill", thread_kill(cm->tid, SIGRTMIN), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

static void sig_norestart_condwait_mutex(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t mutex_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&mutex_killer_tid, 2, mutex_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = timer_get_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 1);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(mutex_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void sig_restart_condwait_mutex(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t mutex_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&mutex_killer_tid, 2, mutex_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = timer_get_tsc();

	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(mutex_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void *double_killer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = timer_get_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	check("thread_kill 1", thread_kill(cm->tid, SIGRTMIN), 0);
	thread_msleep(10);
	check("thread_kill 2", thread_kill(cm->tid, SIGRTMIN), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

static void sig_norestart_double(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t double_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = 0,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&double_killer_tid, 2, double_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = timer_get_tsc();
	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 2);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(double_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void sig_restart_double(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t double_killer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&double_killer_tid, 2, double_killer, &cm), 0);
	thread_msleep(11);

	sig_seen = 0;
	start = timer_get_tsc();

	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check_sleep("cond_wait", start);
	check("sig_seen", sig_seen, 2);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(double_killer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void *cond_destroyer(void *cookie)
{
	unsigned long long start;
	struct cond_mutex *cm = cookie;

	start = timer_get_tsc();
	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check_sleep("mutex_lock", start);
	thread_msleep(10);
	check("cond_destroy", cond_destroy(cm->cond), -EBUSY);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

static void cond_destroy_whilewait(void)
{
	unsigned long long start;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
		.tid = thread_self(),
	};
	pthread_t cond_destroyer_tid;
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);

	smokey_trace("%s", __func__);

	check_unix("sigaction", sigaction(SIGRTMIN, &sa, NULL), 0);
	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_NONE), 0);
	check("cond_init", cond_init(&cond, 0), 0);
	check("mutex_lock", mutex_lock(&mutex), 0);
	check("thread_spawn",
	      thread_spawn(&cond_destroyer_tid, 2, cond_destroyer, &cm), 0);
	thread_msleep(11);

	start = timer_get_tsc();

	check("cond_wait", cond_wait(&cond, &mutex, 10 * NS_PER_MS), -ETIMEDOUT);
	check_sleep("cond_wait", start);
	thread_msleep(10);

	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_destroyer_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

static void *cond_delayed_signaler(void *cookie)
{
	struct cond_mutex *cm = cookie;

	thread_msleep(10);

	check("mutex_lock", mutex_lock(cm->mutex), 0);
	check("cond_signal", cond_signal(cm->cond), 0);
	check("mutex_unlock", mutex_unlock(cm->mutex), 0);

	return NULL;
}

static void cond_ppmutex(void)
{
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t mutex;
	struct cond_mutex cm = {
		.mutex = &mutex,
		.cond = &cond,
	};
	pthread_t cond_signaler_tid;

	smokey_trace("%s", __func__);

	check("mutex_init", mutex_init(&mutex, PTHREAD_MUTEX_DEFAULT,
				       PTHREAD_PRIO_PROTECT), 0);
	check("thread_spawn",
	      thread_spawn(&cond_signaler_tid, 3, cond_delayed_signaler,
			   &cm), 0);

	thread_msleep(1);
	check("mutex_lock", mutex_lock(&mutex), 0);

	check("cond_wait", cond_wait(&cond, &mutex, 0), 0);
	check("mutex_unlock", mutex_unlock(&mutex), 0);
	check("thread_join", thread_join(cond_signaler_tid), 0);
	check("mutex_destroy", mutex_destroy(&mutex), 0);
	check("cond_destroy", cond_destroy(&cond), 0);
}

int run_posix_cond(struct smokey_test *t, int argc, char *const argv[])
{
	struct sched_param sparam;

	/* Set scheduling parameters for the current process */
	sparam.sched_priority = 2;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sparam);

	autoinit_simple_conddestroy();
	autoinit_simple_condwait();
	simple_condwait();
	relative_condwait();
	autoinit_absolute_condwait();
	absolute_condwait();
	sig_norestart_condwait();
	sig_restart_condwait();
	sig_norestart_condwait_mutex();
	sig_restart_condwait_mutex();
	sig_norestart_double();
	sig_restart_double();
	cond_destroy_whilewait();
	cond_ppmutex();

	return 0;
}

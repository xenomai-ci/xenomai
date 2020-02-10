/*
 * Functional testing of unwanted domain switch debugging mechanism.
 *
 * Copyright (C) Siemens AG, 2012-2019
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <asm/unistd.h>
#include <sys/cobalt.h>
#include <smokey/smokey.h>

smokey_test_plugin(sigdebug,
		   SMOKEY_ARGLIST(
			   SMOKEY_BOOL(no_watchdog),
		   ),
		   "Check SIGDEBUG reporting."
);

unsigned int expected_reason;
bool sigdebug_received;
pthread_mutex_t prio_invert;
int corectl_debug;
sem_t send_signal;
char *mem;
FILE *wd;

static void setup_checkdebug(unsigned int reason)
{
	sigdebug_received = false;
	expected_reason = reason;
}

static void check_inner(const char *fn, int line, const char *msg,
			int status, int expected)
{
	if (status == expected)
		return;

	pthread_setmode_np(PTHREAD_WARNSW, 0, NULL);
	rt_print_flush_buffers();
	fprintf(stderr, "FAILURE %s:%d: %s returned %d instead of %d - %s\n",
		fn, line, msg, status, expected, strerror(status));
	exit(EXIT_FAILURE);
}

static void check_sigdebug_inner(const char *fn, int line, const char *reason)
{
	if (sigdebug_received)
		return;

	pthread_setmode_np(PTHREAD_WARNSW, 0, NULL);
	rt_print_flush_buffers();
	fprintf(stderr, "FAILURE %s:%d: no %s received\n", fn, line, reason);
	exit(EXIT_FAILURE);
}

#define check(msg, status, expected) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg, __status, expected);	\
	__status;							\
})

#define check_no_error(msg, status) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg,				\
		    __status < 0 ? errno : __status, 0);		\
	__status;							\
})

#define check_sigdebug_received(reason) do {				\
	const char *__reason = reason;					\
	check_sigdebug_inner(__func__, __LINE__, __reason);		\
} while (0)

static void *rt_thread_body(void *cookie)
{
	struct timespec now, delay = {.tv_sec = 0, .tv_nsec = 10000000LL};
	unsigned long long end;
	int err;

	err = pthread_setname_np(pthread_self(), "test");
	check_no_error("pthread_setname_np", err);
	err = pthread_setmode_np(0, PTHREAD_WARNSW, NULL);
	check_no_error("pthread_setmode_np", err);

	smokey_trace("syscall");
	setup_checkdebug(SIGDEBUG_MIGRATE_SYSCALL);
	syscall(__NR_gettid);
	check_sigdebug_received("SIGDEBUG_MIGRATE_SYSCALL");

	smokey_trace("signal");
	setup_checkdebug(SIGDEBUG_MIGRATE_SIGNAL);
	err = sem_post(&send_signal);
	check_no_error("sem_post", err);
	err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	check("clock_nanosleep", err, EINTR);
	check_sigdebug_received("SIGDEBUG_MIGRATE_SIGNAL");

	smokey_trace("relaxed mutex owner");
	if (corectl_debug & _CC_COBALT_DEBUG_MUTEX_RELAXED) {
		setup_checkdebug(SIGDEBUG_MIGRATE_PRIOINV);
		err = pthread_mutex_lock(&prio_invert);
		check_no_error("pthread_mutex_lock", err);
		check_sigdebug_received("SIGDEBUG_MIGRATE_PRIOINV");
		err = pthread_mutex_unlock(&prio_invert);
		check_no_error("pthread_mutex_unlock", err);
	} else {
		smokey_note("sigdebug \"SIGDEBUG_MIGRATE_PRIOINV\" skipped "
			    "(no kernel support)");
	}

	smokey_trace("page fault");
	setup_checkdebug(SIGDEBUG_MIGRATE_FAULT);
	delay.tv_nsec = 0;
	err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	check_no_error("clock_nanosleep", err);
	*mem ^= 0xFF;
	check_sigdebug_received("SIGDEBUG_MIGRATE_FAULT");

	if (wd) {
		smokey_trace("watchdog");
		rt_print_flush_buffers();
		setup_checkdebug(SIGDEBUG_WATCHDOG);
		clock_gettime(CLOCK_MONOTONIC, &now);
		end = now.tv_sec * 1000000000ULL + now.tv_nsec + 2100000000ULL;
		err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
		check_no_error("clock_nanosleep", err);
		do
			clock_gettime(CLOCK_MONOTONIC, &now);
		while (now.tv_sec * 1000000000ULL + now.tv_nsec < end &&
			 !sigdebug_received);
		check_sigdebug_received("SIGDEBUG_WATCHDOG");
	} else
		smokey_note("watchdog not tested");

	smokey_trace("lock break");
	setup_checkdebug(SIGDEBUG_LOCK_BREAK);
	err = pthread_setmode_np(0, PTHREAD_LOCK_SCHED |
				    PTHREAD_DISABLE_LOCKBREAK, NULL);
	check_no_error("pthread_setmode_np", err);
	delay.tv_nsec = 1000000LL;
	err = clock_nanosleep(CLOCK_MONOTONIC, 0, &delay, NULL);
	check("clock_nanosleep", err, EINTR);
	check_sigdebug_received("SIGDEBUG_LOCK_BREAK");

	return NULL;
}

static void sigdebug_handler(int sig, siginfo_t *si, void *context)
{
	unsigned int reason = sigdebug_reason(si);

	if (reason != expected_reason) {
		rt_print_flush_buffers();
		fprintf(stderr, "FAILURE: sigdebug_handler expected reason %d,"
			" received %d\n", expected_reason, reason);
		exit(EXIT_FAILURE);
	}
	sigdebug_received = true;
}

static void dummy_handler(int sig, siginfo_t *si, void *context)
{
}

static void fault_handler(int sig)
{
	mprotect(mem, 1, PROT_WRITE);
}

static int run_sigdebug(struct smokey_test *t, int argc, char *const argv[])
{
	char tempname[] = "/tmp/sigdebug-XXXXXX";
	struct sched_param params = {.sched_priority = 1};
	pthread_t rt_thread;
	pthread_attr_t attr;
	pthread_mutexattr_t mutex_attr;
	struct timespec delay = {.tv_sec = 0, .tv_nsec = 20000000ULL};
	int err, wdog_delay, tmp_fd;
	struct sigaction sa;

	err = cobalt_corectl(_CC_COBALT_GET_WATCHDOG, &wdog_delay, sizeof(wdog_delay));
	if (err || wdog_delay == 0)
		return -ENOSYS;

	err = cobalt_corectl(_CC_COBALT_GET_DEBUG, &corectl_debug,
			     sizeof(corectl_debug));
	if (err)
		return -ENOSYS;

	smokey_parse_args(t, argc, argv);
	if (!SMOKEY_ARG_ISSET(sigdebug, no_watchdog) ||
	    !SMOKEY_ARG_BOOL(sigdebug, no_watchdog)) {
		wd = fopen("/sys/module/xenomai/parameters/watchdog_timeout",
			   "w+");
		if (wd) {
			err = fprintf(wd, "2");
			check("set watchdog", err, 1);
			fflush(wd);
		}
	}

	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);

	sa.sa_sigaction = dummy_handler;
	sigaction(SIGUSR1, &sa, NULL);

	sa.sa_handler = fault_handler;
	sigaction(SIGSEGV, &sa, NULL);

	errno = 0;
	tmp_fd = mkstemp(tempname);
	check_no_error("mkstemp", -errno);
	unlink(tempname);
	check_no_error("unlink", -errno);
	mem = mmap(NULL, 1, PROT_READ, MAP_PRIVATE, tmp_fd, 0);
	check_no_error("mmap", -errno);
	err = write(tmp_fd, "X", 1);
	check("write", err, 1);

	err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);
	check_no_error("pthread_setschedparam", err);

	err = pthread_mutexattr_init(&mutex_attr);
	check_no_error("pthread_mutexattr_init", err);
	err = pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT);
	check_no_error("pthread_mutexattr_setprotocol", err);
	err = pthread_mutex_init(&prio_invert, &mutex_attr);
	check_no_error("pthread_mutex_init", err);

	err = pthread_mutex_lock(&prio_invert);
	check_no_error("pthread_mutex_lock", err);

	err = sem_init(&send_signal, 0, 0);
	check_no_error("sem_init", err);

	err = pthread_attr_init(&attr);
	check_no_error("pthread_attr_init", err);
	err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	check_no_error("pthread_attr_setinheritsched", err);
	err = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	check_no_error("pthread_attr_setschedpolicy", err);
	params.sched_priority = 2;
	err = pthread_attr_setschedparam(&attr, &params);
	check_no_error("pthread_attr_setschedparam", err);

	smokey_trace("mlockall");
	munlockall();
	setup_checkdebug(SIGDEBUG_NOMLOCK);
	err = pthread_create(&rt_thread, &attr, rt_thread_body, NULL);
	/* Note: EINTR is against the spec, but that's OK in this scenario. */
	check("pthread_create", err, EINTR);
	check_sigdebug_received("SIGDEBUG_NOMLOCK");
	mlockall(MCL_CURRENT | MCL_FUTURE);

	err = pthread_create(&rt_thread, &attr, rt_thread_body, NULL);
	check_no_error("pthread_create", err);

	err = sem_wait(&send_signal);
	check_no_error("sem_wait", err);
	err = __STD(pthread_kill(rt_thread, SIGUSR1));
	check_no_error("pthread_kill", err);

	__STD(nanosleep(&delay, NULL));

	err = pthread_mutex_unlock(&prio_invert);
	check_no_error("pthread_mutex_unlock", err);

	err = pthread_join(rt_thread, NULL);
	check_no_error("pthread_join", err);

	err = pthread_mutex_destroy(&prio_invert);
	check_no_error("pthread_mutex_destroy", err);

	err = sem_destroy(&send_signal);
	check_no_error("sem_destroy", err);

	close(tmp_fd);
	if (wd) {
		fprintf(wd, "%d", wdog_delay);
		fclose(wd);
	}

	return 0;
}

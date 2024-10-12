/*
 * SCHED_QUOTA test.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */

#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <errno.h>
#include <error.h>
#include <sys/cobalt.h>
#include <boilerplate/time.h>
#include <boilerplate/ancillaries.h>
#include <smokey/smokey.h>

smokey_test_plugin(sched_quota,
		   SMOKEY_ARGLIST(
			   SMOKEY_INT(quota),
			   SMOKEY_INT(threads),
		   ),
   "Check the SCHED_QUOTA scheduling policy. Using a pool\n"
   "\tof SCHED_FIFO threads, the code first calibrates, by estimating how\n"
   "\tmuch work the system under test can perform when running\n"
   "\tuninterrupted over a second.\n\n"
   "\tThe same thread pool is re-started afterwards, as a SCHED_QUOTA\n"
   "\tgroup this time, which is allotted a user-definable percentage of\n"
   "\tthe global quota interval (CONFIG_XENO_OPT_SCHED_QUOTA_PERIOD).\n"
   "\tUsing the reference calibration value obtained by running the\n"
   "\tSCHED_FIFO pool, the percentage of runtime consumed by the\n"
   "\tSCHED_QUOTA group over a second is calculated.\n\n"
   "\tA successful test shows that the effective percentage of runtime\n"
   "\tobserved with the SCHED_QUOTA group closely matches the allotted\n"
   "\tquota (barring rounding errors and marginal latency)."
);

#define MAX_THREADS 8
#define TEST_SECS   1

static unsigned long long crunch_per_sec, loops_per_sec;

static pthread_t threads[MAX_THREADS];

static unsigned long counts[MAX_THREADS];

static int nrthreads;

static pthread_cond_t barrier;

static pthread_mutex_t lock;

static int started;

static sem_t ready;

static volatile int throttle;

static unsigned long __attribute__(( noinline ))
__do_work(unsigned long count)
{
	return count + 1;
}

static void __attribute__(( noinline ))
do_work(unsigned long loops, unsigned long *count_r)
{
	unsigned long n;

	for (n = 0; n < loops; n++)
		*count_r = __do_work(*count_r);
}

static void *thread_body(void *arg)
{
	unsigned long *count_r = arg, loops;
	int oldstate, oldtype;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	loops = crunch_per_sec / 100; /* yield each 10 ms runtime */
	*count_r = 0;
	sem_post(&ready);

	pthread_mutex_lock(&lock);
	for (;;) {
		if (started)
			break;
		pthread_cond_wait(&barrier, &lock);
	}
	pthread_mutex_unlock(&lock);

	for (;;) {
		do_work(loops, count_r);
		if (throttle)
			sleep(1);
		else if (nrthreads > 1)
			sched_yield();
	}

	return NULL;
}

static void __create_quota_thread(pthread_t *tid, const char *name,
				  int tgid, unsigned long *count_r)
{
	struct sched_param_ex param_ex;
	pthread_attr_ex_t attr_ex;
	int ret;

	pthread_attr_init_ex(&attr_ex);
	pthread_attr_setdetachstate_ex(&attr_ex, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched_ex(&attr_ex, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy_ex(&attr_ex, SCHED_QUOTA);
	param_ex.sched_priority = 1;
	param_ex.sched_quota_group = tgid;
	pthread_attr_setschedparam_ex(&attr_ex, &param_ex);
	ret = pthread_create_ex(tid, &attr_ex, thread_body, count_r);
	if (ret)
		error(1, ret, "pthread_create_ex(SCHED_QUOTA)");

	pthread_attr_destroy_ex(&attr_ex);
	pthread_setname_np(*tid, name);
}

#define create_quota_thread(__tid, __label, __tgid, __count)	\
	__create_quota_thread(&(__tid), __label, __tgid, &(__count))

static void __create_fifo_thread(pthread_t *tid, const char *name,
				 unsigned long *count_r)
{
	struct sched_param param;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	param.sched_priority = 1;
	pthread_attr_setschedparam(&attr, &param);
	ret = pthread_create(tid, &attr, thread_body, count_r);
	if (ret)
		error(1, ret, "pthread_create(SCHED_FIFO)");

	pthread_attr_destroy(&attr);
	pthread_setname_np(*tid, name);
}

#define create_fifo_thread(__tid, __label, __count)	\
	__create_fifo_thread(&(__tid), __label, &(__count))

static double run_quota(int quota)
{
	size_t len = sched_quota_confsz();
	unsigned long long count;
	union sched_config cf;
	struct timespec req;
	int ret, tgid, n;
	double percent;
	char label[12];

	cf.quota.op = sched_quota_add;
	cf.quota.add.pshared = 0;
	ret = sched_setconfig_np(0, SCHED_QUOTA, &cf, len);
	if (ret)
		error(1, ret, "sched_setconfig_np(add-quota-group)");

	tgid = cf.quota.info.tgid;
	cf.quota.op = sched_quota_set;
	cf.quota.set.quota = quota;
	cf.quota.set.quota_peak = quota;
	cf.quota.set.tgid = tgid;
	ret = sched_setconfig_np(0, SCHED_QUOTA, &cf, len);
	if (ret)
		error(1, ret, "sched_setconfig_np(set-quota, tgid=%d)", tgid);

	smokey_trace("new thread group #%d on CPU0, quota sum is %d%%",
		     tgid, cf.quota.info.quota_sum);

	for (n = 0; n < nrthreads; n++) {
		snprintf(label, sizeof(label), "t%d", n);
		create_quota_thread(threads[n], label, tgid, counts[n]);
		sem_wait(&ready);
	}

	pthread_mutex_lock(&lock);
	started = 1;
	pthread_cond_broadcast(&barrier);
	pthread_mutex_unlock(&lock);

	req.tv_sec = TEST_SECS;
	req.tv_nsec = 0;
	clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);

	for (n = 0, count = 0; n < nrthreads; n++)
		count += counts[n];

	percent = ((double)count / TEST_SECS) * 100.0 / loops_per_sec;

	throttle = 1;
	smp_wmb();

	for (n = 0; n < nrthreads; n++) {
		smokey_trace("done quota_thread[%d], count=%lu", n, counts[n]);
		pthread_cancel(threads[n]);
		pthread_join(threads[n], NULL);
	}

	cf.quota.op = sched_quota_remove;
	cf.quota.remove.tgid = tgid;
	ret = sched_setconfig_np(0, SCHED_QUOTA, &cf, len);
	if (ret)
		error(1, ret, "sched_setconfig_np(remove-quota-group)");

	return percent;
}

static unsigned long long calibrate(void)
{
	struct timespec start, end, delta;
	const int crunch_loops = 100000;
	unsigned long long ns, lps;
	unsigned long count;
	struct timespec req;
	char label[12];
	int n;

	count = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do_work(crunch_loops, &count);
	clock_gettime(CLOCK_MONOTONIC, &end);
	
	timespec_sub(&delta, &end, &start);
	ns = delta.tv_sec * ONE_BILLION + delta.tv_nsec;
	crunch_per_sec = (unsigned long long)((double)ONE_BILLION / (double)ns * crunch_loops);

	for (n = 0; n < nrthreads; n++) {
		snprintf(label, sizeof(label), "t%d", n);
		create_fifo_thread(threads[n], label, counts[n]);
		sem_wait(&ready);
	}

	pthread_mutex_lock(&lock);
	started = 1;
	pthread_cond_broadcast(&barrier);
	pthread_mutex_unlock(&lock);

	req.tv_sec = 1;
	req.tv_nsec = 0;
	clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);

	for (n = 0, lps = 0; n < nrthreads; n++)
		lps += counts[n];

	throttle = 1;
	smp_wmb();

	for (n = 0; n < nrthreads; n++) {
		pthread_cancel(threads[n]);
		pthread_join(threads[n], NULL);
	}

	started = 0;
	throttle = 0;

	return lps;
}

static int run_sched_quota(struct smokey_test *t, int argc, char *const argv[])
{
	pthread_t me = pthread_self();
	int ret, quota = 0, policies;
	struct sched_param param;
	cpu_set_t affinity;
	double effective;

	ret = cobalt_corectl(_CC_COBALT_GET_POLICIES, &policies, sizeof(policies));
	if (ret || (policies & _CC_COBALT_SCHED_QUOTA) == 0)
		return -ENOSYS;
	
	CPU_ZERO(&affinity);
	CPU_SET(0, &affinity);
	ret = sched_setaffinity(0, sizeof(affinity), &affinity);
	if (ret)
		error(1, errno, "sched_setaffinity");

	smokey_parse_args(t, argc, argv);
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&barrier, NULL);
	sem_init(&ready, 0, 0);

	param.sched_priority = 50;
	ret = pthread_setschedparam(me, SCHED_FIFO, &param);
	if (ret) {
		warning("pthread_setschedparam(SCHED_FIFO, 50) failed");
		return -ret;
	}

	if (SMOKEY_ARG_ISSET(sched_quota, quota))
		quota = SMOKEY_ARG_INT(sched_quota, quota);

	if (quota <= 0)
		quota = 10;

	if (SMOKEY_ARG_ISSET(sched_quota, threads))
		nrthreads = SMOKEY_ARG_INT(sched_quota, threads);

	if (nrthreads <= 0)
		nrthreads = 3;
	if (nrthreads > MAX_THREADS)
		error(1, EINVAL, "max %d threads", MAX_THREADS);

	calibrate();	/* Warming up, ignore result. */
	loops_per_sec = calibrate();

	smokey_trace("calibrating: %Lu loops/sec", loops_per_sec);

	effective = run_quota(quota);
	smokey_trace("%d thread%s: cap=%d%%, effective=%.1f%%",
		     nrthreads, nrthreads > 1 ? "s": "", quota, effective);

	if (!smokey_on_vm && fabs(effective - (double)quota) > 1.5) {
		smokey_warning("out of quota: %.1f%%",
			       effective - (double)quota);
		return -EPROTO;
	}

	return 0;
}

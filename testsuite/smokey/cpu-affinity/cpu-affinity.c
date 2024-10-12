/*
 * Test CPU affinity control mechanisms.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <semaphore.h>
#include <rtdm/testing.h>
#include <smokey/smokey.h>

smokey_test_plugin(cpu_affinity,
		   SMOKEY_NOARGS,
		   "Check CPU affinity control."
);

static cpu_set_t cpu_realtime_set, cpu_online_set;

struct test_context {
	sem_t done;
	int status;
	int kfd;
	int nrt_cpu;
};

static void *test_thread(void *arg)
{
	int cpu, cpu_in_rt_set, status = 0, ncpu, ret;
	struct test_context *p = arg;
	cpu_set_t set;

	cpu = get_current_cpu();
	if (!__Fassert(cpu < 0)) {
		status = cpu;
		goto out;
	}

	/*
	 * When emerging, we should be running on a member of the
	 * real-time CPU set.
	 */
	cpu_in_rt_set = CPU_ISSET(cpu, &cpu_realtime_set);
	if (!__Tassert(cpu_in_rt_set)) {
		status = -EINVAL;
		goto out;
	}

	smokey_trace(".... user thread starts on CPU%d, ok", cpu);

	for (ncpu = 0; ncpu < CPU_SETSIZE; ncpu++) {
		if (ncpu == cpu || !CPU_ISSET(ncpu, &cpu_realtime_set))
			continue;
		CPU_ZERO(&set);
		CPU_SET(ncpu, &set);
		if (!__Terrno(ret, sched_setaffinity(0, sizeof(set), &set))) {
			status = ret;
			goto out;
		}
		smokey_trace(".... user thread moved to CPU%d, good", ncpu);
	}
out:
	p->status = status;
	__STD(sem_post(&p->done));
	
	return NULL;
}

static int load_test_module(void)
{
	int fd, status;

	status = smokey_modprobe("xeno_rtdmtest", true);
	if (status)
		return status;

	/* Open the RTDM actor device. */
	fd = open("/dev/rtdm/rtdmx", O_RDWR);
	if (fd < 0)
		return -errno;

	return fd;
}

static void unload_test_module(int fd)
{
	close(fd);
	smokey_rmmod("xeno_rtdmtest");
}

static void *__run_cpu_affinity(void *arg)
{
	struct test_context *context = arg;
	struct sched_param param;
	struct timespec ts, now;
	pthread_attr_t thattr;
	cpu_set_t set;
	pthread_t tid;
	int ret;

	smokey_trace(".. control thread binding to non-RT CPU%d",
		     context->nrt_cpu);

	__STD(sem_init(&context->done, 0, 0));

	/*
	 * Make the child thread inherit a CPU affinity outside of the
	 * valid RT set from us. Cobalt should migrate the spawned
	 * threads (kernel and user) to a CPU from the RT set
	 * automatically.
	 */
	CPU_ZERO(&set);
	CPU_SET(context->nrt_cpu, &set);
	if (!__Terrno(ret, sched_setaffinity(0, sizeof(set), &set))) {
		context->status = ret;
		goto out;
	}

	/* Check CPU affinity handling for user-space threads. */

	smokey_trace(".. starting user thread");

	pthread_attr_init(&thattr);
	param.sched_priority = 1;
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setschedpolicy(&thattr, SCHED_FIFO);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);

	if (!__T(ret, pthread_create(&tid, &thattr, test_thread, context))) {
		context->status = ret;
		goto out;
	}

	__STD(clock_gettime(CLOCK_REALTIME, &now));
	timespec_adds(&ts, &now, 100000000); /* 100ms from now */
	
	if (!__Terrno(ret, __STD(sem_timedwait(&context->done, &ts)))) {
		context->status = ret;
		goto out;
	}

	/*
	 *  Prepare for testing CPU affinity handling for RTDM driver
	 *  tasks. We don't actually run the test just yet, since we
	 *  have no real-time context and the RTDM actor wants one,
	 *  but we still load the module, creating the actor task over
	 *  a non-RT CPU, which is the premise of our kernel-based
	 *  test.
	 */

	context->kfd = load_test_module();
out:
	__STD(sem_destroy(&context->done));
	
	return NULL;
}

static int run_cpu_affinity(struct smokey_test *t,
			    int argc, char *const argv[])
{
	struct test_context context;
	int cpu, ret, cpu_in_rt_set;
	struct sched_param param;
	pthread_attr_t thattr;
	pthread_t tid;
	__u32 kcpu;

	if (sysconf(_SC_NPROCESSORS_CONF) == 1) {
		smokey_trace("uniprocessor system, skipped");
		return 0;
	}

	ret = get_realtime_cpu_set(&cpu_realtime_set);
	if (ret)
		return -ENOSYS;

	ret = get_online_cpu_set(&cpu_online_set);
	if (ret)
		return -ENOSYS;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		if (CPU_ISSET(cpu, &cpu_online_set))
			smokey_trace(".. CPU%d is %s", cpu,
			     CPU_ISSET(cpu, &cpu_realtime_set) ?
			     "available" : "online, non-RT");
	}
			
	/* Find a non-RT CPU in the online set. */
	for (cpu = CPU_SETSIZE - 1; cpu >= 0; cpu--) {
		if (CPU_ISSET(cpu, &cpu_online_set) &&
		    !CPU_ISSET(cpu, &cpu_realtime_set))
			break;
	}

	/*
	 * If there is no CPU restriction on the bootargs
	 * (i.e. xenomai.supported_cpus is absent or does not exclude
	 * any online CPU), pretend that we have no kernel support for
	 * running this test.
	 */
	if (cpu < 0) {
		smokey_trace("no CPU restriction with xenomai.supported_cpus");
		return -ENOSYS;
	}

	pthread_attr_init(&thattr);
	param.sched_priority = 0;
	pthread_attr_setdetachstate(&thattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setschedpolicy(&thattr, SCHED_OTHER);
	pthread_attr_setschedparam(&thattr, &param);
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);

	context.kfd = -1;
	context.status = 0;
	context.nrt_cpu = cpu;
	/*
	 * Start a regular pthread for running the tests, to bypass
	 * sanity checks Cobalt does on CPU affinity. We actually want
	 * to start testing from a non-RT CPU.
	 */
	if (!__T(ret, __STD(pthread_create(&tid, &thattr,
					   __run_cpu_affinity, &context))))
		return ret;

	if (!__T(ret, pthread_join(tid, NULL)))
		return ret;

	if (context.kfd < 0)
		smokey_trace(".. RTDM test module not available, skipping");
	else {
		smokey_trace(".. testing kthread affinity handling");
		if (!__Terrno(ret, ioctl(context.kfd,
				 RTTST_RTIOC_RTDM_ACTOR_GET_CPU, &kcpu)))
			context.status = ret;
		else {
			cpu_in_rt_set = CPU_ISSET(kcpu, &cpu_realtime_set);
			if (!__Tassert(cpu_in_rt_set))
				context.status = -EINVAL;
			else
				smokey_trace(".... kernel thread pinned to CPU%d, fine",
					     kcpu);
		}
		unload_test_module(context.kfd);
	}

	return context.status;
}

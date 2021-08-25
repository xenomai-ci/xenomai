/*
 * Copyright (C) 2012 Gilles Chanteperdrix <gch@xenomai.org>
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
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/unistd.h>
#include <boilerplate/compiler.h>
#include <rtdm/rtdm.h>
#include <cobalt/uapi/kernel/heap.h>
#include <xeno_config.h>
#include <smokey/smokey.h>

smokey_test_plugin(leaks,
		   SMOKEY_NOARGS,
		   "Check for resource leakage in the Cobalt core."
);

#define SEM_NAME "/sem"
#define MQ_NAME "/mq"

#define check_used(object, before, failed)				\
	({								\
		unsigned long long after = get_used();			\
		if (before != after) {					\
			smokey_warning(object				\
				       " leaked %Lu bytes", after-before); \
			failed = 1;					\
		} else							\
			smokey_trace("no leak with" object);		\
	})

#define devnode_root "/dev/rtdm/"

const char *memdev[] = {
	devnode_root COBALT_MEMDEV_PRIVATE,
	devnode_root COBALT_MEMDEV_SHARED,
	devnode_root COBALT_MEMDEV_SYS,
	NULL,
};

static int memdevfd[3];

static unsigned long long get_used(void)
{
	struct cobalt_memdev_stat statbuf;
	unsigned long long used = 0;
	int i, ret;

	for (i = 0; memdev[i]; i++) {
		ret = smokey_check_errno(ioctl(memdevfd[i], MEMDEV_RTIOC_STAT, &statbuf));
		if (ret == 0)
			used += statbuf.size - statbuf.free;
	}

	return used;
}

static void *empty(void *cookie)
{
	return cookie;
}

static inline int subprocess_leak(void)
{
	struct sigevent sevt;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t thread;
	timer_t tm;
	sem_t sem;
	int ret;

	ret = smokey_check_status(pthread_create(&thread, NULL, empty, NULL));
	if (ret)
		return ret;
	
	ret = smokey_check_status(pthread_mutex_init(&mutex, NULL));
	if (ret)
		return ret;
	
	ret = smokey_check_status(pthread_cond_init(&cond, NULL));
	if (ret)
		return ret;

	ret = smokey_check_errno(sem_init(&sem, 0, 0));
	if (ret)
		return ret;

	ret = smokey_check_errno(-!(sem_open(SEM_NAME, O_CREAT, 0644, 1)));
	if (ret)
		return ret;

	sevt.sigev_notify = SIGEV_THREAD_ID;
	sevt.sigev_signo = SIGALRM;
	sevt.sigev_notify_thread_id = syscall(__NR_gettid);
	ret = smokey_check_errno(timer_create(CLOCK_MONOTONIC, &sevt, &tm));
	if (ret)
		return ret;

	ret = smokey_check_errno(mq_open(MQ_NAME, O_RDWR | O_CREAT, 0644, NULL));
	if (ret < 0)
		return ret;

	return 0;
}

static int run_leaks(struct smokey_test *t, int argc, char *const argv[])
{
	unsigned long long before;
	struct sigevent sevt;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int fd, failed = 0, i, ret, child_ret;
	pthread_t thread;
	sem_t sem, *psem;
	timer_t tm;
	__maybe_unused pid_t child;

	for (i = 0; memdev[i]; i++) {
		memdevfd[i] = smokey_check_errno(open(memdev[i], O_RDONLY));
		if (memdevfd[i] < 0)
			return memdevfd[i];
	}
	
	before = get_used();
	ret = smokey_check_status(pthread_create(&thread, NULL, empty, NULL));
	if (ret)
		return ret;
	
	ret = smokey_check_status(pthread_join(thread, NULL));
	if (ret)
		return ret;

	sleep(1);		/* Leave some time for xnheap
				 * deferred free */
	check_used("thread", before, failed);
	before = get_used();
	ret = smokey_check_status(pthread_mutex_init(&mutex, NULL));
	if (ret)
		return ret;
	ret = smokey_check_status(pthread_mutex_destroy(&mutex));
	if (ret)
		return ret;

	check_used("mutex", before, failed);
	before = get_used();
	ret = smokey_check_status(pthread_cond_init(&cond, NULL));
	if (ret)
		return ret;
	ret = smokey_check_status(pthread_cond_destroy(&cond));
	if (ret)
		return ret;

	check_used("cond", before, failed);
	before = get_used();
	ret = smokey_check_errno(sem_init(&sem, 0, 0));
	if (ret)
		return ret;
	ret = smokey_check_errno(sem_destroy(&sem));
	if (ret)
		return ret;

	check_used("sem", before, failed);
	before = get_used();
	ret = smokey_check_errno(-!(psem = sem_open(SEM_NAME, O_CREAT, 0644, 1)));
	if (ret)
		return ret;
	ret = smokey_check_errno(sem_close(psem));
	if (ret)
		return ret;
	ret = smokey_check_errno(sem_unlink(SEM_NAME));
	if (ret)
		return ret;

	check_used("named sem", before, failed);
	before = get_used();
	sevt.sigev_notify = SIGEV_THREAD_ID;
	sevt.sigev_signo = SIGALRM;
	sevt.sigev_notify_thread_id = syscall(__NR_gettid);
	ret = smokey_check_errno(timer_create(CLOCK_MONOTONIC, &sevt, &tm));
	if (ret)
		return ret;
	ret = smokey_check_errno(timer_delete(tm));
	if (ret)
		return ret;

	check_used("timer", before, failed);
	before = get_used();
	fd = smokey_check_errno(mq_open(MQ_NAME, O_RDWR | O_CREAT, 0644, NULL));
	if (fd < 0)
		return fd;
	ret = smokey_check_errno(mq_close(fd));
	if (ret)
		return ret;
	ret = smokey_check_errno(mq_unlink(MQ_NAME));
	if (ret)
		return ret;

	check_used("mq", before, failed);
#ifdef HAVE_FORK
	before = get_used();
	child = smokey_check_errno(fork());
	if (child < 0)
		return child;
	if (!child)
		exit(-subprocess_leak());
	while (waitpid(child, &child_ret, 0) == -1 && errno == EINTR);
	sleep(1);

	ret = smokey_check_errno(sem_unlink(SEM_NAME));
	if (ret)
		return ret;
	ret = smokey_check_errno(mq_unlink(MQ_NAME));
	if (ret)
		return ret;
	if (WIFEXITED(child_ret) && WEXITSTATUS(child_ret))
		return -WEXITSTATUS(child_ret);
	check_used("fork", before, failed);
#endif

	for (i = 0; memdev[i]; i++)
		close(memdevfd[i]);

	return failed ? -EINVAL : 0;
}

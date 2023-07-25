// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>

static struct traceobj trobj;

static RT_TASK t_test;

#define ONE_SECOND  1000000000ULL

static void sighandler(int sig)
{
	/* nop */
}

static void test_task(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	ret = rt_task_sleep_until(TM_INFINITE);
	traceobj_check(&trobj, ret, -EINTR);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	struct sigaction sa;
	RT_TASK_INFO info;
	sigset_t set;
	int ret;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGUSR1, &sa, NULL);

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_create(&t_test, "test_task", 0, 10, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_inquire(&t_test, &info);
	traceobj_check(&trobj, ret, 0);

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	ret = rt_task_start(&t_test, test_task, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_sleep(ONE_SECOND);
	traceobj_check(&trobj, ret, 0);

	ret = __STD(kill(info.pid, SIGUSR1));
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_sleep(ONE_SECOND);
	traceobj_check(&trobj, ret, 0);
	ret = rt_task_unblock(&t_test);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

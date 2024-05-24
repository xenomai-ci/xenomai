// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/mutex.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9,
};

static RT_TASK t_a, t_b, t_c;

static RT_MUTEX mutex;

static void task_a(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 3);

	ret = rt_mutex_acquire(&mutex, 1000000000ULL);
	traceobj_check(&trobj, ret, -EINTR);

	traceobj_mark(&trobj, 6);

	traceobj_exit(&trobj);
}

static void task_c(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 4);

	ret = rt_mutex_acquire(&mutex, TM_INFINITE);
	traceobj_check(&trobj, ret, -EINTR);

	traceobj_mark(&trobj, 8);

	traceobj_exit(&trobj);
}

static void task_b(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	ret = rt_mutex_create(&mutex, "MUTEX");
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 1);

	ret = rt_mutex_acquire(&mutex, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 2);

	traceobj_exit(&trobj);
	rt_task_suspend(rt_task_self());
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_create(&t_b, "taskB", 0, 21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_b, task_b, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_a, "taskA", 0, 20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_a, task_a, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_c, "taskC", 0, 20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_c, task_c, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 5);

	ret = rt_task_unblock(&t_a);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	ret = rt_task_unblock(&t_c);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 9);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

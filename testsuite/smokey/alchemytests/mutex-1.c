// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/mutex.h>

static struct traceobj trobj;

static int tseq[] = {
	6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	18, 1, 2, 3, 19, 4, 5, 16, 6, 17
};

static RT_TASK t_a, t_b;

static RT_MUTEX mutex;

static void task_a(void *arg)
{
	RT_TASK t;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = rt_task_bind(&t, "taskB", TM_INFINITE);
	traceobj_assert(&trobj, ret == 0 && rt_task_same(&t, &t_b));

	traceobj_mark(&trobj, 2);

	ret = rt_mutex_acquire(&mutex, TM_NONBLOCK);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);

	traceobj_mark(&trobj, 3);

	ret = rt_mutex_acquire(&mutex, 100000000ULL);
	traceobj_check(&trobj, ret, -ETIMEDOUT);

	traceobj_mark(&trobj, 4);

	ret = rt_task_resume(&t);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 5);

	ret = rt_mutex_acquire(&mutex, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 6);

	traceobj_exit(&trobj);
}

static void task_b(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 6);

	ret = rt_task_set_priority(NULL, 19);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	ret = rt_mutex_create(&mutex, "MUTEX");
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 8);

	ret = rt_mutex_create(&mutex, "MUTEX");
	traceobj_check(&trobj, ret, -EEXIST);

	traceobj_mark(&trobj, 9);

	ret = rt_mutex_acquire(&mutex, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 10);

	ret = rt_mutex_acquire(&mutex, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 11);

	ret = rt_mutex_release(&mutex);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 12);

	ret = rt_mutex_release(&mutex);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 13);

	ret = rt_mutex_release(&mutex);
	traceobj_check(&trobj, ret, -EPERM);

	traceobj_mark(&trobj, 14);

	ret = rt_mutex_acquire(&mutex, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 15);

	ret = rt_task_suspend(rt_task_self());
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 16);

	ret = rt_mutex_release(&mutex);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 17);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_create(&t_b, "taskB", 0, 21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_b, task_b, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 18);

	ret = rt_task_create(&t_a, "taskA", 0, 20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_a, task_a, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 19);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

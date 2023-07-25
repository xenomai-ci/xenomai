// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static int tseq[] = {
	10, 13,
	1, 14, 15, 2, 3, 4,
	5, 6, 7, 8, 16, 17, 18,
	9, 19
};

static RT_TASK t_a, t_b;

static RT_SEM sem;

static void task_a(void *arg)
{
	int ret, oldmode;
	RT_TASK t;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 2);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 3);

	ret = rt_task_set_mode(T_LOCK, T_LOCK, &oldmode);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 4);

	ret = rt_task_bind(&t, "taskB", TM_INFINITE);
	traceobj_assert(&trobj, ret == 0 && rt_task_same(&t, &t_b));

	traceobj_mark(&trobj, 5);

	ret = rt_task_resume(&t);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 6);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 8);

	ret = rt_task_set_mode(T_LOCK, 0, &oldmode);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 9);

	ret = rt_task_suspend(NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_exit(&trobj);
}

static void task_b(void *arg)
{
	RT_TASK t;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 10);

	ret = rt_sem_create(&sem, "SEMA", 0, S_FIFO);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 13);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_bind(&t, "taskA", TM_INFINITE);
	traceobj_assert(&trobj, ret == 0 && rt_task_same(&t, &t_a));

	traceobj_mark(&trobj, 14);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 15);

	ret = rt_task_suspend(NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 16);

	ret = rt_sem_p(&sem, 10000000ULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 17);

	ret = rt_sem_p(&sem, TM_NONBLOCK);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 18);

	ret = rt_sem_p(&sem, 100000000ULL);
	traceobj_check(&trobj, ret, -ETIMEDOUT);

	traceobj_mark(&trobj, 19);

	ret = rt_task_resume(&t);
	traceobj_check(&trobj, ret, 0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_create(&t_a, "taskA", 0, 20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_b, "taskB", 0, 21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_b, task_b, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_a, task_a, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

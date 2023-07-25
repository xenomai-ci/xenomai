// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static int tseq[] = {
	8, 1, 9, 4, 10, 2, 11, 12, 3, 5, 13
};

static RT_TASK t_bgnd, t_fgnd;

static RT_SEM sem;

static void background_task(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 2);

	ret = rt_task_suspend(&t_fgnd);
	traceobj_check(&trobj, ret, 0);

	rt_task_sleep(20000000ULL);

	traceobj_mark(&trobj, 3);

	ret = rt_task_resume(&t_fgnd);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 13);

	traceobj_exit(&trobj);
}

static void foreground_task(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 4);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 5);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_sem_create(&sem, "SEMA", 0, S_FIFO);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 8);

	ret = rt_task_create(&t_bgnd, "BGND", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_bgnd, background_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 9);

	ret = rt_task_create(&t_fgnd, "FGND", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_fgnd, foreground_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 10);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 11);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 12);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

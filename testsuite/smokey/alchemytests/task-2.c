// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static int tseq[] = {
	8, 1, 9, 4, 10, 5, 11, 2, 6, 7
};

static RT_TASK t_bgnd, t_fgnd;

static RT_SEM sem;

static void background_task(void *arg)
{
	unsigned int safety = 100000000, count = 0;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 2);

	while (--safety > 0)
		count++;

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

	rt_task_sleep(20000000ULL);

	traceobj_mark(&trobj, 6);

	ret = rt_task_delete(&t_bgnd);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_sem_create(&sem, "SEMA", 0, S_PRIO);
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

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

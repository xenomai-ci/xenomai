// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static int tseq[] = {
	9, 1, 10, 3, 11, 4, 5, 6, 7, 2, 8, 12
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

	traceobj_exit(&trobj);
}

static void foreground_task(void *arg)
{
	RT_TASK_INFO info;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 3);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 4);

	ret = rt_task_inquire(NULL, &info);
	traceobj_assert(&trobj, ret == 0 && info.prio == 21);

	traceobj_mark(&trobj, 5);

	ret = rt_task_set_priority(&t_bgnd, info.prio);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 6);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	ret = rt_task_set_priority(&t_bgnd, info.prio + 1);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 8);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_sem_create(&sem, "SEMA", 0, S_PRIO);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 9);

	ret = rt_task_create(&t_bgnd, "BGND", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_bgnd, background_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 10);

	ret = rt_task_create(&t_fgnd, "FGND", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_fgnd, foreground_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 11);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 12);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

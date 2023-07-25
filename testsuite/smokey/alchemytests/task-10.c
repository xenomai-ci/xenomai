// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 6, 2, 3, 4, 7, 5
};

static RT_TASK t_test;

static RT_SEM sem;

static void test_task(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 6);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_sem_create(&sem, "SEMA", 0, S_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_test, "test_task", 0, 10, 0);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 1);

	ret = rt_task_start(&t_test, test_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 2);

	ret = rt_task_suspend(&t_test);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 3);

	ret = rt_sem_v(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 4);

	ret = rt_task_resume(&t_test);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 5);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

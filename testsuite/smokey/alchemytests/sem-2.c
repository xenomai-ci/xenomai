// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 3, 5, 4, 6
};

static RT_TASK t_main;

static RT_SEM sem;

static void main_task(void *arg)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = rt_sem_create(&sem, "SEMA", 1, S_FIFO);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 2);

	ret = rt_sem_p(&sem, TM_NONBLOCK);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 3);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, -EIDRM);

	traceobj_mark(&trobj, 4);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_create(&t_main, "main_task", 0, 20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_main, main_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 5);

	ret = rt_sem_delete(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 6);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

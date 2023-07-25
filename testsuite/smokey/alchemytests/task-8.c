// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/sem.h>

static struct traceobj trobj;

static RT_TASK t_rr1, t_rr2;

static RT_SEM sem;

#define RR_QUANTUM  500000ULL

double d = 0.7;

double f = 1.7;

static void rr_task(void *arg)
{
	int ret, n;

	traceobj_enter(&trobj);

	ret = rt_task_slice(NULL, RR_QUANTUM);
	traceobj_check(&trobj, ret, 0);

	ret = rt_sem_p(&sem, TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	for (n = 0; n < 1000000; n++) {
		d *= 0.99;
		f = d / 16;
	}

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_sem_create(&sem, "SEMA", 0, S_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_rr1, "rr_task_1", 0, 10, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_rr1, rr_task, "t1");
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_rr2, "rr_task_2", 0, 10, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_rr2, rr_task, "t2");
	traceobj_check(&trobj, ret, 0);

	ret = rt_sem_broadcast(&sem);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

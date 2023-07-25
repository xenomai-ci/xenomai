// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	10, 13,
	1, 14, 15, 2, 3, 4,
	5, 6, 7, 8, 16, 17, 18,
	9, 19
};

static u_long tidA, tidB, sem_id;

static void task_A(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long tid, oldmode;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = sm_v(sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = sm_p(sem_id, SM_WAIT, 0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 3);

	ret = t_mode(T_NOPREEMPT, T_NOPREEMPT, &oldmode);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 4);

	ret = t_ident("TSKB", 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS && tid == tidB);

	traceobj_mark(&trobj, 5);

	ret = t_resume(tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 6);

	ret = sm_v(sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 7);

	ret = sm_v(sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 8);

	ret = t_mode(T_NOPREEMPT, 0, &oldmode);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 9);

	ret = t_suspend(0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_exit(&trobj);
}

static void task_B(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long tid;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 10);

	ret = sm_create("SEM", 0, SM_FIFO, &sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 13);

	ret = sm_p(sem_id, SM_WAIT, 0);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_ident("TSKA", 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS && tid == tidA);

	traceobj_mark(&trobj, 14);

	ret = sm_v(sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 15);

	ret = t_suspend(0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 16);

	ret = sm_p(sem_id, SM_WAIT, 10);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 17);

	ret = sm_p(sem_id, SM_NOWAIT, 0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 18);

	ret = sm_p(sem_id, SM_WAIT, 100);
	traceobj_assert(&trobj, ret == ERR_TIMEOUT);

	traceobj_mark(&trobj, 19);

	ret = t_resume(tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = t_create("TSKA", 20, 0, 0, 0, &tidA);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKB", 21, 0, 0, 0, &tidB);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidB, 0, task_B, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidA, 0, task_A, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

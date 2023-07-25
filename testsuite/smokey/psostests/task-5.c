// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	8, 1, 9, 4, 10, 2, 11, 12, 3, 5, 13
};

static u_long btid, ftid;

static u_long sem_id;

static void backgroundTask(u_long a1, u_long a2, u_long a3, u_long a4)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = sm_p(sem_id, SM_WAIT, 0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = t_suspend(ftid);
	traceobj_assert(&trobj, ret == SUCCESS);

	tm_wkafter(2);

	traceobj_mark(&trobj, 3);

	ret = t_resume(ftid);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 13);

	traceobj_exit(&trobj);
}

static void foregroundTask(u_long a1, u_long a2, u_long a3, u_long a4)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 4);

	ret = sm_p(sem_id, SM_WAIT, 0);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 5);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = sm_create("SEMA", 0, SM_FIFO, &sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 8);

	ret = t_create("BGND", 20, 0, 0, 0, &btid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(btid, 0, backgroundTask, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 9);

	ret = t_create("FGND", 21, 0, 0, 0, &ftid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(ftid, 0, foregroundTask, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 10);

	ret = sm_v(sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 11);

	ret = sm_v(sem_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 12);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

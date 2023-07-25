// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 10, 2, 3, 20, 4
};

static u_long tid1, tid2;

static void task1(u_long a1, u_long a2, u_long a3, u_long a4)
{
	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 10);

	traceobj_exit(&trobj);
}

static void task2(u_long a1, u_long a2, u_long a3, u_long a4)
{
	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 20);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = t_create("DUP", 20, 0, 0, 0, &tid1);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 1);

	ret = t_start(tid1, 0, task1, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = t_create("DUP", 20, 0, 0, 0, &tid2);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 3);

	ret = t_start(tid2, 0, task2, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 4);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

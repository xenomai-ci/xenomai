// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <xeno_config.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 3, 4, 5, 6, 7
};

static u_long tid1, tid2;

static u_long count1, count2;

static void do_work(u_long *counter, int mark)
{
	traceobj_mark(&trobj, mark);
	tm_wkafter(2);

	for (;;)
		(*counter)++;
}

static void task1(u_long a1, u_long a2, u_long a3, u_long a4)
{
	traceobj_enter(&trobj);
	do_work(&count1, 4);
	traceobj_exit(&trobj);
}

static void task2(u_long a1, u_long a2, u_long a3, u_long a4)
{
	traceobj_enter(&trobj);
	do_work(&count2, 5);
	traceobj_exit(&trobj);
}

static void main_task(u_long a1, u_long a2, u_long a3, u_long a4)
{
	u_long args[] = { 1, 2, 3, 4 }, old;
	int ret;

	traceobj_mark(&trobj, 1);

	ret = t_create("T1", 10, 0, 0, 0, &tid1);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("T2", 10, 0, 0, 0, &tid2);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = t_start(tid1, T_TSLICE, task1, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid2, T_TSLICE, task2, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 3);

	ret = t_mode(T_NOPREEMPT, 0, &old);
	traceobj_assert(&trobj, ret == SUCCESS);

	tm_wkafter(2000);

	traceobj_mark(&trobj, 6);

	t_delete(tid1);
	t_delete(tid2);
	traceobj_mark(&trobj, 7);
}

#ifdef CONFIG_XENO_DEBUG_FULL
#define threshold_quantum 50
#else
#define threshold_quantum 1000
#endif

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 }, tid, delta, max;
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = t_create("MAIN", 50, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, T_NOPREEMPT, main_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	if (count1 < count2) {
		delta = count2 - count1;
		max = count2;
	} else {
		delta = count1 - count2;
		max = count1;
	}
	traceobj_assert(&trobj, delta < max / threshold_quantum);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

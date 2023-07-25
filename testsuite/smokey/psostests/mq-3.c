// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 4, 7, 2, 5, 7, 2, 5, 7, 2, 3, 5, 6, 8
};

static u_long tidA, tidB, qid;

static void task_A(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long msgbuf[4];
	int ret, n;

	traceobj_enter(&trobj);

	traceobj_assert(&trobj, a0 == 1);
	traceobj_assert(&trobj, a1 == 2);
	traceobj_assert(&trobj, a2 == 3);
	traceobj_assert(&trobj, a3 == 4);

	traceobj_mark(&trobj, 1);

	for (n = 0; n < 3; n++) {
		ret = q_receive(qid, Q_WAIT, 10, msgbuf);
		traceobj_mark(&trobj, 2);
		traceobj_assert(&trobj, ret == SUCCESS);
		traceobj_assert(&trobj, msgbuf[0] == n + 1);
		traceobj_assert(&trobj, msgbuf[1] == n + 2);
		traceobj_assert(&trobj, msgbuf[2] == n + 3);
		traceobj_assert(&trobj, msgbuf[3] == n + 4);
	}

	traceobj_mark(&trobj, 3);

	traceobj_exit(&trobj);
}

static void task_B(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long msgbuf[4];
	int ret, n;

	traceobj_enter(&trobj);

	traceobj_assert(&trobj, a0 == 1);
	traceobj_assert(&trobj, a1 == 2);
	traceobj_assert(&trobj, a2 == 3);
	traceobj_assert(&trobj, a3 == 4);

	traceobj_mark(&trobj, 4);

	for (n = 0; n < 3; n++) {
		ret = q_receive(qid, Q_WAIT, 10, msgbuf);
		traceobj_mark(&trobj, 5);
		traceobj_assert(&trobj, ret == SUCCESS);
		traceobj_assert(&trobj, msgbuf[0] == n + 1);
		traceobj_assert(&trobj, msgbuf[1] == n + 2);
		traceobj_assert(&trobj, msgbuf[2] == n + 3);
		traceobj_assert(&trobj, msgbuf[3] == n + 4);
	}

	traceobj_mark(&trobj, 6);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 }, msgbuf[4], count;
	int ret, n;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = q_create("QUEUE", Q_NOLIMIT, 0, &qid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKA", 21, 0, 0, 0, &tidA);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKB", 20, 0, 0, 0, &tidB);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidA, 0, task_A, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidB, 0, task_B, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	for (n = 0; n < 3; n++) {
		msgbuf[0] = n + 1;
		msgbuf[1] = n + 2;
		msgbuf[2] = n + 3;
		msgbuf[3] = n + 4;
		count = 0;
		traceobj_mark(&trobj, 7);
		ret = q_broadcast(qid, msgbuf, &count);
		traceobj_assert(&trobj, ret == SUCCESS && count == 2);
	}

	traceobj_mark(&trobj, 8);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	ret = q_delete(qid);
	traceobj_assert(&trobj, ret == SUCCESS);

	exit(0);
}

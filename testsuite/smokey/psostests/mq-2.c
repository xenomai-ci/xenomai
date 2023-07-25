// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 7, 8, 9, 10, 11, 3, 4, 5, 6
};

static u_long tidA, tidB, qid;

static void task_A(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long msgbuf[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_enter(&trobj);

	traceobj_assert(&trobj, a0 == 1);
	traceobj_assert(&trobj, a1 == 2);
	traceobj_assert(&trobj, a2 == 3);
	traceobj_assert(&trobj, a3 == 4);

	traceobj_mark(&trobj, 7);

	ret = q_send(qid, msgbuf);
	traceobj_assert(&trobj, ret == ERR_VARQ);
	traceobj_mark(&trobj, 8);

	ret = q_vsend(qid, msgbuf, sizeof(u_long[4]));
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_mark(&trobj, 9);

	msgbuf[0]++;
	ret = q_vsend(qid, msgbuf, sizeof(u_long[4]));
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_mark(&trobj, 10);

	msgbuf[0]++;
	ret = q_vsend(qid, msgbuf, sizeof(u_long[4]));
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_mark(&trobj, 11);

	traceobj_exit(&trobj);
}

static void task_B(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long _msgbuf[8], msglen;
	int ret;

	traceobj_enter(&trobj);

	traceobj_assert(&trobj, a0 == 1);
	traceobj_assert(&trobj, a1 == 2);
	traceobj_assert(&trobj, a2 == 3);
	traceobj_assert(&trobj, a3 == 4);

	traceobj_mark(&trobj, 1);

	msglen = 0;
	ret = q_vreceive(qid, Q_NOWAIT, 0, _msgbuf, sizeof(u_long[8]), &msglen);
	traceobj_assert(&trobj, ret == ERR_NOMSG);
	traceobj_mark(&trobj, 2);

	ret = q_vreceive(qid, Q_WAIT, 0, _msgbuf, sizeof(u_long[8]), &msglen);
	traceobj_assert(&trobj, ret == SUCCESS && msglen == sizeof(u_long[4]));
	traceobj_assert(&trobj, _msgbuf[0] == 1);
	traceobj_mark(&trobj, 3);

	ret = q_vreceive(qid, Q_WAIT, 0, _msgbuf, sizeof(u_long[8]), &msglen);
	traceobj_assert(&trobj, ret == SUCCESS && msglen == sizeof(u_long[4]));
	traceobj_assert(&trobj, _msgbuf[0] == 2);
	traceobj_mark(&trobj, 4);

	ret = q_vreceive(qid, Q_WAIT, 10, _msgbuf, sizeof(u_long[8]), &msglen);
	traceobj_assert(&trobj, ret == SUCCESS && msglen == sizeof(u_long[4]));
	traceobj_assert(&trobj, _msgbuf[0] == 3);
	traceobj_mark(&trobj, 5);

	ret = q_vreceive(qid, Q_WAIT, 10, _msgbuf, sizeof(u_long[8]), &msglen);
	traceobj_assert(&trobj, ret == ERR_TIMEOUT);
	traceobj_mark(&trobj, 6);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = q_vcreate("VQUEUE", Q_LIMIT, 3, sizeof(u_long[4]), &qid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKA", 21, 0, 0, 0, &tidA);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKB", 20, 0, 0, 0, &tidB);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidB, 0, task_B, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tidA, 0, task_A, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	ret = q_delete(qid);
	traceobj_assert(&trobj, ret == ERR_VARQ);

	ret = q_vdelete(qid);
	traceobj_assert(&trobj, ret == SUCCESS);

	exit(0);
}

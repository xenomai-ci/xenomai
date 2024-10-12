// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/msgQLib.h>

#define NMESSAGES  10

static struct traceobj trobj;

static int tseq[] = {
	11, 1, 2, 3, 12, 8,
	4, 5, 6, 9, 7, 10, 13
};

static MSG_Q_ID qid;

static void rootTask(long arg, ...)
{
	int ret, msg, n;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	qid = msgQCreate(NMESSAGES, sizeof(msg), MSG_Q_FIFO);
	traceobj_assert(&trobj, qid != 0);

	traceobj_mark(&trobj, 2);

	for (msg = 0; msg < NMESSAGES; msg++) {
		ret = msgQSend(qid, (char *)&msg, sizeof(msg), NO_WAIT, MSG_PRI_NORMAL);
		traceobj_assert(&trobj, ret == OK);
	}

	traceobj_mark(&trobj, 3);

	ret = msgQSend(qid, (char *)&msg, sizeof(msg), WAIT_FOREVER, MSG_PRI_URGENT);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 4);

	ret = msgQReceive(qid, (char *)&msg, sizeof(int), WAIT_FOREVER);
	traceobj_assert(&trobj, ret == sizeof(int) && msg == 10);

	traceobj_mark(&trobj, 5);

	for (n = 1; n < NMESSAGES; n++) { /* peer task read #0 already. */
		ret = msgQReceive(qid, (char *)&msg, sizeof(int), WAIT_FOREVER);
		traceobj_assert(&trobj, ret == sizeof(int) && msg == n);
	}

	traceobj_mark(&trobj, 6);

	ret = msgQReceive(qid, (char *)&msg, sizeof(int), WAIT_FOREVER);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_DELETED);

	traceobj_mark(&trobj, 7);

	traceobj_exit(&trobj);
}

static void peerTask(long arg, ...)
{
	int ret, msg;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 8);

	ret = msgQReceive(qid, (char *)&msg, sizeof(int), WAIT_FOREVER);
	traceobj_assert(&trobj, ret == sizeof(int) && msg == 0);

	traceobj_mark(&trobj, 9);

	ret = msgQDelete(qid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 10);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID rtid, ptid;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	traceobj_mark(&trobj, 11);

	rtid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, rtid != ERROR);

	traceobj_mark(&trobj, 12);

	ptid = taskSpawn("peerTask",
			 51,
			 0, 0, peerTask, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, ptid != ERROR);

	traceobj_mark(&trobj, 13);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

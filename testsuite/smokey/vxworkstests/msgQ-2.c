// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskLib.h>
#include <vxworks/msgQLib.h>

static struct traceobj trobj;

static int tseq[] = {
	3, 4, 5, 6,
	1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2,
	7,
};

#define NMESSAGES (sizeof(messages) / sizeof(messages[0]))

static int messages[] = {
	0xfafafafa,
	0xbebebebe,
	0xcdcdcdcd,
	0xabcdefff,
	0x12121212,
	0x34343434,
	0x56565656,
	0x78787878,
	0xdededede,
	0xbcbcbcbc
};

MSG_Q_ID qid;

static void peerTask(long arg, ...)
{
	int ret, msg, n;

	traceobj_enter(&trobj);

	n = 1;
	do {
		traceobj_mark(&trobj, 1);
		ret = msgQReceive(qid, (char *)&msg, sizeof(int), NO_WAIT);
		traceobj_assert(&trobj, ret == sizeof(int));
		traceobj_assert(&trobj, msg == messages[NMESSAGES - n]);
		traceobj_mark(&trobj, 2);
	} while(n++ < NMESSAGES);

	traceobj_exit(&trobj);
}

static void rootTask(long arg, ...)
{
	TASK_ID tid;
	int ret, n;

	traceobj_enter(&trobj);

	qid = msgQCreate(NMESSAGES, sizeof(int), MSG_Q_PRIORITY);
	traceobj_assert(&trobj, qid != 0);

	traceobj_mark(&trobj, 3);

	ret = taskPrioritySet(taskIdSelf(), 10);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 4);

	tid = taskSpawn("peerTask",
			11,
			0, 0, peerTask, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_mark(&trobj, 5);

	n = 0;
	do
		ret = msgQSend(qid, (char *)&messages[n], sizeof(int), NO_WAIT, MSG_PRI_URGENT);
	while(n++ < NMESSAGES && ret != ERROR);

	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_UNAVAILABLE && n == NMESSAGES + 1);

	traceobj_mark(&trobj, 6);

	ret = taskDelay(10);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 7);

	ret = msgQNumMsgs(qid);
	traceobj_assert(&trobj, ret == 0);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	tid = taskSpawn("rootTask", 50,	0, 0, rootTask,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_join(&trobj);

	exit(0);
}

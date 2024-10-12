// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/msgQLib.h>

static struct traceobj trobj;

#define NMESSAGES   ARRAY_SIZE(messages)

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

static void rootTask(long arg, ...)
{
	MSG_Q_ID qid;
	int ret, msg;

	traceobj_enter(&trobj);

	qid = msgQCreate(NMESSAGES, sizeof(int), 0xffff);
	traceobj_assert(&trobj, qid == 0 && errno == S_msgQLib_INVALID_QUEUE_TYPE);

	qid = msgQCreate(-1, sizeof(int), MSG_Q_FIFO);
	traceobj_assert(&trobj, qid == 0 && errno == S_msgQLib_INVALID_QUEUE_TYPE);

	qid = msgQCreate(NMESSAGES, 0, MSG_Q_FIFO);
	traceobj_assert(&trobj, qid != 0);

	ret = msgQDelete(qid);
	traceobj_assert(&trobj, ret == OK);

	qid = msgQCreate(NMESSAGES, sizeof(int), MSG_Q_PRIORITY);
	traceobj_assert(&trobj, qid != 0);

	ret = msgQNumMsgs(qid);
	traceobj_assert(&trobj, ret == 0);

	ret = msgQSend(qid, (char *)&messages[0], sizeof(int), NO_WAIT, MSG_PRI_NORMAL);
	traceobj_assert(&trobj, ret == OK);

	ret = msgQNumMsgs(qid);
	traceobj_assert(&trobj, ret == 1);

	ret = msgQSend(qid, (char *)&messages[1], sizeof(int), NO_WAIT, MSG_PRI_NORMAL);
	traceobj_assert(&trobj, ret == OK);

	ret = msgQNumMsgs(qid);
	traceobj_assert(&trobj, ret == 2);

	ret = msgQReceive(qid, (char *)&msg, 0, NO_WAIT);
	traceobj_assert(&trobj, ret == 0);

	ret = msgQNumMsgs(qid);
	traceobj_assert(&trobj, ret == 1);

	ret = msgQReceive(qid, (char *)&msg, sizeof(int), NO_WAIT);
	traceobj_assert(&trobj, ret == sizeof(int));
	traceobj_assert(&trobj, msg == 0xbebebebe);

	ret = msgQNumMsgs(qid);
	traceobj_assert(&trobj, ret == 0);

	ret = msgQReceive(qid, (char *)&msg, sizeof(int), 1000);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_TIMEOUT);

	ret = msgQDelete(qid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;

	traceobj_init(&trobj, argv[0], 0);

	tid = taskSpawn("rootTask", 50,	0, 0, rootTask,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_join(&trobj);

	exit(0);
}

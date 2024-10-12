// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskInfo.h>
#include <vxworks/semLib.h>

static struct traceobj trobj;

static int tseq[] = {
	10, 11, 12, 13, 20,
	1, 14, 15, 2, 3, 4,
	5, 6, 7, 8, 16, 17, 18,
	9, 21, 19
};

static SEM_ID sem_id;

static void peerTask(long arg, ...)
{
	TASK_ID rtid;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 2);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 3);

	ret = taskLock();
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 4);

	rtid = taskNameToId("rootTask");
	traceobj_assert(&trobj, rtid != ERROR);

	traceobj_mark(&trobj, 5);

	ret = taskResume(rtid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 6);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 7);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 8);

	ret = taskUnlock();
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 9);

	ret = taskSuspend(taskIdSelf());
	traceobj_assert(&trobj, ret == OK);

	traceobj_exit(&trobj);
}

static void rootTask(long arg, ...)
{
	TASK_ID ptid;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 10);

	ret = taskPrioritySet(taskIdSelf(), 10);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 11);

	sem_id = semCCreate(0xffffffff, 0);
	traceobj_assert(&trobj, sem_id == 0 && errno == S_semLib_INVALID_OPTION);

	traceobj_mark(&trobj, 12);

	sem_id = semCCreate(SEM_Q_FIFO, 0);
	traceobj_assert(&trobj, sem_id != 0);

	traceobj_mark(&trobj, 13);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	ptid = taskNameToId("peerTask");
	traceobj_assert(&trobj, ptid != ERROR);

	traceobj_mark(&trobj, 14);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 15);

	ret = taskSuspend(taskIdSelf());
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 16);

	ret = semTake(sem_id, 10);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 17);

	ret = semTake(sem_id, NO_WAIT);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 18);

	ret = semTake(sem_id, 100);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_TIMEOUT);

	traceobj_mark(&trobj, 19);

	ret = taskResume(ptid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID rtid, ptid;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	rtid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, rtid != ERROR);

	traceobj_mark(&trobj, 20);

	ptid = taskSpawn("peerTask", 11, 0, 0, peerTask,
			 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, ptid != ERROR);

	traceobj_mark(&trobj, 21);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

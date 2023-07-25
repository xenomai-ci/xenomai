// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskLib.h>
#include <vxworks/taskInfo.h>
#include <vxworks/semLib.h>

static struct traceobj trobj;

static int tseq[] = {
	6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	18, 1, 2, 3, 19, 4, 5, 16, 6, 17
};

static SEM_ID sem_id;

static void peerTask(long arg, ...)
{
	TASK_ID rtid;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	rtid = taskNameToId("rootTask");
	traceobj_assert(&trobj, rtid != ERROR);

	traceobj_mark(&trobj, 2);

	ret = semTake(sem_id, NO_WAIT);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_UNAVAILABLE);

	traceobj_mark(&trobj, 3);

	ret = semTake(sem_id, 100);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_TIMEOUT);

	traceobj_mark(&trobj, 4);

	ret = taskResume(rtid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 5);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 6);

	traceobj_exit(&trobj);
}

static void rootTask(long arg, ...)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 6);

	ret = taskPrioritySet(taskIdSelf(), 11);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 7);

	sem_id = semMCreate(0xffffffff);
	traceobj_assert(&trobj, sem_id == 0 && errno == S_semLib_INVALID_OPTION);

	traceobj_mark(&trobj, 8);

	sem_id = semMCreate(SEM_Q_PRIORITY|SEM_DELETE_SAFE|SEM_INVERSION_SAFE);
	traceobj_assert(&trobj, sem_id != 0);

	traceobj_mark(&trobj, 9);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 10);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 11);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 12);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 13);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == ERROR && errno == S_semLib_INVALID_OPERATION);

	traceobj_mark(&trobj, 14);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 15);

	ret = taskSuspend(taskIdSelf());
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 16);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 17);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID rtid, ptid;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	rtid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, rtid != ERROR);

	traceobj_mark(&trobj, 18);

	ptid = taskSpawn("peerTask", 10, 0, 0, peerTask,
			 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, ptid != ERROR);

	traceobj_mark(&trobj, 19);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

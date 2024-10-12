// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/semLib.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 3, 5, 4, 6
};

static SEM_ID sem_id;

static void rootTask(long arg, ...)
{
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	sem_id = semBCreate(SEM_Q_FIFO, SEM_FULL);
	traceobj_assert(&trobj, sem_id != 0);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 2);

	ret = semTake(sem_id, NO_WAIT);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 3);

	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_DELETED);

	traceobj_mark(&trobj, 4);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	tid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_mark(&trobj, 5);

	ret = semDelete(sem_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 6);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

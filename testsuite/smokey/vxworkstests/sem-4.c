// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/semLib.h>
#include <vxworks/tickLib.h>

static struct traceobj trobj;

#define WAIT_TIME  100
#define TOLERANCE  20
#define MIN_WAIT   (WAIT_TIME - TOLERANCE)

static SEM_ID sem_id;

static void rootTask(long arg, ...)
{
	ULONG start;
	int ret;

	traceobj_enter(&trobj);

	sem_id = semCCreate(SEM_Q_PRIORITY, 0);
	traceobj_assert(&trobj, sem_id != 0);

	start = tickGet();
	ret = semTake(sem_id, WAIT_TIME);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_TIMEOUT);
	traceobj_assert(&trobj, tickGet() - start >= MIN_WAIT);

	start = tickGet();
	ret = semTake(sem_id, WAIT_TIME);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_TIMEOUT);
	traceobj_assert(&trobj, tickGet() - start >= MIN_WAIT);

	start = tickGet();
	ret = semTake(sem_id, WAIT_TIME);
	traceobj_assert(&trobj, ret == ERROR && errno == S_objLib_OBJ_TIMEOUT);
	traceobj_assert(&trobj, tickGet() - start >= MIN_WAIT);

	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);
	ret = semTake(sem_id, WAIT_TIME);
	traceobj_assert(&trobj, ret == OK);
	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);
	ret = semGive(sem_id);
	traceobj_assert(&trobj, ret == OK);
	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);
	ret = semTake(sem_id, WAIT_FOREVER);
	traceobj_assert(&trobj, ret == OK);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	tid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_join(&trobj);

	ret = semDelete(sem_id);
	traceobj_assert(&trobj, ret == OK);

	exit(0);
}

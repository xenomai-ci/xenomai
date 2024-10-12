// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/wdLib.h>
#include <vxworks/intLib.h>

static struct traceobj trobj;

static int tseq[] = {
	5, 6,
	1, 4, 1, 4, 1,
	2, 3, 7
};

static TASK_ID tid;

static WDOG_ID wdog_id;

static void watchdogHandler(long arg)
{
	static int hits;
	int ret;

	traceobj_assert(&trobj, arg == 0xfefbfcfd);

	ret = intContext();
	traceobj_assert(&trobj, ret);

	traceobj_mark(&trobj, 1);

	if (++hits >= 3) {
		ret = wdCancel(wdog_id);
		traceobj_assert(&trobj, ret == OK);
		traceobj_mark(&trobj, 2);
		ret = taskResume(tid);
		traceobj_assert(&trobj, ret == OK);
		traceobj_mark(&trobj, 3);
		return;
	}

	traceobj_mark(&trobj, 4);
	ret = wdStart(wdog_id, 200, watchdogHandler, arg);
	traceobj_assert(&trobj, ret == OK);
}

static void rootTask(long arg, ...)
{
	int ret;

	traceobj_enter(&trobj);

	tid = taskIdSelf();

	traceobj_mark(&trobj, 5);

	wdog_id = wdCreate();
	traceobj_assert(&trobj, wdog_id != 0);

	ret = wdStart(wdog_id, 200, watchdogHandler, 0xfefbfcfd);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 6);

	ret = taskSuspend(tid);
	traceobj_assert(&trobj, ret == OK);

	traceobj_mark(&trobj, 7);

	ret = wdDelete(wdog_id);
	traceobj_assert(&trobj, ret == OK);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	tid = taskSpawn("rootTask", 50, 0, 0, rootTask,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

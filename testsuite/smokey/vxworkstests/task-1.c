// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskLib.h>

static struct traceobj trobj;

static int tseq[] = { 1, 2 };

static void windTask(long a1, long a2, long a3, long a4, long a5,
		     long a6, long a7, long a8, long a9, long a10)
{
	traceobj_mark(&trobj, 1);

	traceobj_enter(&trobj);

	traceobj_assert(&trobj, a1 == 1);
	traceobj_assert(&trobj, a2 == 2);
	traceobj_assert(&trobj, a3 == 4);
	traceobj_assert(&trobj, a4 == 8);
	traceobj_assert(&trobj, a5 == 16);
	traceobj_assert(&trobj, a6 == 32);
	traceobj_assert(&trobj, a7 == 64);
	traceobj_assert(&trobj, a8 == 128);
	traceobj_assert(&trobj, a9 == 256);
	traceobj_assert(&trobj, a10 == 512);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	TASK_ID tid;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	tid = taskSpawn("windTask", 70, 0, 0, (FUNCPTR)windTask,
			1, 2, 4, 8, 16, 32, 64, 128, 256, 512);
	traceobj_assert(&trobj, tid != ERROR);

	traceobj_mark(&trobj, 2);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

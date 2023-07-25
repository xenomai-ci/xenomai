// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskLib.h>
#include <vxworks/lstLib.h>

static struct traceobj trobj;

static void rootTask(long arg, ...)
{
	NODE first, second, third, fourth;
	LIST list;

	traceobj_enter(&trobj);

	traceobj_assert(&trobj, 0 == lstNth (0, 1));
	traceobj_assert(&trobj, 0 == lstFirst(0));
	traceobj_assert(&trobj, 0 == lstLast(0));
	traceobj_assert(&trobj, 0 == lstGet(0));

	lstInit(&list);
	traceobj_assert(&trobj, 0 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstFirst(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, 0 == lstNth(&list, 1));

	lstAdd(&list, &first);
	traceobj_assert(&trobj, 1 == lstCount(&list));
	traceobj_assert(&trobj, &first == lstFirst(&list));
	traceobj_assert(&trobj, &first == lstLast(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, NULL == lstNext(&first));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first == lstNth(&list, 1));
	traceobj_assert(&trobj, 0 == lstNth(&list, 2));

	lstAdd(&list, &second);
	traceobj_assert(&trobj, 2 == lstCount(&list));
	traceobj_assert(&trobj, &first == lstFirst(&list));
	traceobj_assert(&trobj, &second == lstLast(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, 0 == lstNth(&list, 3));

	lstAdd(&list, &third);
	traceobj_assert(&trobj, 3 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, &third == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, &third  == lstNth(&list, 3));
	traceobj_assert(&trobj, 0 == lstNth(&list, 4));

	lstAdd(&list, &fourth);
	traceobj_assert(&trobj, 4 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, &fourth == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, &third  == lstNth(&list, 3));
	traceobj_assert(&trobj, &fourth == lstNth(&list, 4));
	traceobj_assert(&trobj, 0 == lstNth(&list, 5));

	lstDelete(&list, &third);
	traceobj_assert(&trobj, 3 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, &fourth == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, &fourth == lstNth(&list, 3));
	traceobj_assert(&trobj, 0 == lstNth(&list, 4));
	traceobj_assert(&trobj, 0 == lstNth(&list, 5));

	lstInsert(&list, &second, &third);
	traceobj_assert(&trobj, 4 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, &fourth == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, &third  == lstNth(&list, 3));
	traceobj_assert(&trobj, &fourth == lstNth(&list, 4));
	traceobj_assert(&trobj, 0 == lstNth(&list, 5));

	traceobj_assert(&trobj, &fourth == lstNStep(&second, 2));
	traceobj_assert(&trobj, 4 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, &fourth == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, &third  == lstNth(&list, 3));
	traceobj_assert(&trobj, &fourth == lstNth(&list, 4));
	traceobj_assert(&trobj, 0 == lstNth(&list, 5));

	traceobj_assert(&trobj, 1 == lstFind(&list, &first ));
	traceobj_assert(&trobj, 2 == lstFind(&list, &second));
	traceobj_assert(&trobj, 3 == lstFind(&list, &third ));
	traceobj_assert(&trobj, 4 == lstFind(&list, &fourth));
	traceobj_assert(&trobj, 4 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, &fourth == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &first  == lstNth(&list, 1));
	traceobj_assert(&trobj, &second == lstNth(&list, 2));
	traceobj_assert(&trobj, &third  == lstNth(&list, 3));
	traceobj_assert(&trobj, &fourth == lstNth(&list, 4));
	traceobj_assert(&trobj, 0 == lstNth(&list, 5));

	traceobj_assert(&trobj, &first == lstGet(&list));
	traceobj_assert(&trobj, 3 == lstCount(&list));
	traceobj_assert(&trobj, NULL == lstPrevious(&first));
	traceobj_assert(&trobj, NULL == lstPrevious(&second));
	traceobj_assert(&trobj, &fourth == lstLast(&list));
	traceobj_assert(&trobj, 0 == lstNth(&list, 0));
	traceobj_assert(&trobj, &second == lstNth(&list, 1));
	traceobj_assert(&trobj, &third  == lstNth(&list, 2));
	traceobj_assert(&trobj, &fourth == lstNth(&list, 3));
	traceobj_assert(&trobj, 0 == lstNth(&list, 4));
	traceobj_assert(&trobj, 0 == lstNth(&list, 5));

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

// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static u_long tid;

static void root_task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	traceobj_enter(&trobj);

	traceobj_assert(&trobj, a0 == 1);
	traceobj_assert(&trobj, a1 == 2);
	traceobj_assert(&trobj, a2 == 3);
	traceobj_assert(&trobj, a3 == 4);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = t_create("root", 1, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, root_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	exit(0);
}

// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static void task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	unsigned long timer_id;
	int i;

	traceobj_enter(&trobj);

	for (i = 0; i < 100; i++)
		tm_evafter(20, 0x1, &timer_id);

	tm_wkafter(100);

	t_delete(0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	unsigned long tid;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	tm_wkafter(10);

	traceobj_join(&trobj);

	exit(0);
}

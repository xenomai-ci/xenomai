// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

#define MAX_PRIO 95

static struct traceobj trobj;

static void test_task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	traceobj_enter(&trobj);
	tm_wkafter(1000000);
	traceobj_exit(&trobj);
}

static void root_task(u_long increment, u_long a1, u_long a2, u_long a3)
{
	u_long args[] = { 1, 2, 3, 4 }, ret, tid;
	int n;

	traceobj_enter(&trobj);

	for (n = 0; n < 512; n++) {
		if(increment)
			ret = t_create ("TEST", (n % MAX_PRIO) + 2,100000, 0, 0, &tid);
		else
			ret = t_create ("TEST", MAX_PRIO - (n % MAX_PRIO) + 1,100000, 0, 0, &tid);
		traceobj_assert(&trobj, ret == SUCCESS);
		ret = t_start(tid, T_PREEMPT, test_task, args);
		traceobj_assert(&trobj, ret == SUCCESS);
		ret = t_delete(tid);
		traceobj_assert(&trobj, ret == SUCCESS);
	}

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 }, ret, tid;

	traceobj_init(&trobj, argv[0], 0);

	// Low priority root task, loop incr. priority test tasks
	ret = t_create("root", 3, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, root_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	// Low priority root task, loop decr. priority test tasks
	args[0] = 0;
	ret = t_create("root", 3, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, root_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	// High priority root task, loop incr. priority test tasks
	args[0] = 1;
	ret = t_create("root", 90, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, root_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	// High priority root task, loop decr. priority test tasks 
	args[0] = 0;
	ret = t_create("root", 90, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, root_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);
	
	traceobj_join(&trobj);

	exit(0);
}

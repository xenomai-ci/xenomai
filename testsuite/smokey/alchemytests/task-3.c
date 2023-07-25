// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>

static struct traceobj trobj;

static RT_TASK t_a, t_b;

int main(int argc, char *const argv[])
{
	RT_TASK t;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_create(&t_a, "taskA", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_b, "taskB", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_bind(&t, "taskA", TM_NONBLOCK);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, rt_task_same(&t, &t_a));

	ret = rt_task_bind(&t, "taskB", TM_NONBLOCK);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, rt_task_same(&t, &t_b));

	ret = rt_task_delete(&t_a);
	traceobj_check(&trobj, ret, 0);
	ret = rt_task_bind(&t, "taskA", TM_NONBLOCK);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);

	ret = rt_task_delete(&t_b);
	traceobj_check(&trobj, ret, 0);
	ret = rt_task_bind(&t, "taskB", TM_NONBLOCK);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);

	ret = rt_task_shadow(NULL, "main_task", 1, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_bind(&t, "taskB", 1000000000ULL);
	traceobj_check(&trobj, ret, -ETIMEDOUT);

	exit(0);
}

// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>

static struct traceobj trobj;

static RT_TASK t_main;

static void main_task(void *arg)
{
	traceobj_enter(&trobj);
	traceobj_assert(&trobj, arg == (void *)(long)0xdeadbeef);
	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_create(&t_main, "main_task", 0, 99, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_main, main_task, (void *)(long)0xdeadbeef);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

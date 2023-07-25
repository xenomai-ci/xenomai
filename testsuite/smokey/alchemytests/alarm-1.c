// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/alarm.h>

static struct traceobj trobj;

static int tseq[] = {
	5, 6, 8,
	1, 4, 1, 4, 1,
	2, 3, 7
};

static RT_TASK t_main;

static RT_ALARM alrm;

static void alarm_handler(void *arg)
{
	static int hits;
	int ret;

	traceobj_assert(&trobj, arg == &alrm);

	traceobj_mark(&trobj, 1);

	if (++hits >= 3) {
		ret = rt_alarm_stop(&alrm);
		traceobj_check(&trobj, ret, 0);
		traceobj_mark(&trobj, 2);
		ret = rt_task_resume(&t_main);
		traceobj_check(&trobj, ret, 0);
		traceobj_mark(&trobj, 3);
		return;
	}

	traceobj_mark(&trobj, 4);
}

static void main_task(void *arg)
{
	RT_TASK *p;
	int ret;

	traceobj_enter(&trobj);

	p = rt_task_self();
	traceobj_assert(&trobj, p != NULL && rt_task_same(p, &t_main));

	traceobj_mark(&trobj, 5);

	ret = rt_alarm_start(&alrm, 200000000ULL, 200000000ULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 6);

	ret = rt_task_suspend(&t_main);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 7);

	ret = rt_alarm_delete(&alrm);
	traceobj_check(&trobj, ret, 0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_alarm_create(&alrm, "ALARM", alarm_handler, &alrm);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_spawn(&t_main, "main_task", 0,  50, 0, main_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 8);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

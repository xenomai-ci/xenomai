// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>

#define TEST_ITERATIONS (10)
#define PERIOD_SWITCH_ITERATION (5)

#define PERIOD_INITIAL (2000 * 1000)
#define PERIOD_SWITCH (1000 * 1000)
#define PERIOD_ERROR_MAX (5)

static RT_TASK t_test;
static RTIME duration[TEST_ITERATIONS];

static struct traceobj trobj;

static int tseq[] = {
	1, 2,
	3, 4, 3, 4, 3, 4, 3, 4, 3, 4,
	3, 4, 3, 4, 3, 4, 3, 4, 3, 4,
	5, 6, 7
};

static void test_task(void *arg)
{
	(void)arg;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 2);

	rt_task_set_periodic(NULL, TM_NOW, PERIOD_INITIAL);

	for (size_t i = 0; i < TEST_ITERATIONS; ++i) {
		RTIME start, end;
		int ret;

		traceobj_mark(&trobj, 3);

		start = rt_timer_read();

		if (i == PERIOD_SWITCH_ITERATION)
			rt_task_set_periodic(NULL, TM_NOW, PERIOD_SWITCH);

		ret = rt_task_wait_period(NULL);
		traceobj_check(&trobj, ret, 0);

		end = rt_timer_read();

		duration[i] = end - start;

		traceobj_mark(&trobj, 4);
	}

	traceobj_mark(&trobj, 5);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	bool on_vm = argc > 1 && strcmp(argv[1], "--vm") == 0;
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_create(&t_test, "test_task", 0, 99, 0);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 1);

	ret = rt_task_start(&t_test, test_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	traceobj_mark(&trobj, 6);

	for (size_t i = 0; i < TEST_ITERATIONS; ++i) {
		RTIME value, expect, diff, error;

		expect = PERIOD_INITIAL;
		if (i > PERIOD_SWITCH_ITERATION)
			expect = PERIOD_SWITCH;

		value = duration[i];

		diff = value < expect ? (expect - value) : (value - expect);
		error = (100 * diff) / expect;

		traceobj_assert(&trobj, error <= PERIOD_ERROR_MAX || on_vm);
	}

	traceobj_mark(&trobj, 7);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

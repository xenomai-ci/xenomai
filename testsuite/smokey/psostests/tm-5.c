// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

#define TEST_DATE  ((2008 << 16)|(4 << 8)|25) /* 4/25/2008 */
#define TEST_TIME  ((11 << 16)|(17 << 8)|30)  /* 11:17:30 */
#define TEST_TICKS 0

#define WAKEUP_DATE  ((2008 << 16)|(4 << 8)|25) /* 4/25/2008 */
#define WAKEUP_TIME  ((11 << 16)|(17 << 8)|33)  /* 11:17:33 */
#define WAKEUP_TICKS 0

static struct traceobj trobj;

static int tseq[] = {
	1, 3, 2, 4
};

static u_long tid;

static void task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	unsigned long date, time, ticks;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 3);

	ret = tm_set(TEST_DATE, TEST_TIME, TEST_TICKS);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = tm_wkwhen(WAKEUP_DATE, WAKEUP_TIME, WAKEUP_TICKS);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = tm_get(&date, &time, &ticks);
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_assert(&trobj, date == WAKEUP_DATE);
	traceobj_assert(&trobj, time == WAKEUP_TIME);

	traceobj_mark(&trobj, 4);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	traceobj_mark(&trobj, 1);

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

#define TEST_DATE  ((2008 << 16)|(4 << 8)|25) /* 4/25/2008 */
#define TEST_TIME  ((11 << 16)|(17 << 8)|30)  /* 11:17:30 */
#define TEST_TICKS 0

#define TRIG_DATE  ((2008 << 16)|(4 << 8)|25) /* 4/25/2008 */
#define TRIG_TIME  ((11 << 16)|(17 << 8)|30)  /* 11:17:30 */
#define TRIG_TICKS 400

static struct traceobj trobj;

static int tseq[] = {
	4, 1, 2, 3
};

static u_long tid;

static u_long timer_id;

static void task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long events;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = tm_set(TEST_DATE, TEST_TIME, TEST_TICKS);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = tm_evwhen(TRIG_DATE, TRIG_TIME, TRIG_TICKS, 0x1234, &timer_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = ev_receive(0x1030, EV_WAIT|EV_ANY, 800, &events);
	traceobj_assert(&trobj, ret == SUCCESS && events == 0x1030);
	traceobj_mark(&trobj, 3);

	ret = tm_cancel(timer_id);
	traceobj_assert(&trobj, ret == ERR_BADTMID);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	traceobj_mark(&trobj, 4);

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

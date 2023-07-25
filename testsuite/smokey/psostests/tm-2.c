// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	5, 1, 2, 3, 3, 3, 3, 3, 4
};

static u_long tid;

static u_long timer_id;

static void task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long events;
	int ret, n;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = tm_evevery(200, 0x1, &timer_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	for (n = 0; n < 5; n++) {
		events = 0;
		ret = ev_receive(0x1, EV_WAIT|EV_ALL, 0, &events);
		traceobj_assert(&trobj, ret == SUCCESS && events == 0x1);
		traceobj_mark(&trobj, 3);
	}

	traceobj_mark(&trobj, 4);

	ret = tm_cancel(timer_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	traceobj_mark(&trobj, 5);

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

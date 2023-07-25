// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	7, 1, 2, 3, 4, 5, 6
};

static u_long tid;

static u_long timer_id;

static void task(u_long a0, u_long a1, u_long a2, u_long a3)
{
	u_long events;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	ret = tm_evafter(200, 0x1, &timer_id);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 2);

	ret = ev_receive(0x3, EV_WAIT|EV_ALL, 300, &events);
	traceobj_assert(&trobj, ret == ERR_TIMEOUT);
	traceobj_mark(&trobj, 3);

	ret = ev_receive(0x2, EV_NOWAIT|EV_ANY, 0, &events);
	traceobj_assert(&trobj, ret == ERR_NOEVS);
	traceobj_mark(&trobj, 4);

	events = 0;
	ret = ev_receive(0x1, EV_NOWAIT|EV_ALL, 0, &events);
	traceobj_assert(&trobj, ret == SUCCESS && events == 0x1);
	traceobj_mark(&trobj, 5);

	events = 0;
	ret = ev_receive(0x1, EV_WAIT|EV_ALL, 400, &events);
	traceobj_assert(&trobj, ret == ERR_TIMEOUT);
	traceobj_mark(&trobj, 6);

	/* Valgrind will bark at this one, this is expected. */
	ret = tm_cancel(timer_id);
	traceobj_assert(&trobj, ret == ERR_BADTMID);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	traceobj_mark(&trobj, 7);

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>

static struct traceobj trobj;

static RT_TASK t_bgnd, t_fgnd;

static void background_task(void *arg)
{
	int msg, ret, flowid, n;
	RT_TASK_MCB mcb;

	traceobj_enter(&trobj);

	for (n = 0; n < 10; n++) {
		mcb.data = &msg;
		mcb.size = sizeof(msg);
		flowid = rt_task_receive(&mcb, TM_INFINITE);
		traceobj_assert(&trobj, flowid > 0);
		traceobj_assert(&trobj, mcb.opcode == 0x77);
		traceobj_assert(&trobj, mcb.size == sizeof(msg));
		traceobj_assert(&trobj, msg == n);
		msg = ~msg;
		ret = rt_task_reply(flowid, &mcb);
		traceobj_check(&trobj, ret, 0);
	}

	traceobj_exit(&trobj);
}

static void foreground_task(void *arg)
{
	RT_TASK_MCB mcb, mcb_r;
	int ret, msg, notmsg;

	traceobj_enter(&trobj);

	for (msg = 0; msg < 10; msg++) {
		rt_task_sleep(1000000ULL);
		mcb.opcode = 0x77;
		mcb.data = &msg;
		mcb.size = sizeof(msg);
		mcb_r.data = &notmsg;
		mcb_r.size = sizeof(notmsg);
		notmsg = msg;
		ret = rt_task_send(&t_bgnd, &mcb, &mcb_r, TM_INFINITE);
		traceobj_assert(&trobj, ret == sizeof(msg));
		traceobj_assert(&trobj, notmsg == ~msg);
	}

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_create(&t_bgnd, "BGND", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_bgnd, background_task, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_fgnd, "FGND", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_fgnd, foreground_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

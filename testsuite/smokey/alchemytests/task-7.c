// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>

static struct traceobj trobj;

static RT_TASK t_bgnd, t_fgnda, t_fgndb;

static void background_task(void *arg)
{
	int msg, ret, flowid, n;
	RT_TASK_MCB mcb;

	traceobj_enter(&trobj);

	ret = rt_task_reply(-1, &mcb);
	traceobj_check(&trobj, ret, -EINVAL);

	ret = rt_task_reply(999, &mcb);
	traceobj_check(&trobj, ret, -ENXIO);

	for (n = 0; n < 20; n++) {
		mcb.data = &msg;
		mcb.size = sizeof(msg);
		flowid = rt_task_receive(&mcb, TM_NONBLOCK);
		traceobj_assert(&trobj, flowid > 0);
		traceobj_assert(&trobj, mcb.size == sizeof(msg));
		switch (mcb.opcode) {
		case 0x77:
			msg = ~msg;
			ret = rt_task_reply(flowid, &mcb);
			traceobj_check(&trobj, ret, 0);
			break;
		case 0x78:
			ret = rt_task_reply(flowid, &mcb);
			traceobj_assert(&trobj, ret == -ENOBUFS);
			break;
		default:
			traceobj_assert(&trobj, 0);
		}
	}

	traceobj_exit(&trobj);
}

static void foreground_task_a(void *arg)
{
	RT_TASK_MCB mcb, mcb_r;
	int ret, msg, notmsg;

	traceobj_enter(&trobj);

	for (msg = 0; msg < 10; msg++) {
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

static void foreground_task_b(void *arg)
{
	RT_TASK_MCB mcb, mcb_r;
	int ret, msg;

	traceobj_enter(&trobj);

	for (msg = 0; msg < 10; msg++) {
		mcb.opcode = 0x78;
		mcb.data = &msg;
		mcb.size = sizeof(msg);
		mcb_r.data = NULL;
		mcb_r.size = 0;
		ret = rt_task_send(&t_bgnd, &mcb, &mcb_r, TM_INFINITE);
		traceobj_check(&trobj, ret, -ENOBUFS);
	}

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_create(&t_bgnd, "BGND", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_fgnda, "FGND-A", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_fgnda, foreground_task_a, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_fgndb, "FGND-B", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_fgndb, foreground_task_b, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_bgnd, background_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

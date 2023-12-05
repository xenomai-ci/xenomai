// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/queue.h>

static struct traceobj trobj;

#define NMESSAGES   ARRAY_SIZE(messages)

static int messages[] = {
	0xfafafafa,
	0xbebebebe,
	0xcdcdcdcd,
	0xabcdefff,
	0x12121212,
	0x34343434,
	0x56565656,
	0x78787878,
	0xdededede,
	0xbcbcbcbc
};

static void main_task(void *arg)
{
	RT_QUEUE_INFO info;
	int ret, msg = 0;
	RT_QUEUE q;

	traceobj_enter(&trobj);

	ret = rt_queue_create(&q, "QUEUE", sizeof(messages), Q_UNLIMITED, 0xffffffff);
	traceobj_check(&trobj, ret, -EINVAL);

	ret = rt_queue_create(&q, "QUEUE", 0, NMESSAGES, Q_FIFO);
	traceobj_check(&trobj, ret, -EINVAL);

	ret = rt_queue_create(&q, "QUEUE", sizeof(messages), Q_UNLIMITED, Q_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_delete(&q);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_create(&q, "QUEUE", sizeof(messages), NMESSAGES, Q_PRIO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_inquire(&q, &info);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, info.nmessages == 0);

	ret = rt_queue_write(&q, &messages[0], sizeof(int), Q_NORMAL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_inquire(&q, &info);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, info.nmessages == 1);

	ret = rt_queue_write(&q, &messages[1], sizeof(int), Q_NORMAL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_inquire(&q, &info);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, info.nmessages == 2);

	ret = rt_queue_read(&q, &msg, sizeof(msg), TM_NONBLOCK);
	traceobj_assert(&trobj, ret == sizeof(msg));
	traceobj_assert(&trobj, msg == 0xfafafafa);

	ret = rt_queue_inquire(&q, &info);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, info.nmessages == 1);

	ret = rt_queue_read(&q, &msg, sizeof(msg), TM_NONBLOCK);
	traceobj_assert(&trobj, ret == sizeof(msg));
	traceobj_assert(&trobj, msg == 0xbebebebe);

	ret = rt_queue_inquire(&q, &info);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, info.nmessages == 0);

	ret = rt_queue_read(&q, &msg, sizeof(msg), 1000000ULL);
	traceobj_check(&trobj, ret, -ETIMEDOUT);

	ret = rt_queue_delete(&q);
	traceobj_check(&trobj, ret, 0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	RT_TASK t_main;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_task_spawn(&t_main, "main_task", 0,  50, 0, main_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

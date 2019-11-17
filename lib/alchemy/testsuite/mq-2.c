#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/queue.h>

static struct traceobj trobj;

static int tseq[] = {
	3, 4, 5, 6,
	1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2,
	7,
};

#define NMESSAGES (sizeof(messages) / sizeof(messages[0]))

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
	0xbcbcbcbc,
};

RT_QUEUE q;

static void peer_task(void *arg)
{
	int ret, msg, n;

	traceobj_enter(&trobj);

	n = 1;
	do {
		traceobj_mark(&trobj, 1);
		ret = rt_queue_read(&q, &msg, sizeof(msg), TM_NONBLOCK);
		traceobj_assert(&trobj, ret == sizeof(msg));
		traceobj_assert(&trobj, msg == messages[NMESSAGES - n - 1]);
		traceobj_mark(&trobj, 2);
	} while (n++ < NMESSAGES - 1);

	traceobj_exit(&trobj);
}

static void main_task(void *arg)
{
	RT_QUEUE_INFO info;
	RT_TASK t_peer;
	int ret, n;

	traceobj_enter(&trobj);

	ret = rt_queue_create(&q, "QUEUE", sizeof(messages), NMESSAGES - 1, Q_PRIO);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 3);

	ret = rt_task_set_priority(NULL, 11);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 4);

	ret = rt_task_spawn(&t_peer, "peer_task", 0,  10, 0, peer_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_mark(&trobj, 5);

	n = 0;
	do {
		ret = rt_queue_write(&q, &messages[n], sizeof(int), Q_URGENT); /* LIFO */
	} while (++n < NMESSAGES && ret >= 0);

	traceobj_assert(&trobj, ret == -ENOMEM && n == NMESSAGES);

	traceobj_mark(&trobj, 6);

	rt_task_sleep(10000000ULL);

	traceobj_mark(&trobj, 7);

	ret = rt_queue_inquire(&q, &info);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, info.nmessages == 0);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	RT_TASK t_main;
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_spawn(&t_main, "main_task", 0,  50, 0, main_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

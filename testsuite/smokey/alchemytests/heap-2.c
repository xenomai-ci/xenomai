// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/heap.h>
#include <alchemy/queue.h>

#define HEAPSIZE  16384
#define MSGSIZE   16
#define NMESSAGES (HEAPSIZE / MSGSIZE)
#define POOLSIZE  (NMESSAGES * sizeof(void *))

static struct traceobj trobj;

static RT_TASK t_pull, t_push;

static RT_HEAP heap1, heap2;

static RT_QUEUE queue1, queue2;

static void pull_task(void *arg)
{
	int ret, n = 0;
	void *p;

	traceobj_enter(&trobj);

	while (n++ < 1000) {
		ret = rt_heap_alloc(&heap1, MSGSIZE, TM_INFINITE, &p);
		traceobj_check(&trobj, ret, 0);
		ret = rt_queue_write(&queue1, &p, sizeof(p), Q_NORMAL);
		traceobj_assert(&trobj, ret >= 0);

		ret = rt_queue_read(&queue2, &p, sizeof(p), TM_INFINITE);
		traceobj_assert(&trobj, ret == sizeof(p));
		ret = rt_heap_free(&heap2, p);
		traceobj_check(&trobj, ret, 0);
	}

	traceobj_exit(&trobj);
}

static void push_task(void *arg)
{
	int ret, n = 0;
	void *p;

	traceobj_enter(&trobj);

	while (n++ < 1000) {
		ret = rt_queue_read(&queue1, &p, sizeof(p), TM_INFINITE);
		traceobj_assert(&trobj, ret == sizeof(p));
		ret = rt_heap_free(&heap1, p);
		traceobj_check(&trobj, ret, 0);
	
		ret = rt_heap_alloc(&heap2, MSGSIZE, TM_INFINITE, &p);
		traceobj_check(&trobj, ret, 0);
		ret = rt_queue_write(&queue2, &p, sizeof(p), Q_NORMAL);
		traceobj_assert(&trobj, ret >= 0);
	}

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_heap_create(&heap1, "HEAP1", HEAPSIZE, H_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_heap_create(&heap2, "HEAP2", HEAPSIZE, H_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_create(&queue1, "QUEUE1", POOLSIZE, NMESSAGES, Q_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_queue_create(&queue2, "QUEUE2", POOLSIZE, NMESSAGES, Q_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_pull, "PULL", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_push, "PUSH", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_pull, pull_task, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_push, push_task, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

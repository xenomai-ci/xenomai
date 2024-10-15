#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/heap.h>

static int tseq[] = {
	7, 1, 2, 3, 4,
	8, 9, 5, 6, 10,
	11, 12,
};

static struct traceobj trobj;

static RT_TASK t_bgnd, t_fgnd;

static bool on_vm;

static void background_task(void *arg)
{
	void *p1, *p2;
	RT_HEAP heap;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 7);
	ret = rt_heap_bind(&heap, "HEAP", TM_INFINITE);
	traceobj_check(&trobj, ret, 0);
	traceobj_mark(&trobj, 8);

	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p1);
	traceobj_mark(&trobj, 9);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);
	ret = rt_heap_alloc(&heap, 8192, TM_INFINITE, &p1);
	traceobj_mark(&trobj, 10);
	traceobj_check(&trobj, ret, 0);
	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p2);
	traceobj_mark(&trobj, 11);
	traceobj_check(&trobj, ret, 0);
	ret = rt_heap_alloc(&heap, 8192, TM_INFINITE, &p1);
	traceobj_mark(&trobj, 12);
	traceobj_check(&trobj, ret, -EIDRM);

	traceobj_exit(&trobj);
}

static void foreground_task(void *arg)
{
	void *p1, *p2;
	RT_HEAP heap;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);
	ret = rt_heap_bind(&heap, "HEAP", TM_INFINITE);
	traceobj_check(&trobj, ret, 0);
	traceobj_mark(&trobj, 2);

	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p1);
	traceobj_check(&trobj, ret, 0);
	traceobj_mark(&trobj, 3);
	ret = rt_heap_alloc(&heap, 8192, TM_NONBLOCK, &p2);
	traceobj_check(&trobj, ret, 0);
	traceobj_mark(&trobj, 4);

	ret = rt_task_set_priority(NULL, 19);
	traceobj_check(&trobj, ret, 0);
	traceobj_mark(&trobj, 5);
	ret = rt_task_set_priority(NULL, 21);
	traceobj_check(&trobj, ret, 0);
	traceobj_mark(&trobj, 6);

	ret = rt_heap_free(&heap, p1);
	traceobj_check(&trobj, ret, 0);
	ret = rt_heap_free(&heap, p2);
	traceobj_check(&trobj, ret, 0);

	rt_task_sleep(on_vm ? 100000000ULL : 1000000ULL);

	ret = rt_heap_delete(&heap);
	traceobj_check(&trobj, ret, 0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	RT_HEAP heap;
	int ret;

	on_vm = argc > 1 && strcmp(argv[1], "--vm") == 0;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = rt_task_create(&t_fgnd, "FGND", 0,  21, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_bgnd, "BGND", 0,  20, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_bgnd, background_task, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_fgnd, foreground_task, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_heap_create(&heap, "HEAP", 16384, H_PRIO);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}

#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/event.h>

static struct traceobj trobj;

static RT_TASK t_bgnd, t_fgnd;

static RT_EVENT event;

static void background_task(void *arg)
{
	unsigned int flags;
	int ret, n;

	traceobj_enter(&trobj);

	for (n = 0; n < 10; n++) {
		flags = 0;
		ret = rt_event_wait(&event, 0x55555, &flags, EV_ANY, TM_INFINITE);
		traceobj_check(&trobj, ret, 0);
		traceobj_assert(&trobj, flags == 1 << n * 2);
		ret = rt_event_clear(&event, flags, NULL);
		traceobj_check(&trobj, ret, 0);
		ret = rt_event_signal(&event, 2 << n * 2);
		traceobj_check(&trobj, ret, 0);
	}

	ret = rt_event_wait(&event, 0x55555, &flags, EV_ANY, TM_INFINITE);
	traceobj_check(&trobj, ret, -EIDRM);

	traceobj_exit(&trobj);
}

static void foreground_task(void *arg)
{
	unsigned int flags;
	int ret, n;

	traceobj_enter(&trobj);

	for (n = 0; n < 10; n++) {
		flags = 0;
		ret = rt_event_signal(&event, 1 << n * 2);
		traceobj_check(&trobj, ret, 0);
		ret = rt_event_wait(&event, 2 << n * 2, &flags, EV_ALL, TM_NONBLOCK);
		traceobj_check(&trobj, ret, -EWOULDBLOCK);
		ret = rt_event_wait(&event, 2 << n * 2, &flags, EV_ALL, TM_INFINITE);
		traceobj_check(&trobj, ret, 0);
		traceobj_assert(&trobj, flags == 2 << n * 2);
		ret = rt_event_clear(&event, flags, NULL);
		traceobj_check(&trobj, ret, 0);
	}

	rt_task_sleep(1000000ULL);
	ret = rt_event_delete(&event);
	traceobj_check(&trobj, ret, 0);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	unsigned int flags;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_event_create(&event, "EVENT", 0, EV_FIFO);
	traceobj_check(&trobj, ret, 0);

	ret = rt_event_signal(&event, 0x3);
	traceobj_check(&trobj, ret, 0);
	ret = rt_event_wait(&event, 0x1, &flags, EV_ALL, TM_NONBLOCK);
	traceobj_check(&trobj, ret, 0);
	traceobj_assert(&trobj, flags == 0x1);
	ret = rt_event_clear(&event, 0x3, NULL);
	traceobj_check(&trobj, ret, 0);

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

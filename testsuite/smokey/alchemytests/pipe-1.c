// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <alchemy/pipe.h>

static struct traceobj trobj;

static RT_TASK t_real;

static RT_PIPE mpipe;

static pthread_t t_reg;

static int minor;

struct pipe_message {
	int value;
};

static void realtime_task(void *arg)
{
	struct pipe_message m;
	int ret, seq = 0;

	traceobj_enter(&trobj);

	ret = rt_pipe_bind(&mpipe, "pipe", TM_INFINITE);
	traceobj_check(&trobj, ret, 0);

	while (seq < 8192) {
		ret = rt_pipe_read(&mpipe, &m, sizeof(m), TM_INFINITE);
		traceobj_assert(&trobj, ret == sizeof(m));
		traceobj_assert(&trobj, m.value == seq);
		ret = rt_pipe_write(&mpipe, &m, sizeof(m),
				    (seq & 1) ? P_URGENT : P_NORMAL);
		traceobj_assert(&trobj, ret == sizeof(m));
		seq++;
	}

	pthread_cancel(t_reg);

	traceobj_exit(&trobj);
}

static void *regular_thread(void *arg)
{
	struct pipe_message m;
	int fd, seq = 0;
	ssize_t ret;
	char *rtp;

	asprintf(&rtp, "/dev/rtp%d", minor);

	fd = open(rtp, O_RDWR);
	free(rtp);
	traceobj_assert(&trobj, fd >= 0);

	for (;;) {
		m.value = seq;
		ret = write(fd, &m, sizeof(m));
		traceobj_assert(&trobj, ret == sizeof(m));
		ret = read(fd, &m, sizeof(m));
		traceobj_assert(&trobj, ret == sizeof(m));
		traceobj_assert(&trobj, m.value == seq);
		seq++;
	}

	return NULL;
}

int main(int argc, char *const argv[])
{
	struct timespec ts_start, ts_end, ts_timeout, ts_delta;
	struct pipe_message m;
	RTIME start, end;
	SRTIME timeout;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rt_pipe_create(&mpipe, "pipe", P_MINOR_AUTO, 0);
	traceobj_assert(&trobj, ret >= 0);

	ret = rt_pipe_delete(&mpipe);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_create(&t_real, "realtime", 0,  10, 0);
	traceobj_check(&trobj, ret, 0);

	ret = rt_task_start(&t_real, realtime_task, NULL);
	traceobj_check(&trobj, ret, 0);

	ret = rt_pipe_create(&mpipe, "pipe", P_MINOR_AUTO, 16384);
	traceobj_assert(&trobj, ret >= 0);
	minor = ret;

	ret = rt_pipe_read(&mpipe, &m, sizeof(m), TM_NONBLOCK);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);

	ret = rt_pipe_read_until(&mpipe, &m, sizeof(m), TM_NONBLOCK);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);

	ts_timeout.tv_sec = 0;
	ts_timeout.tv_nsec = 0;
	ret = rt_pipe_read_timed(&mpipe, &m, sizeof(m), &ts_timeout);
	traceobj_check(&trobj, ret, -EWOULDBLOCK);

	start = rt_timer_read();
	timeout = rt_timer_ns2ticks(100000000);
	ret = rt_pipe_read(&mpipe, &m, sizeof(m), timeout);
	end = rt_timer_read();
	traceobj_assert(&trobj, end - start >= timeout);
	traceobj_assert(&trobj, end - start < timeout + rt_timer_ns2ticks(5000000));

	start = rt_timer_read();
	timeout = start + rt_timer_ns2ticks(100000000);
	ret = rt_pipe_read_until(&mpipe, &m, sizeof(m), timeout);
	end = rt_timer_read();
	traceobj_assert(&trobj, end >= timeout);
	traceobj_assert(&trobj, end < timeout + rt_timer_ns2ticks(5000000));

	clock_gettime(CLOCK_COPPERPLATE, &ts_start);
	timespec_adds(&ts_timeout, &ts_start, 100000000);
	ret = rt_pipe_read_timed(&mpipe, &m, sizeof(m), &ts_timeout);
	clock_gettime(CLOCK_COPPERPLATE, &ts_end);
	timespec_sub(&ts_delta, &ts_end, &ts_timeout);
	traceobj_assert(&trobj, ts_delta.tv_sec >= 0);
	traceobj_assert(&trobj, ts_delta.tv_nsec < 5000000);

	ret = pthread_create(&t_reg, NULL, regular_thread, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

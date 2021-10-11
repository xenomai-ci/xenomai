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
	struct pipe_message m;
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

	ret = pthread_create(&t_reg, NULL, regular_thread, NULL);
	traceobj_check(&trobj, ret, 0);

	traceobj_join(&trobj);

	exit(0);
}

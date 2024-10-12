/*
 * RTIPC/XDDP test.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <smokey/smokey.h>
#include <rtdm/ipc.h>

smokey_test_plugin(xddp,
		   SMOKEY_NOARGS,
		   "Check RTIPC/XDDP protocol."
);

static pthread_t rt1, rt2, nrt;

static sem_t semsync;

#define XDDP_PORT_LABEL  "xddp-smokey"

static void fail(const char *reason)
{
	perror(reason);
	exit(EXIT_FAILURE);
}

static void *realtime_thread1(void *arg)
{
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;
	long control = 0, data;
	fd_set set;
	int ret, s;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP);
	if (s < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	strcpy(plabel.label, XDDP_PORT_LABEL);
	ret = setsockopt(s, SOL_XDDP, XDDP_LABEL, &plabel, sizeof(plabel));
	if (ret)
		fail("setsockopt");

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = -1;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("bind");

	FD_ZERO(&set);
	FD_SET(s, &set);
	sem_post(&semsync); /* unleash client RT thread */

	for (;;) {
		control++;
		ret = select(s + 1, &set, NULL, NULL, NULL);
		if (ret != 1 || !FD_ISSET(s, &set))
			fail("select");

		/*
		 * We can't race with any other reader in this setup,
		 * so recvfrom() shall confirm the select() result.
		 */
		ret = recvfrom(s, &data, sizeof(data), MSG_DONTWAIT, NULL, 0);
		if (ret != sizeof(data)) {
			close(s);
			fail("recvfrom");
		}

		if (data != control) {
			close(s);
			smokey_note("data does not match control value");
			errno = -EINVAL;
			fail("recvfrom");
		}

		smokey_trace("%s: %ld relayed by peer", __func__, data);
	}

	return NULL;
}

static void sem_sync(sem_t *sem)
{
	int ret;

	for (;;) {
		ret = sem_wait(sem);
		if (ret == 0)
			return;
		if (errno != EINTR)
			fail("sem_wait");
	}
}

static void *realtime_thread2(void *arg)
{
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;
	int ret, s, loops = 30;
	struct timespec ts;
	struct timeval tv;
	socklen_t addrlen;
	long data = 0;
	fd_set set;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP);
	if (s < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
			 &tv, sizeof(tv));
	if (ret)
		fail("setsockopt");

	strcpy(plabel.label, XDDP_PORT_LABEL);
	ret = setsockopt(s, SOL_XDDP, XDDP_LABEL,
			 &plabel, sizeof(plabel));
	if (ret)
		fail("setsockopt");

	sem_sync(&semsync);

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = -1;	/* Tell XDDP to search by label. */
	ret = connect(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("connect");

	addrlen = sizeof(saddr);
	ret = getpeername(s, (struct sockaddr *)&saddr, &addrlen);
	if (ret || addrlen != sizeof(saddr))
		fail("getpeername");

	smokey_trace("%s: NRT peer is reading from /dev/rtp%d",
		     __func__, saddr.sipc_port);

	FD_ZERO(&set);
	FD_SET(s, &set);

	while (--loops) {
		ret = select(s + 1, NULL, &set, NULL, NULL);
		/* Should always be immediately writable. */
		if (ret != 1 || !FD_ISSET(s, &set))
			fail("select");

		/*
		 * Actually we might fail sending although select() on
		 * POLLOUT succeeded earlier, as the situation might
		 * have changed in the meantime due to a sudden
		 * pressure on the system heap. Pretend it did not.
		 */
		data++;
		ret = sendto(s, &data, sizeof(data), MSG_DONTWAIT, NULL, 0);
		if (ret != sizeof(data))
			fail("sendto");

		smokey_trace("%s: sent %d bytes, %ld", __func__, ret, data);

		ts.tv_sec = 0;
		ts.tv_nsec = 100000000; /* 100 ms */
		clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
	}

	sleep(1);	/* Wait for the output to drain. */

	return NULL;
}

static void *regular_thread(void *arg)
{
	char *devname;
	int fd, ret;
	long data;

	if (asprintf(&devname,
		     "/proc/xenomai/registry/rtipc/xddp/%s",
		     XDDP_PORT_LABEL) < 0)
		fail("asprintf");

	do
		fd = open(devname, O_RDWR);
	while (fd < 0 && errno == ENOENT);
	free(devname);
	if (fd < 0)
		fail("open");

	for (;;) {
		ret = read(fd, &data, sizeof(data));
		if (ret != sizeof(data))
			fail("read");

		ret = write(fd, &data, sizeof(data));
		if (ret != sizeof(data))
			fail("write");
	}

	return NULL;
}

static int run_xddp(struct smokey_test *t, int argc, char *const argv[])
{
	struct sched_param param = { .sched_priority = 42 };
	pthread_attr_t rtattr, regattr;
	int s;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP);
	if (s < 0) {
		if (errno == EAFNOSUPPORT)
			return -ENOSYS;
	} else
		close(s);

	sem_init(&semsync, 0, 0);

	pthread_attr_init(&rtattr);
	pthread_attr_setdetachstate(&rtattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&rtattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&rtattr, SCHED_FIFO);
	pthread_attr_setschedparam(&rtattr, &param);

	errno = pthread_create(&rt1, &rtattr, &realtime_thread1, NULL);
	if (errno)
		fail("pthread_create");

	errno = pthread_create(&rt2, &rtattr, &realtime_thread2, NULL);
	if (errno)
		fail("pthread_create");

	pthread_attr_init(&regattr);
	pthread_attr_setdetachstate(&regattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&regattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&regattr, SCHED_OTHER);

	errno = pthread_create(&nrt, &regattr, &regular_thread, NULL);
	if (errno)
		fail("pthread_create");

	pthread_join(rt2, NULL);
	pthread_cancel(rt1);
	pthread_cancel(nrt);
	pthread_join(rt1, NULL);
	pthread_join(nrt, NULL);

	return 0;
}

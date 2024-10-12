/*
 * RTIPC/BUFP test.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <smokey/smokey.h>
#include <rtdm/ipc.h>

smokey_test_plugin(bufp,
		   SMOKEY_NOARGS,
		   "Check RTIPC/BUFP protocol."
);

#define BUFP_SVPORT 12

static pthread_t svtid, cltid;

static void fail(const char *reason)
{
	perror(reason);
	exit(EXIT_FAILURE);
}

static void *server(void *arg)
{
	struct sockaddr_ipc saddr, claddr;
	long data, control = 0;
	socklen_t addrlen;
	size_t bufsz;
	fd_set set;
	int ret, s;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_BUFP);
	if (s < 0)
		fail("socket");

	bufsz = 32768; /* bytes */
	ret = setsockopt(s, SOL_BUFP, BUFP_BUFSZ,
			 &bufsz, sizeof(bufsz));
	if (ret)
		fail("setsockopt");

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = BUFP_SVPORT;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("bind");

	FD_ZERO(&set);
	FD_SET(s, &set);

	for (;;) {
		control++;
		ret = select(s + 1, &set, NULL, NULL, NULL);
		if (ret != 1 || !FD_ISSET(s, &set))
			fail("select");

		/*
		 * We can't race with any other reader in this setup,
		 * so recvfrom() shall confirm the select() result.
		 */
		addrlen = sizeof(saddr);
		ret = recvfrom(s, &data, sizeof(data), MSG_DONTWAIT,
			       (struct sockaddr *)&claddr, &addrlen);
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
		smokey_trace("%s: received %d bytes, %ld from port %d",
			     __func__, ret, data, claddr.sipc_port);
	}

	return NULL;
}

static void *client(void *arg)
{
	struct sockaddr_ipc svsaddr;
	int ret, s, loops = 30;
	struct timespec ts;
	long data = 0;
	fd_set set;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_BUFP);
	if (s < 0)
		fail("socket");

	memset(&svsaddr, 0, sizeof(svsaddr));
	svsaddr.sipc_family = AF_RTIPC;
	svsaddr.sipc_port = BUFP_SVPORT;
	ret = connect(s, (struct sockaddr *)&svsaddr, sizeof(svsaddr));
	if (ret)
		fail("connect");

	FD_ZERO(&set);
	FD_SET(s, &set);

	while (--loops) {
		ret = select(s + 1, NULL, &set, NULL, NULL);
		if (ret != 1 || !FD_ISSET(s, &set))
			fail("select");
		data++;
		ret = sendto(s, &data, sizeof(data), MSG_DONTWAIT,
			     (struct sockaddr *)&svsaddr, sizeof(svsaddr));
		if (ret != sizeof(data)) {
			close(s);
			fail("sendto");
		}
		smokey_trace("%s: sent %d bytes, %ld", __func__, ret, data);
		ts.tv_sec = 0;
		ts.tv_nsec = 100000000; /* 100 ms */
		clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
	}

	return NULL;
}

static int run_bufp(struct smokey_test *t, int argc, char *const argv[])
{
	struct sched_param svparam = {.sched_priority = 71 };
	struct sched_param clparam = {.sched_priority = 70 };
	pthread_attr_t svattr, clattr;
	int s;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_BUFP);
	if (s < 0) {
		if (errno == EAFNOSUPPORT)
			return -ENOSYS;
	} else
		close(s);

	pthread_attr_init(&svattr);
	pthread_attr_setdetachstate(&svattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&svattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&svattr, SCHED_FIFO);
	pthread_attr_setschedparam(&svattr, &svparam);

	errno = pthread_create(&svtid, &svattr, &server, NULL);
	if (errno)
		fail("pthread_create");

	pthread_attr_init(&clattr);
	pthread_attr_setdetachstate(&clattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&clattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&clattr, SCHED_FIFO);
	pthread_attr_setschedparam(&clattr, &clparam);

	errno = pthread_create(&cltid, &clattr, &client, NULL);
	if (errno)
		fail("pthread_create");

	pthread_join(cltid, NULL);
	pthread_cancel(svtid);
	pthread_join(svtid, NULL);

	return 0;
}

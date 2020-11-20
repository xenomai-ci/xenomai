/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 *
 * IDDP-based client/server demo, using the sendto(2)/recvfrom(2)
 * system calls to exchange data over a socket.
 *
 * In this example, two sockets are created.  A server thread (reader)
 * is bound to a real-time port and receives datagrams sent to this
 * port from a client thread (writer). The client socket is bound to a
 * different port, only to provide a valid peer name; this is
 * optional.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <rtdm/ipc.h>

pthread_t svtid, cltid;

#define IDDP_SVPORT 12
#define IDDP_CLPORT 13

static const char *msg[] = {
	"Surfing With The Alien",
	"Lords of Karma",
	"Banana Mango",
	"Psycho Monkey",
	"Luminous Flesh Giants",
	"Moroccan Sunset",
	"Satch Boogie",
	"Flying In A Blue Dream",
	"Ride",
	"Summer Song",
	"Speed Of Light",
	"Crystal Planet",
	"Raspberry Jam Delta-V",
	"Champagne?",
	"Clouds Race Across The Sky",
	"Engines Of Creation"
};

static void fail(const char *reason)
{
	perror(reason);
	exit(EXIT_FAILURE);
}

static void *server(void *arg)
{
	struct sockaddr_ipc saddr, claddr;
	socklen_t addrlen;
	char buf[128];
	size_t poolsz;
	int ret, s;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_IDDP);
	if (s < 0)
		fail("socket");

	/*
	 * Set a local 32k pool for the server endpoint. Memory needed
	 * to convey datagrams will be pulled from this pool, instead
	 * of Xenomai's system pool.
	 */
	poolsz = 32768; /* bytes */
	ret = setsockopt(s, SOL_IDDP, IDDP_POOLSZ,
			 &poolsz, sizeof(poolsz));
	if (ret)
		fail("setsockopt");

	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = IDDP_SVPORT;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("bind");

	for (;;) {
		addrlen = sizeof(saddr);
		ret = recvfrom(s, buf, sizeof(buf), 0,
			       (struct sockaddr *)&claddr, &addrlen);
		if (ret < 0) {
			close(s);
			fail("recvfrom");
		}
		printf("%s: received %d bytes, \"%.*s\" from port %d\n",
		       __FUNCTION__, ret, ret, buf, claddr.sipc_port);
	}

	return NULL;
}

static void *client(void *arg)
{
	struct sockaddr_ipc svsaddr, clsaddr;
	int ret, s, n = 0, len;
	struct timespec ts;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_IDDP);
	if (s < 0)
		fail("socket");

	clsaddr.sipc_family = AF_RTIPC;
	clsaddr.sipc_port = IDDP_CLPORT;
	ret = bind(s, (struct sockaddr *)&clsaddr, sizeof(clsaddr));
	if (ret)
		fail("bind");

	svsaddr.sipc_family = AF_RTIPC;
	svsaddr.sipc_port = IDDP_SVPORT;
	for (;;) {
		len = strlen(msg[n]);
		ret = sendto(s, msg[n], len, 0,
			     (struct sockaddr *)&svsaddr, sizeof(svsaddr));
		if (ret < 0) {
			close(s);
			fail("sendto");
		}
		printf("%s: sent %d bytes, \"%.*s\"\n",
		       __FUNCTION__, ret, ret, msg[n]);
		n = (n + 1) % (sizeof(msg) / sizeof(msg[0]));
		/*
		 * We run in full real-time mode (i.e. primary mode),
		 * so we have to let the system breathe between two
		 * iterations.
		 */
		ts.tv_sec = 0;
		ts.tv_nsec = 500000000; /* 500 ms */
		clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct sched_param svparam = {.sched_priority = 71 };
	struct sched_param clparam = {.sched_priority = 70 };
	pthread_attr_t svattr, clattr;
	sigset_t set;
	int sig;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

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

	__STD(sigwait(&set, &sig));
	pthread_cancel(svtid);
	pthread_cancel(cltid);
	pthread_join(svtid, NULL);
	pthread_join(cltid, NULL);

	return 0;
}

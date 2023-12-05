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
 * BUFP-based client/server demo, using the read(2)/write(2)
 * system calls to exchange data over a socket.
 *
 * In this example, two sockets are created.  A server thread (reader)
 * is bound to a real-time port and receives a stream of bytes sent to
 * this port from a client thread (writer).
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include <boilerplate/ancillaries.h>
#include <rtdm/ipc.h>

pthread_t svtid, cltid;

#define BUFP_SVPORT 12

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
	struct sockaddr_ipc saddr;
	char buf[128];
	size_t bufsz;
	int ret, s;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_BUFP);
	if (s < 0)
		fail("socket");

	/*
	 * Set a 16k buffer for the server endpoint. This
	 * configuration must be done prior to binding the socket to a
	 * port.
	 */
	bufsz = 16384; /* bytes */
	ret = setsockopt(s, SOL_BUFP, BUFP_BUFSZ,
			 &bufsz, sizeof(bufsz));
	if (ret)
		fail("setsockopt");

	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = BUFP_SVPORT;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("bind");

	for (;;) {
		ret = read(s, buf, sizeof(buf));
		if (ret < 0) {
			close(s);
			fail("read");
		}
		printf("%s: received %d bytes, \"%.*s\"\n",
		       __FUNCTION__, ret, ret, buf);
	}

	return NULL;
}

static void *client(void *arg)
{
	struct sockaddr_ipc svsaddr;
	int ret, s, n = 0, len;
	struct timespec ts;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_BUFP);
	if (s < 0)
		fail("socket");

	memset(&svsaddr, 0, sizeof(svsaddr));
	svsaddr.sipc_family = AF_RTIPC;
	svsaddr.sipc_port = BUFP_SVPORT;
	ret = connect(s, (struct sockaddr *)&svsaddr, sizeof(svsaddr));
	if (ret)
		fail("connect");

	for (;;) {
		len = strlen(msg[n]);
		ret = write(s, msg[n], len);
		if (ret < 0) {
			close(s);
			fail("write");
		}
		printf("%s: sent %d bytes, \"%.*s\"\n",
		       __FUNCTION__, ret, ret, msg[n]);
		n = (n + 1) % ARRAY_SIZE(msg);
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

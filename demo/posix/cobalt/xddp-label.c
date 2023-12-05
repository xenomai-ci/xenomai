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
 * XDDP-based RT/NRT threads communication demo.
 *
 * Real-time Xenomai threads and regular Linux threads may want to
 * exchange data in a way that does not require the former to leave
 * the real-time domain (i.e. secondary mode). Message pipes - as
 * implemented by the RTDM-based XDDP protocol - are provided for this
 * purpose.
 *
 * On the Linux domain side, pseudo-device files named /dev/rtp<minor>
 * give regular POSIX threads access to non real-time communication
 * endpoints, via the standard character-based I/O interface. On the
 * Xenomai domain side, sockets may be bound to XDDP ports, which act
 * as proxies to send and receive data to/from the associated
 * pseudo-device files. Ports and pseudo-device minor numbers are
 * paired, meaning that e.g. port 7 will proxy the traffic for
 * /dev/rtp7. Therefore, port numbers may range from 0 to
 * CONFIG_XENO_OPT_PIPE_NRDEV - 1.
 *
 * All data sent through a bound/connected XDDP socket via sendto(2) or
 * write(2) will be passed to the peer endpoint in the Linux domain,
 * and made available for reading via the standard read(2) system
 * call. Conversely, all data sent using write(2) through the non
 * real-time endpoint will be conveyed to the real-time socket
 * endpoint, and made available to the recvfrom(2) or read(2) system
 * calls.
 *
 * ASCII labels can be attached to bound ports, in order to connect
 * sockets to them in a more descriptive way than using plain numeric
 * port values.
 *
 * The example code below illustrates the following process:
 *
 * realtime_thread1----------------------------->----------+
 *   =>  get socket                                        |
 *   =>  bind socket to port "xddp-demo                    |
 *   =>  read traffic from NRT domain via recvfrom()    <--+--+
 *                                                         |  |
 * realtime_thread2----------------------------------------+  |
 *   =>  get socket                                        |  |
 *   =>  connect socket to port "xddp-demo"                |  |
 *   =>  write traffic to NRT domain via sendto()          v  |
 *                                                         |  ^
 * regular_thread------------------------------------------+  |
 *   =>  open /proc/xenomai/registry/rtipc/xddp/xddp-demo  |  |
 *   =>  read traffic from RT domain via read()            |  |
 *   =>  mirror traffic to RT domain via write()           +--+
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include <boilerplate/ancillaries.h>
#include <rtdm/ipc.h>

pthread_t rt1, rt2, nrt;

#define XDDP_PORT_LABEL  "xddp-demo"

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

static void *realtime_thread1(void *arg)
{
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;
	char buf[128];
	int ret, s;

	/*
	 * Get a datagram socket to bind to the RT endpoint. Each
	 * endpoint is represented by a port number within the XDDP
	 * protocol namespace.
	 */
	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP);
	if (s < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	/*
	 * Set a port label. This name will be registered when
	 * binding, in addition to the port number (if given).
	 */
	strcpy(plabel.label, XDDP_PORT_LABEL);
	ret = setsockopt(s, SOL_XDDP, XDDP_LABEL,
			 &plabel, sizeof(plabel));
	if (ret)
		fail("setsockopt");
	/*
	 * Bind the socket to the port, to setup a proxy to channel
	 * traffic to/from the Linux domain. Assign that port a label,
	 * so that peers may use a descriptive information to locate
	 * it. For instance, the pseudo-device matching our RT
	 * endpoint will appear as
	 * /proc/xenomai/registry/rtipc/xddp/<XDDP_PORT_LABEL> in the
	 * Linux domain, once the socket is bound.
	 *
	 * saddr.sipc_port specifies the port number to use. If -1 is
	 * passed, the XDDP driver will auto-select an idle port.
	 */
	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = -1;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("bind");

	for (;;) {
		/* Get packets relayed by the regular thread */
		ret = recvfrom(s, buf, sizeof(buf), 0, NULL, 0);
		if (ret <= 0)
			fail("recvfrom");

		printf("%s: \"%.*s\" relayed by peer\n", __FUNCTION__, ret, buf);
	}

	return NULL;
}

static void *realtime_thread2(void *arg)
{
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;
	int ret, s, n = 0, len;
	struct timespec ts;
	struct timeval tv;
	socklen_t addrlen;

	s = socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP);
	if (s < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	/*
	 * Set the socket timeout; it will apply when attempting to
	 * connect to a labeled port, and to recvfrom() calls.  The
	 * following setup tells the XDDP driver to wait for at most
	 * one second until a socket is bound to a port using the same
	 * label, or return with a timeout error.
	 */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
			 &tv, sizeof(tv));
	if (ret)
		fail("setsockopt");

	/*
	 * Set a port label. This name will be used to find the peer
	 * when connecting, instead of the port number.
	 */
	strcpy(plabel.label, XDDP_PORT_LABEL);
	ret = setsockopt(s, SOL_XDDP, XDDP_LABEL,
			 &plabel, sizeof(plabel));
	if (ret)
		fail("setsockopt");

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = -1;	/* Tell XDDP to search by label. */
	ret = connect(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("connect");

	/*
	 * We succeeded in making the port our default destination
	 * address by using its label, but we don't know its actual
	 * port number yet. Use getpeername() to retrieve it.
	 */
	addrlen = sizeof(saddr);
	ret = getpeername(s, (struct sockaddr *)&saddr, &addrlen);
	if (ret || addrlen != sizeof(saddr))
		fail("getpeername");

	printf("%s: NRT peer is reading from /dev/rtp%d\n",
	       __FUNCTION__, saddr.sipc_port);

	for (;;) {
		len = strlen(msg[n]);
		/*
		 * Send a datagram to the NRT endpoint via the proxy.
		 * We may pass a NULL destination address, since the
		 * socket was successfully assigned the proper default
		 * address via connect(2).
		 */
		ret = sendto(s, msg[n], len, 0, NULL, 0);
		if (ret != len)
			fail("sendto");

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

static void *regular_thread(void *arg)
{
	char buf[128], *devname;
	int fd, ret;

	if (asprintf(&devname,
		     "/proc/xenomai/registry/rtipc/xddp/%s",
		     XDDP_PORT_LABEL) < 0)
		fail("asprintf");

	fd = open(devname, O_RDWR);
	free(devname);
	if (fd < 0)
		fail("open");

	for (;;) {
		/* Get the next message from realtime_thread2. */
		ret = read(fd, buf, sizeof(buf));
		if (ret <= 0)
			fail("read");

		/* Relay the message to realtime_thread1. */
		ret = write(fd, buf, ret);
		if (ret <= 0)
			fail("write");
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct sched_param rtparam = { .sched_priority = 42 };
	pthread_attr_t rtattr, regattr;
	sigset_t set;
	int sig;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGHUP);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	pthread_attr_init(&rtattr);
	pthread_attr_setdetachstate(&rtattr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setinheritsched(&rtattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&rtattr, SCHED_FIFO);
	pthread_attr_setschedparam(&rtattr, &rtparam);

	/* Both real-time threads have the same attribute set. */

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

	__STD(sigwait(&set, &sig));
	pthread_cancel(rt1);
	pthread_cancel(rt2);
	pthread_cancel(nrt);
	pthread_join(rt1, NULL);
	pthread_join(rt2, NULL);
	pthread_join(nrt, NULL);

	return 0;
}

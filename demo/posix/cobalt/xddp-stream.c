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
 * In addition to sending datagrams, real-time threads may stream data
 * in a byte-oriented mode through the proxy as well. This increases
 * the bandwidth and reduces the overhead, when a lot of data has to
 * flow down to the Linux domain, if keeping the message boundaries is
 * not required. The example code below illustrates such use.
 *
 * realtime_thread-------------------------------------->----------+
 *   =>  get socket                                                |
 *   =>  bind socket to port 0                                     v
 *   =>  write scattered traffic to NRT domain via sendto()        |
 *   =>  read traffic from NRT domain via recvfrom()            <--|--+
 *                                                                 |  |
 * regular_thread--------------------------------------------------+  |
 *   =>  open /dev/rtp0                                            |  ^
 *   =>  read traffic from RT domain via read()                    |  |
 *   =>  echo traffic back to RT domain via write()                +--+
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

pthread_t rt, nrt;

#define XDDP_PORT 0	/* [0..CONFIG-XENO_OPT_PIPE_NRDEV - 1] */

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

static void *realtime_thread(void *arg)
{
	struct sockaddr_ipc saddr;
	int ret, s, n = 0, len, b;
	struct timespec ts;
	size_t streamsz;
	char buf[128];

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
	 * Tell the XDDP driver that we will use the streaming
	 * capabilities on this socket. To this end, we have to
	 * specify the size of the streaming buffer, as a count of
	 * bytes. The real-time output will be buffered up to that
	 * amount, and sent as a single datagram to the NRT endpoint
	 * when fully gathered, or when another source port attempts
	 * to send data to the same endpoint. Passing a null size
	 * would disable streaming.
	 */
	streamsz = 1024; /* bytes */
	ret = setsockopt(s, SOL_XDDP, XDDP_BUFSZ,
			 &streamsz, sizeof(streamsz));
	if (ret)
		fail("setsockopt");
	/*
	 * Bind the socket to the port, to setup a proxy to channel
	 * traffic to/from the Linux domain.
	 *
	 * saddr.sipc_port specifies the port number to use.
	 */
	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = XDDP_PORT;
	ret = bind(s, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret)
		fail("bind");

	for (;;) {
		len = strlen(msg[n]);
		/*
		 * Send a datagram to the NRT endpoint via the proxy.
		 * The output is artificially scattered in separate
		 * one-byte sendings, to illustrate the use of
		 * MSG_MORE.
		 */
		for (b = 0; b < len; b++) {
			ret = sendto(s, msg[n] + b, 1, MSG_MORE, NULL, 0);
			if (ret != 1)
				fail("sendto");
		}

		printf("%s: sent (scattered) %d-bytes message, \"%.*s\"\n",
		       __FUNCTION__, len, len, msg[n]);

		/* Read back packets echoed by the regular thread */
		ret = recvfrom(s, buf, sizeof(buf), 0, NULL, 0);
		if (ret <= 0)
			fail("recvfrom");

		printf("   => \"%.*s\" echoed by peer\n", ret, buf);

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

	if (asprintf(&devname, "/dev/rtp%d", XDDP_PORT) < 0)
		fail("asprintf");

	fd = open(devname, O_RDWR);
	free(devname);
	if (fd < 0)
		fail("open");

	for (;;) {
		/* Get the next message from realtime_thread. */
		ret = read(fd, buf, sizeof(buf));
		if (ret <= 0)
			fail("read");

		/* Echo the message back to realtime_thread. */
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

	errno = pthread_create(&rt, &rtattr, &realtime_thread, NULL);
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
	pthread_cancel(rt);
	pthread_cancel(nrt);
	pthread_join(rt, NULL);
	pthread_join(nrt, NULL);

	return 0;
}

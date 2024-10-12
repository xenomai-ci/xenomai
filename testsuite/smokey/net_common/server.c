/*
 * RTnet test server loop
 *
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>

#include <sys/cobalt.h>
#include <smokey/smokey.h>
#include "smokey_net.h"
#include "smokey_net_server.h"

struct smokey_server {
	sem_t sync;
	int fds[0];
};

struct proto {
	int config_flag;
	int (*create_socket)(void);
	void (*serve)(int fd);
};

static int udp_create_socket(void);
static void udp_serve(int fd);
static int packet_dgram_socket(void);
static void packet_dgram_serve(int fd);

static const struct proto protos[] = {
	{
		.config_flag = _CC_COBALT_NET_UDP,
		.create_socket = &udp_create_socket,
		.serve = &udp_serve,
	},
	{
		.config_flag = _CC_COBALT_NET_AF_PACKET,
		.create_socket = &packet_dgram_socket,
		.serve = &packet_dgram_serve,
	},
};

static int udp_create_socket(void)
{
	struct sockaddr_in name;
	int fd;

	fd = check_unix(__RT(socket(PF_INET, SOCK_DGRAM, 0)));

	name.sin_family = AF_INET;
	name.sin_port = htons(7); /* UDP echo service */
	name.sin_addr.s_addr = htonl(INADDR_ANY);

	check_unix(__RT(bind(fd, (struct sockaddr *)&name, sizeof(name))));

	return fd;
}

static void udp_serve(int fd)
{
	struct smokey_net_payload pkt;
	struct sockaddr_in peer;
	socklen_t peer_len;
	int err;

	peer_len = sizeof(peer);
	err = check_unix(
		__RT(recvfrom(fd, &pkt, sizeof(pkt), 0,
				(struct sockaddr *)&peer, &peer_len)));

	check_unix(
		__RT(sendto(fd, &pkt, err, 0,
				(struct sockaddr *)&peer, peer_len)));
}

static int packet_dgram_socket(void)
{
	return check_unix(
		__RT(socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_802_EX1))));
}

static void packet_dgram_serve(int fd)
{
	struct smokey_net_payload pkt;
	struct sockaddr_ll peer;
	socklen_t peer_len;
	int err;

	peer_len = sizeof(peer);
	err = check_unix(
		__RT(recvfrom(fd, &pkt, sizeof(pkt), 0,
				(struct sockaddr *)&peer, &peer_len)));

	peer.sll_protocol = htons(ETH_P_802_EX1 + 1);
	check_unix(
		__RT(sendto(fd, &pkt, err, 0,
				(struct sockaddr *)&peer, peer_len)));
}

static void server_loop_cleanup(void *cookie)
{
	int *fds = cookie;
	int i;

	for (i = 0; i < ARRAY_SIZE(protos); i++)
		__RT(close(fds[i]));
	free(fds);
}

void smokey_net_server_loop(int net_config)
{
	struct sched_param prio;
	const struct proto *p;
	int i, maxfd, *fds;
	fd_set rfds;

	fds = malloc(sizeof(*fds) * ARRAY_SIZE(protos));
	if (fds == NULL)
		pthread_exit((void *)(long)-ENOMEM);

	pthread_cleanup_push(server_loop_cleanup, fds);

	FD_ZERO(&rfds);
	maxfd = 0;
	for (i = 0; i < ARRAY_SIZE(protos); i++) {
		p = &protos[i];

		if ((net_config & p->config_flag) == 0) {
			fds[i] = -1;
			continue;
		}

		fds[i] = p->create_socket();
		FD_SET(fds[i], &rfds);
		if (fds[i] > maxfd)
			maxfd = fds[i];
	}

	prio.sched_priority = 20;
	check_pthread(
		__RT(pthread_setschedparam(pthread_self(), SCHED_FIFO, &prio)));

	for (;;) {
		fd_set tfds;

		tfds = rfds;

		check_unix(__RT(select(maxfd + 1, &tfds, NULL, NULL, NULL)));

		for (i = 0; i < ARRAY_SIZE(protos); i++) {
			p = &protos[i];

			if (fds[i] < 0 || !FD_ISSET(fds[i], &tfds))
				continue;

			p->serve(fds[i]);
		}
	}

	pthread_cleanup_pop(1);
}

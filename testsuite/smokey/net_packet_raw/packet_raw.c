/*
 * RTnet AF_PACKET test
 *
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <sys/cobalt.h>
#include <smokey/smokey.h>
#include "smokey_net.h"

smokey_test_plugin(net_packet_raw,
	SMOKEY_ARGLIST(
		SMOKEY_STRING(rtnet_driver),
		SMOKEY_STRING(rtnet_interface),
		SMOKEY_INT(rtnet_rate),
		SMOKEY_INT(rtnet_duration),
	),
	"Check RTnet driver, using raw packets, measuring round trip time\n"
	"\tand packet losses,\n"
	"\tthe rtnet_driver parameter allows choosing the network driver\n"
	"\tthe rtnet_interface parameter allows choosing the network interface\n"
	"\tthe rtnet_rate parameter allows choosing the packet rate\n"
	"\tthe rtnet_duration parameter allows choosing the test duration\n"
	"\tA server on the network must run the smokey_rtnet_server program."
);

struct raw_packet_client {
	struct smokey_net_client base;
	struct ethhdr header;
};

static int
packet_raw_create_socket(struct smokey_net_client *bclient)
{
	struct raw_packet_client *client = (struct raw_packet_client *)bclient;
	struct ifreq ifr;
	int err, sock;

	sock = smokey_check_errno(
		__RT(socket(PF_PACKET, SOCK_RAW, htons(ETH_P_802_EX1 + 1))));
	if (sock < 0)
		return sock;

	memcpy(client->header.h_dest, bclient->ll_peer.sll_addr, 6);
	ifr.ifr_ifindex = bclient->ll_peer.sll_ifindex;
	err = smokey_check_errno(
		__RT(ioctl(sock, SIOCGIFNAME, &ifr)));
	if (err < 0)
		goto err;
	err = smokey_check_errno(
		__RT(ioctl(sock, SIOCGIFHWADDR, &ifr)));
	if (err < 0)
		goto err;
	memcpy(client->header.h_source, ifr.ifr_hwaddr.sa_data, 6);
	client->header.h_proto = htons(ETH_P_802_EX1);

	return sock;

  err:
	__RT(close(sock));
	return err;
}

static int
packet_raw_prepare(struct smokey_net_client *bclient,
		void *buf, size_t len, const struct smokey_net_payload *payload)
{
	struct raw_packet_client *client = (struct raw_packet_client *)bclient;

	if (len < sizeof(client->header) + sizeof(*payload))
		return -EINVAL;

	len = sizeof(client->header) + sizeof(*payload);
	memcpy(buf, &client->header, sizeof(client->header));
	memcpy(buf + sizeof(client->header), payload, sizeof(*payload));
	return len;
}

static int
packet_raw_extract(struct smokey_net_client *bclient,
		struct smokey_net_payload *payload, const void *buf, size_t len)
{
	struct raw_packet_client *client = (struct raw_packet_client *)bclient;

	if (len < sizeof(client->header) + sizeof(*payload))
		return -EINVAL;

	len = sizeof(client->header) + sizeof(*payload);
	memcpy(payload, buf + sizeof(client->header), sizeof(*payload));
	return len;
}

static int
run_net_packet_raw(struct smokey_test *t, int argc, char *const argv[])
{
	struct raw_packet_client client = {
		.base = {
			.name = "raw packets",
			.option = _CC_COBALT_NET_AF_PACKET,
			.create_socket = &packet_raw_create_socket,
			.prepare = &packet_raw_prepare,
			.extract = &packet_raw_extract,
		},
	};
	struct smokey_net_client *bclient = &client.base;

	memset(&bclient->ll_peer, '\0', sizeof(bclient->ll_peer));
	bclient->ll_peer.sll_family = AF_PACKET;
	bclient->peer_len = sizeof(bclient->ll_peer);

	return smokey_net_client_run(t, bclient, argc, argv);
}

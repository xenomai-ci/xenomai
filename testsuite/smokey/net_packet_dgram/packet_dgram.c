/*
 * RTnet AF_PACKET test
 *
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <sys/socket.h>

#include <sys/cobalt.h>
#include <smokey/smokey.h>
#include "smokey_net.h"

smokey_test_plugin(net_packet_dgram,
	SMOKEY_ARGLIST(
		SMOKEY_STRING(rtnet_driver),
		SMOKEY_STRING(rtnet_interface),
		SMOKEY_INT(rtnet_rate),
		SMOKEY_INT(rtnet_duration),
	),
	"Check RTnet driver, using cooked packets, measuring round trip time\n"
	"\tand packet losses,\n"
	"\tthe rtnet_driver parameter allows choosing the network driver\n"
	"\tthe rtnet_interface parameter allows choosing the network interface\n"
	"\tthe rtnet_rate parameter allows choosing the packet rate\n"
	"\tthe rtnet_duration parameter allows choosing the test duration\n"
	"\tA server on the network must run the smokey_rtnet_server program."
);

static int
packet_dgram_create_socket(struct smokey_net_client *client)
{
	return smokey_check_errno(
		__RT(socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_802_EX1 + 1))));
}

static int
packet_dgram_prepare(struct smokey_net_client *client,
		void *buf, size_t len, const struct smokey_net_payload *payload)
{
	if (sizeof(*payload) < len)
		len = sizeof(*payload);
	memcpy(buf, payload, len);
	return len;
}

static int
packet_dgram_extract(struct smokey_net_client *client,
		struct smokey_net_payload *payload, const void *buf, size_t len)
{
	if (sizeof(*payload) < len)
		len = sizeof(*payload);
	memcpy(payload, buf, len);
	return len;
}

static int
run_net_packet_dgram(struct smokey_test *t, int argc, char *const argv[])
{
	struct smokey_net_client client = {
		.name = "cooked packets",
		.option = _CC_COBALT_NET_AF_PACKET,
		.create_socket = &packet_dgram_create_socket,
		.prepare = &packet_dgram_prepare,
		.extract = &packet_dgram_extract,
	};

	memset(&client.ll_peer, '\0', sizeof(client.ll_peer));
	client.ll_peer.sll_family = AF_PACKET;
	client.ll_peer.sll_protocol = htons(ETH_P_802_EX1);
	client.peer_len = sizeof(client.ll_peer);

	return smokey_net_client_run(t, &client, argc, argv);
}

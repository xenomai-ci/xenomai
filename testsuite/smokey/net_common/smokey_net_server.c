/*
 * RTnet test server
 *
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtcfg_chrdev.h>
#include <sys/cobalt.h>
#include <smokey/smokey.h>
#include "smokey_net_server.h"

static const char *intf = "rteth0";

int smokey_net_server_check_inner(const char *file, int line,
					  const char *msg, int status)
{
       if (status >= 0)
	       return status;

       fprintf(stderr, "FAILED %s: returned error %d - %s\n",
	       msg, -status, strerror(-status));
       exit(EXIT_FAILURE);
}

static int rtnet_rtcfg_setup_server(void)
{
	struct rtcfg_cmd cmd;
	int fd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.args.server.period    = 1000;
	cmd.args.server.burstrate = 4;
	cmd.args.server.heartbeat = 1000;
	cmd.args.server.threshold = 2;
	cmd.args.server.flags     = 0;

	check_unix(snprintf(cmd.head.if_name, sizeof(cmd.head.if_name),
				intf, sizeof(cmd.head.if_name)));

	fd = check_unix(open("/dev/rtnet", O_RDWR));

	check_unix(ioctl(fd, RTCFG_IOC_SERVER, &cmd));

	return fd;
}

static void
rtnet_rtcfg_add_client(int fd, const char *hwaddr, const char *ipaddr)
{
	struct rtcfg_cmd cmd;
	struct ether_addr mac;
	struct in_addr ip;

	fprintf(stderr, "add client %s, mac %s\n", ipaddr, hwaddr);

	memset(&cmd, 0, sizeof(cmd));

	check_unix(snprintf(cmd.head.if_name, sizeof(cmd.head.if_name),
				intf, sizeof(cmd.head.if_name)));

	if (ether_aton_r(hwaddr, &mac) == 0) {
		fprintf(stderr, "%s is an invalid mac address\n", hwaddr);
		exit(EXIT_FAILURE);
	}

	if (check_unix(inet_aton(ipaddr, &ip)) == 0) {
		fprintf(stderr, "%s is an invalid ip address\n", ipaddr);
		exit(EXIT_FAILURE);
	}

	cmd.args.add.addr_type = RTCFG_ADDR_IP | FLAG_ASSIGN_ADDR_BY_MAC;
	cmd.args.add.ip_addr = ip.s_addr;
	cmd.args.add.timeout = 3000;
	memcpy(cmd.args.add.mac_addr, mac.ether_addr_octet,
		sizeof(mac.ether_addr_octet));

	check_unix(ioctl(fd, RTCFG_IOC_ADD, &cmd));
}

static void cleanup(int sig)
{
	struct rtcfg_cmd cmd;
	int fd;

	memset(&cmd, 0, sizeof(cmd));

	check_unix(snprintf(cmd.head.if_name, sizeof(cmd.head.if_name),
				intf, sizeof(cmd.head.if_name)));

	fd = check_unix(open("/dev/rtnet", O_RDWR));

	check_unix(ioctl(fd, RTCFG_IOC_DETACH, &cmd));

	close(fd);

	signal(sig, SIG_DFL);
	raise(sig);
}

void application_usage(void)
{
	fprintf(stderr, "%s options [ <interface> ]:\n\n"
		"Runs server for smokey network tests, on interface named "
		"<interface>\n"
		"(rtlo if unspecified)\n\n"
		"Available options:\n"
		"-f | --file <file>\t\tAnswers clients from file named <file>"
		"\n\t(uses standard input if unspecified)\n"
		"\tWhere every line contains a mac address and an IP address\n",
		get_program_name());
}

int main(int argc, char *argv[])
{
	int net_config, c, fd, err;
	FILE *input = stdin;

	check_native(cobalt_corectl(_CC_COBALT_GET_NET_CONFIG,
					&net_config, sizeof(net_config)));

	for (;;) {
		int option_index = 0;

		static struct option long_options[] = {
			{ "help", no_argument,       0, 'h', },
			{ "file", required_argument, 0, 'f', },
			{ 0,      0,                 0, 0,   },
		};

		c = getopt_long(argc, argv, "hf:", long_options, &option_index);
		if (c == -1)
			break;

		switch(c) {
		case 'h':
			application_usage();
			exit(EXIT_SUCCESS);

		case 'f':
			input = fopen(optarg, "r");
			if (input == NULL) {
				fprintf(stderr, "fopen(%s): %m\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case '?':
			application_usage();
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		if (argc - optind > 1) {
			application_usage();
			printf("\nOnly one interface argument expected\n");
			exit(EXIT_FAILURE);
		}

		intf = argv[optind];
		if (strcmp(intf, "rtlo") == 0) {
			application_usage();
			printf("\nRunning smokey_net_server on rtlo makes no sense\n");
			exit(EXIT_FAILURE);
		}
	}

	if ((net_config & _CC_COBALT_NET_CFG) == 0) {
		fprintf(stderr, "RTcfg not enabled, aborting\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "Smokey network tests server, using interface %s\n",
		intf);

	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);
	signal(SIGHUP, cleanup);

	fd = rtnet_rtcfg_setup_server();

	do {
		char *mac, *ip;

		err = fscanf(input, "%ms %ms\n", &mac, &ip);
		if (err == 2) {
			rtnet_rtcfg_add_client(fd, mac, ip);
			free(mac);
			free(ip);
		}
	} while (err != EOF);

	close(fd);

	smokey_net_server_loop(net_config);
	exit(EXIT_SUCCESS);
}

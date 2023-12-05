/*
 * Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>

#include <sys/cobalt.h>
#include <smokey/smokey.h>

#include <rtnet_chrdev.h>
#include <rtcfg_chrdev.h>
#include "smokey_net.h"
#include "smokey_net_server.h"


struct module {
	int option;
	const char *name;
};

#define TIMEOUT 10

static struct rtnet_core_cmd cmd;
static int fd;
static pthread_t loopback_server_tid;
static bool loopback_thread_created, ifup;
static struct module modules[] = {
	{
		.name = "rtnet",
	},
	{
		.name = "rtipv4",
	},
	{
		.name = "rtcfg",
	},
	{
		.option = _CC_COBALT_NET_UDP,
		.name = "rtudp",
	},
	{
		.option = _CC_COBALT_NET_AF_PACKET,
		.name = "rtpacket",
	},
	{
		.name = NULL,	/* driver */
	},
};

#define MODID_RTNET  0
#define MODID_IPV4   1
#define MODID_CFG    2
#define MODID_UDP    3
#define MODID_PACKET 4
#define MODID_DRIVER 5

static int option_to_modid(int option)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(modules); i++) {
		if (modules[i].option != option)
			continue;

		return i;
	}

	return -1;
}

static int get_info(const char *intf)
{
	int err;

	err = smokey_check_errno(
		snprintf(cmd.head.if_name, sizeof(cmd.head.if_name),
			"%s", intf));
	if (err < 0)
		return err;

	cmd.args.info.ifindex = 0;

	err = smokey_check_errno(ioctl(fd, IOC_RT_IFINFO, &cmd));
	if (err < 0)
		return err;

	return 0;
}

static int do_up(const char *intf)
{
	int err;

	snprintf(cmd.head.if_name, sizeof(cmd.head.if_name), "%s", intf);
	cmd.args.info.ifindex = 0;
	if (strcmp(intf, "rtlo")) {
		cmd.args.up.ip_addr = 0xffffffff;
		cmd.args.up.broadcast_ip = cmd.args.up.ip_addr;
	} else {
		cmd.args.up.ip_addr = htonl(0x7f000001); /* 127.0.0.1 */
		cmd.args.up.broadcast_ip = cmd.args.up.ip_addr | ~0x000000ff;
	}
	cmd.args.up.set_dev_flags = 0;
	cmd.args.up.clear_dev_flags = 0;
	cmd.args.up.dev_addr_type = 0xffff;

	err = smokey_check_errno(ioctl(fd, IOC_RT_IFUP, &cmd));
	if (err < 0)
		return err;

	return 0;
}

static int do_down(const char *intf)
{
	int err;

	snprintf(cmd.head.if_name, sizeof(cmd.head.if_name), "%s", intf);
	cmd.args.info.ifindex = 0;

	err = smokey_check_errno(ioctl(fd, IOC_RT_IFDOWN, &cmd));
	if (err < 0)
		return err;

	return 0;
}

static int smokey_net_modprobe(int modid, bool silent)
{
	struct module *m = modules + modid;

	if (modid < 0)
		return -EINVAL;

	return smokey_modprobe(m->name, silent);
}

static int smokey_net_rmmod(int modid)
{
	struct module *m = modules + modid;

	return smokey_rmmod(m->name);
}

static int smokey_net_setup_rtcfg_client(const char *intf, int net_config)
{
	struct rtcfg_cmd cmd;
	int err;

	if ((net_config & _CC_COBALT_NET_CFG) == 0)
		return -ENOSYS;

	err = smokey_net_modprobe(MODID_CFG, false);
	if (err < 0)
		return err;

	memset(&cmd, 0, sizeof(cmd));
	err = smokey_check_errno(
		snprintf(cmd.head.if_name, sizeof(cmd.head.if_name),
			intf, IFNAMSIZ));
	if (err < 0)
		return err;

	cmd.args.client.timeout      = 10000;
	cmd.args.client.max_stations = 32;
	cmd.args.client.buffer_size = 0;

	err = smokey_check_errno(ioctl(fd, RTCFG_IOC_CLIENT, &cmd));
	if (err < 0)
		return err;

	cmd.args.announce.timeout     = 5000;
	cmd.args.announce.buffer_size = 0;
	cmd.args.announce.flags       = 0;
	cmd.args.announce.burstrate   = 4;

	err = smokey_check_errno(ioctl(fd, RTCFG_IOC_ANNOUNCE, &cmd));
	if (err < 0)
		return err;

	return 0;
}

static int smokey_net_teardown_rtcfg(const char *intf)
{
	struct rtcfg_cmd cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));
	err = smokey_check_errno(
		snprintf(cmd.head.if_name, sizeof(cmd.head.if_name),
			intf, IFNAMSIZ));
	if (err < 0)
		return err;

	/*
	 * We may or may not be acting as a server; don't check the
	 * status.
	 */
	ioctl(fd, RTCFG_IOC_DETACH, &cmd);

	return smokey_net_rmmod(MODID_CFG);
}

static int find_peer(const char *intf, void *vpeer)
{
	struct sockaddr_in *in_peer = vpeer;
	struct sockaddr_ll *ll_peer = vpeer;
	struct sockaddr *peer = vpeer;
	char buf[4096];
	char hash[3];
	char dest[16];
	char mac[18];
	char dev[16];
	FILE *f;
	int err;

	f = fopen("/proc/rtnet/ipv4/host_route", "r");
	if (!f) {
		err = -errno;
		smokey_warning("open(/proc/rtnet/ipv4/host_route): %s",
			strerror(-err));
		return err;
	}

	/* Skip first line */
	if (!fgets(buf, sizeof(buf), f)) {
		err = -errno;
		smokey_warning("fgets(/proc/rtnet/ipv4/host_route): %s",
			strerror(-err));
		goto err;
	}

	for(;;) {
		err = fscanf(f, "%s\t%s\t%s\t%s\n", hash, dest, mac, dev);
		if (err == EOF) {
			smokey_warning("No peer found\n");
			err = -ENOENT;
			goto err;
		}
		if (err < 4) {
			smokey_warning("Error parsing"
				" /proc/rtnet/ipv4/host_route\n");
			err = -EINVAL;
			goto err;
		}

		if (strcmp(dev, intf))
			continue;

		if (strcmp(mac, "FF:FF:FF:FF:FF:FF") == 0)
			continue;

		if (strcmp(dest, "255.255.255.255") == 0)
			continue;

		if (strcmp(dest, "0.0.0.0") == 0)
			continue;

		break;
	}

	switch(peer->sa_family) {
	case AF_INET:
		err = smokey_check_errno(
			inet_pton(AF_INET, dest, &in_peer->sin_addr));
		if (err < 0)
			goto err;
		break;

	case AF_PACKET: {
		const unsigned eth_alen = 6;
		struct ether_addr eth;
		struct ifreq ifr;
		int sock;

		ll_peer->sll_halen = eth_alen;
		if (ether_aton_r(mac, &eth) == 0) {
			err = -errno;
			smokey_warning("ether_aton_r(%s): %m", mac);
			goto err;
		}

		memcpy(&ll_peer->sll_addr[0], eth.ether_addr_octet, eth_alen);

		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", dev);

		err = smokey_check_errno(
			__RT(socket(PF_PACKET, SOCK_DGRAM, 0)));
		if (err < 0)
			goto err;
		sock = err;

		err = smokey_check_errno(__RT(ioctl(sock, SIOCGIFINDEX, &ifr)));
		sock = smokey_check_errno(__RT(close(sock)));
		if (err < 0)
			goto err;
		if (sock < 0) {
			err = sock;
			goto err;
		}

		ll_peer->sll_ifindex = ifr.ifr_ifindex;
	}
	}

	err = 0;
  err:
	fclose(f);
	return err;
}

int smokey_net_server_check_inner(const char *file, int line,
				     const char *msg, int status)
{
       if (status >= 0)
	       return status;

       __smokey_warning(file, line, "%s: %s", msg, strerror(-status));
       pthread_exit((void *)(long)status);
}

static void *loopback_server(void *cookie)
{
	int net_config = (long)cookie;
	smokey_net_server_loop(net_config);
	return NULL;
}

int smokey_net_setup(const char *driver, const char *intf, int tested_config,
		void *vpeer)
{
	int net_config, err, i, err_teardown;
	struct sockaddr_in *in_peer = vpeer;
	struct sockaddr *peer = vpeer;

	/*
	 * Main module needs to be loaded in order to use
	 * _CC_COBALT_GET_NET_CONFIG.
	 */
	smokey_net_modprobe(MODID_RTNET, true);

	err = cobalt_corectl(_CC_COBALT_GET_NET_CONFIG,
			&net_config, sizeof(net_config));
	if (err == -EINVAL)
		return -ENOSYS;
	if (err < 0)
		return err;

	if ((net_config & (_CC_COBALT_NET | _CC_COBALT_NET_IPV4))
		!= (_CC_COBALT_NET | _CC_COBALT_NET_IPV4))
		return -ENOSYS;

	if ((net_config & tested_config) == 0)
		return -ENOSYS;

	modules[MODID_DRIVER].name = driver;
	err = smokey_net_modprobe(MODID_DRIVER, false);
	if (err < 0)
		return err;

	err = smokey_net_modprobe(MODID_IPV4, false);
	if (err < 0)
		return err;

	err = smokey_net_modprobe(option_to_modid(tested_config), false);
	if (err < 0)
		return err;

	fd = smokey_check_errno(open("/dev/rtnet", O_RDWR));
	if (fd < 0)
		return fd;

	err = get_info(intf);
	if (err < 0)
		goto err;

	if ((cmd.args.info.flags & IFF_UP) == 0) {
		err = do_up(intf);
		if (err < 0)
			goto err;
		ifup = true;
	}

	smokey_trace("Waiting for interface %s to be running", intf);

	for (i = 0; i < 30; i++) {
		err = get_info(intf);
		if (err < 0)
			goto err;

		if ((cmd.args.info.flags & (IFF_UP | IFF_RUNNING))
			== (IFF_UP | IFF_RUNNING))
			goto running;

		sleep(1);
	}

	smokey_warning("Interface is not running,"
		" giving up (cable unplugged?)");
	err = -ETIMEDOUT;
	goto err;

running:
	err = get_info(intf);
	if (err < 0)
		goto err;

	if (cmd.args.info.ip_addr == 0) {
		err = smokey_net_setup_rtcfg_client(intf, net_config);
		if (err < 0)
			goto err;
	}

	if (strcmp(driver, "rt_loopback") == 0) {
		err  = smokey_check_status(
			__RT(pthread_create(&loopback_server_tid, NULL,
						loopback_server,
						(void *)(long)tested_config)));
		if (err < 0)
			goto err;
		loopback_thread_created = true;
	}

	switch (peer->sa_family) {
	case AF_INET:
		if (in_peer->sin_addr.s_addr == htonl(INADDR_ANY) &&
			strcmp(driver, "rt_loopback") == 0) {
			in_peer->sin_addr.s_addr = cmd.args.info.ip_addr;
			break;
		}

		/* Fallthrough wanted */
	case AF_PACKET:
		err = find_peer(intf, vpeer);
		if (err < 0)
			goto err;
	}

	close(fd);
	return 0;

  err:
	close(fd);

	err_teardown = smokey_net_teardown(driver, intf, tested_config);
	if (err == 0)
		err = err_teardown;

	return err;
}

int smokey_net_teardown(const char *driver, const char *intf, int tested_config)
{
	int err = 0, tmp;

	if (loopback_thread_created) {
		void *status;

		pthread_cancel(loopback_server_tid); /* May fail */
		tmp = smokey_check_errno(
			pthread_join(loopback_server_tid, &status));
		if (err == 0)
			err = tmp;
		if (err == 0 && status != PTHREAD_CANCELED)
			err = (long)status;
	}

	tmp = smokey_check_errno(open("/dev/rtnet", O_RDWR));
	if (tmp >= 0) {
		fd = tmp;

		if (strcmp(driver, "rt_loopback")) {
			tmp = smokey_net_teardown_rtcfg(intf);
			if (err == 0)
				err = tmp;
		}

		if (ifup) {
			tmp = do_down(intf);
			if (err == 0)
				err = tmp;
		}

		close(fd);
	} else
		err = tmp;

	tmp = smokey_net_rmmod(option_to_modid(tested_config));
	if (err == 0)
		err = tmp;

	tmp = smokey_net_rmmod(MODID_DRIVER);
	if (err == 0)
		err = tmp;

	tmp = smokey_net_rmmod(MODID_IPV4);
	if (err == 0)
		err = tmp;

	tmp = smokey_net_rmmod(MODID_RTNET);
	if (err == 0)
		err = tmp;

	return err;
}

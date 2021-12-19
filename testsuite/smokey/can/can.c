/*
 * RT Socket CAN test
 *
 * Copyright (C) 2021 Dario Binacchi <dariobin@libero.it>
 * Copyright (c) Siemens AG, 2022
 *
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <rtdm/can.h>
#include <sys/cobalt.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <smokey/smokey.h>
#include <stdbool.h>

#define CAN_MOD                        "xeno_can"
#define CAN_VIRT_MOD                   "xeno_can_virt"

struct smokey_can_virt_ctx {
	int s;
	unsigned int baud;
	unsigned int id;
	unsigned int cnt_snd;
	unsigned int cnt_rcv;
	unsigned int cnt_limit;
	bool recv_run;
	pthread_cond_t recv_run_cond;
	pthread_mutex_t recv_run_mutex;
	const char *recv_ifname;
	nanosecs_rel_t recv_timeout;
	const char *send_ifname;
};

smokey_test_plugin(can, SMOKEY_NOARGS,
	"Check RT Socket CAN stack"
);

static void can_virt_trace_frame(bool send, struct can_frame *frame)
{
	char buf[128];
	int i, len = 0, n = sizeof(buf);

	if (!frame || smokey_verbose_mode <= 1)
		return;

	len += snprintf(buf, n, "%s, id: %d, dlc: %d, data:",
			send ? "send" : "recv", frame->can_id, frame->can_dlc);
	n = sizeof(buf) - len;
	for (i = 0; i < frame->can_dlc; i++) {
		len += snprintf(buf + len, n, " %02x", frame->data[i]);
		n = sizeof(buf) - len;
	}

	smokey_trace("%s", buf);
}

static int can_virt_send(struct smokey_can_virt_ctx *ctx)
{
	struct can_ifreq ifr;
	struct sockaddr_can to_addr;
	struct can_frame frame = {0};
	int ret;

	namecpy(ifr.ifr_name, ctx->send_ifname);
	ret = smokey_check_errno(__RT(ioctl(ctx->s, SIOCGIFINDEX, &ifr)));
	if (ret < 0)
		goto finally;

	memset(&to_addr, 0, sizeof(to_addr));
	to_addr.can_ifindex = ifr.ifr_ifindex;
	to_addr.can_family = AF_CAN;

	frame.can_id = ctx->id;
	frame.can_dlc = sizeof(ctx->cnt_snd);
	memcpy(&frame.data[0], &ctx->cnt_snd, sizeof(ctx->cnt_snd));
	can_virt_trace_frame(true, &frame);
	ret = smokey_check_errno(__RT(sendto(ctx->s, (void *)&frame,
					     sizeof(can_frame_t), 0,
					     (struct sockaddr *)&to_addr,
					     sizeof(to_addr))));
	if (ret < 0)
		goto finally;

	ret = 0;
finally:
	return ret;
}

static int can_virt_recv(struct smokey_can_virt_ctx *ctx)
{
	struct sockaddr_can addr;
	socklen_t addrlen = sizeof(addr);
	struct can_frame frame;
	unsigned int cnt;
	int ret;

	ret = smokey_check_errno(__RT(recvfrom(ctx->s, (void *)&frame,
					       sizeof(can_frame_t), 0,
					       (struct sockaddr *)&addr,
					       &addrlen)));
	if (ret < 0)
		return ret;

	can_virt_trace_frame(false, &frame);

	if (!smokey_assert(frame.can_dlc == sizeof(ctx->cnt_rcv)))
		return -EINVAL;

	if (!smokey_assert(frame.can_id == ctx->id))
		return -EINVAL;

	memcpy(&cnt, &frame.data[0], sizeof(cnt));
	if (!smokey_assert(cnt == ctx->cnt_rcv))
		return -EINVAL;
	ctx->cnt_rcv++;

	return 0;
}

static void *recv_task(void *cookie)
{
	struct smokey_can_virt_ctx *ctx = (struct smokey_can_virt_ctx *)cookie;
	struct sched_param prio;
	struct can_ifreq ifr;
	struct sockaddr_can recv_addr;
	int ret;

	pthread_mutex_lock(&ctx->recv_run_mutex);
	prio.sched_priority = 22;
	ret = smokey_check_status(pthread_setschedparam(pthread_self(),
							SCHED_FIFO, &prio));

	namecpy(ifr.ifr_name, ctx->recv_ifname);
	ret = smokey_check_errno(__RT(ioctl(ctx->s, SIOCGIFINDEX, &ifr)));
	if (ret < 0)
		goto finally;

	recv_addr.can_family = AF_CAN;
	recv_addr.can_ifindex = ifr.ifr_ifindex;
	ret = smokey_check_errno(__RT(bind(ctx->s,
					   (struct sockaddr *)&recv_addr,
					   sizeof(struct sockaddr_can))));
	if (ret < 0)
		goto finally;

	ret = smokey_check_errno(__RT(ioctl(ctx->s, RTCAN_RTIOC_RCV_TIMEOUT,
					   &ctx->recv_timeout)));
	if (ret < 0)
		goto finally;

	ctx->recv_run = true;
	pthread_cond_signal(&ctx->recv_run_cond);
	pthread_mutex_unlock(&ctx->recv_run_mutex);

	if (ret < 0)
		goto finally;

	for (;;) {
		ret = can_virt_recv(ctx);
		if (ret)
			break;

		if (ctx->cnt_rcv == ctx->cnt_limit)
			break;
	}

finally:
	ctx->recv_run = false;
	pthread_exit((void *)(long)ret);
}


static void *loop_task(void *cookie)
{
	struct smokey_can_virt_ctx *ctx = (struct smokey_can_virt_ctx *)cookie;
	struct timespec ts;
	struct sched_param prio;
	pthread_t tid;
	void *tstatus;
	int ret;

	prio.sched_priority = 20;
	ret = smokey_check_status(pthread_setschedparam(pthread_self(),
							SCHED_FIFO, &prio));
	if (ret < 0)
		pthread_exit((void *)(long)ret);

	pthread_mutex_lock(&ctx->recv_run_mutex);
	ret = smokey_check_status(__RT(pthread_create(&tid, NULL, recv_task,
						      ctx)));
	if (ret < 0)
		pthread_exit((void *)(long)ret);

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	ret = smokey_check_status(pthread_cond_timedwait(&ctx->recv_run_cond,
							 &ctx->recv_run_mutex,
							 &ts));
	if (ret < 0)
		goto finally;

	smokey_trace("recv task started");
	for (; ctx->recv_run && ctx->cnt_snd <= ctx->cnt_limit;
	     ctx->cnt_snd++) {
		ret = can_virt_send(ctx);
		if (ret)
			goto finally;
	}

finally:
	pthread_join(tid, &tstatus);
	if (!ret)
		ret = (int)(long)tstatus;

	pthread_exit((void *)(long)ret);
}

static int can_virt_config(int s, const char *ifname, int baudrate, int mode)
{
	struct can_ifreq ifr;
	int ret;

	namecpy(ifr.ifr_name, ifname);
	ret = smokey_check_errno(__RT(ioctl(s, SIOCGIFINDEX, &ifr)));
	if (ret < 0)
		return ret;

	if (baudrate > 0) {
		ifr.ifr_ifru.baudrate = baudrate;
		ret = smokey_check_errno(__RT(ioctl(s, SIOCSCANBAUDRATE,
						    &ifr)));
		if (ret < 0)
			return ret;
	}

	if (mode >= 0) {
		ifr.ifr_ifru.mode = mode;
		ret = smokey_check_errno(__RT(ioctl(s, SIOCSCANMODE, &ifr)));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int can_virt_teardown(struct smokey_can_virt_ctx *ctx)
{
	int ret = 0, r;

	r = can_virt_config(ctx->s, ctx->send_ifname, -1, CAN_MODE_STOP);
	if (ret == 0)
		ret = r;

	smokey_trace("config: ifname: %s, mode: down", ctx->send_ifname);

	r = can_virt_config(ctx->s, ctx->recv_ifname, -1, CAN_MODE_STOP);
	if (ret == 0)
		ret = r;

	smokey_trace("config: ifname: %s, mode: down", ctx->recv_ifname);

	__RT(close(ctx->s));
	r = smokey_rmmod(CAN_VIRT_MOD);
	if (ret == 0)
		ret = r;

	r = smokey_rmmod(CAN_MOD);
	if (ret == 0)
		ret = r;

	pthread_cond_destroy(&ctx->recv_run_cond);
	pthread_mutex_destroy(&ctx->recv_run_mutex);
	return ret;
}

static int can_virt_setup(struct smokey_can_virt_ctx *ctx)
{
	int cfg, ret;

	pthread_mutex_init(&ctx->recv_run_mutex, NULL);
	pthread_cond_init(&ctx->recv_run_cond, NULL);

	/*
	 * Main module needs to be loaded in order to use
	 * _CC_COBALT_GET_CAN_CONFIG.
	 */
	smokey_modprobe(CAN_MOD, true);

	ret = cobalt_corectl(_CC_COBALT_GET_CAN_CONFIG, &cfg, sizeof(cfg));
	if (ret == -EINVAL) {
		ret = -ENOSYS;
		goto corectl_err;
	}

	if (ret < 0)
		goto corectl_err;

	if ((cfg & _CC_COBALT_CAN) == 0) {
		ret = -ENOSYS;
		goto corectl_err;
	}

	smokey_modprobe(CAN_VIRT_MOD, true);

	ret = smokey_check_errno(__RT(socket(PF_CAN, SOCK_RAW, CAN_RAW)));
	if (ret < 0)
		goto sock_err;

	ctx->s = ret;

	ret = can_virt_config(ctx->s, ctx->recv_ifname, ctx->baud,
			      CAN_MODE_START);
	if (ret)
		goto recv_cfg_err;

	smokey_trace("config: ifname: %s, baud: %d, mode: up", ctx->recv_ifname,
		     ctx->baud);

	ret = can_virt_config(ctx->s, ctx->send_ifname, ctx->baud,
			      CAN_MODE_START);
	if (ret)
		goto send_cfg_err;

	smokey_trace("config: ifname: %s, baud: %d, mode: up", ctx->send_ifname,
		     ctx->baud);

	return 0;

send_cfg_err:
	can_virt_config(ctx->s, ctx->send_ifname, -1, CAN_MODE_STOP);
recv_cfg_err:
	__RT(close(ctx->s));
sock_err:
	smokey_rmmod(CAN_VIRT_MOD);
corectl_err:
	smokey_rmmod(CAN_MOD);
	pthread_mutex_destroy(&ctx->recv_run_mutex);
	pthread_cond_destroy(&ctx->recv_run_cond);
	return ret;
}

static int run_can(struct smokey_test *t, int argc, char *const argv[])
{
	struct smokey_can_virt_ctx ctx = {
		.baud = 1250000,
		.id = 19,
		.cnt_snd = 1,
		.cnt_rcv = 1,
		.cnt_limit = 1000,
		.recv_ifname = "rtcan0",
		.recv_timeout = 1000000000, /* 1 s */
		.send_ifname = "rtcan1",
	};
	pthread_t tid;
	void *status;
	int ret, r;

	ret = can_virt_setup(&ctx);
	if (ret < 0)
		return ret;

	ret = smokey_check_status(__RT(pthread_create(&tid, NULL, loop_task,
						      &ctx)));
	if (ret == 0) {
		pthread_join(tid, &status);
		ret = (int)(long)status;
	}

	r = can_virt_teardown(&ctx);
	if (ret == 0)
		ret = r;

	return ret;
}

#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "smokey_net.h"

static int duration = 10;
static int rate = 1000;
static const char *driver = "rt_loopback";
static const char *intf;
static pthread_t tid;
static unsigned long long glost, glate;

static int rcv_packet(struct smokey_net_client *client, int sock, unsigned seq,
		      struct timespec *next_shot, bool last, int *linesout)
{
	static unsigned long long gmin = ~0ULL, gmax = 0, gsum = 0, gcount = 0;
	static unsigned long long min = ~0ULL, max = 0, sum = 0, count = 0,
		lost = 0, late = 0;
	static struct timespec last_print;
	struct smokey_net_payload payload;
	struct timeval timeout;
	struct timespec now;
	char packet[256];
	long long diff;
	fd_set set;
	int err;

	FD_ZERO(&set);
	FD_SET(sock, &set);

	err = smokey_check_errno(
		__RT(clock_gettime(CLOCK_MONOTONIC, &now)));
	if (err < 0)
		return err;

	diff = next_shot->tv_sec * 1000000000ULL + next_shot->tv_nsec
		- (now.tv_sec * 1000000000ULL + now.tv_nsec);
	if (diff < 0)
		diff = 0;

	timeout.tv_sec = diff / 1000000000;
	timeout.tv_usec = (diff % 1000000000 + 500) / 1000;

	err = smokey_check_errno(
		__RT(select(sock + 1, &set, NULL, NULL, &timeout)));
	if (err < 0)
		return err;

	if (err == 0) {
		if (seq)
			++lost;
		err = -ETIMEDOUT;
		goto print_stats;
	}

	err = smokey_check_errno(
		__RT(recv(sock, packet, sizeof(packet), 0)));
	if (err < 0)
		return err;

	err = client->extract(client, &payload, packet, err);
	if (err < 0)
		return err;

	err = smokey_check_errno(
		__RT(clock_gettime(CLOCK_MONOTONIC, &now)));
	if (err < 0)
		return err;

	diff = now.tv_sec * 1000000000ULL + now.tv_nsec
		- (payload.ts.tv_sec * 1000000000ULL
			+ payload.ts.tv_nsec);
	if (diff < min)
		min = diff;
	if (diff > max)
		max = diff;
	sum += diff;
	++count;

	err = 0;
	if (payload.seq != seq) {
		++late;
		err = -EAGAIN;
	}

  print_stats:
	if (seq == 1 && !last_print.tv_sec) {
		last_print = now;
		if (last_print.tv_nsec < 1000000000 / rate) {
			last_print.tv_nsec += 1000000000;
			last_print.tv_sec--;
		}
		last_print.tv_nsec -= 1000000000 / rate;
	}

	diff = now.tv_sec * 1000000000ULL + now.tv_nsec
		- (last_print.tv_sec * 1000000000ULL
			+ last_print.tv_nsec);

	if (diff < 1000000000LL && (!last || (!count && !lost)))
		return err;

	if (min < gmin)
		gmin = min;
	if (max > gmax)
		gmax = max;
	gsum += sum;
	gcount += count;
	glost += lost - late;
	glate += late;

	if (((*linesout)++ % 20) == 0) {
		smokey_trace("\n   %-7s%6s%8s%8s%8s%8s%8s%10s",
			     "PPS", "LOST", "LATE", "MIN", "MAX",
			     "BEST", "AVG", "WORST");
		smokey_trace("------------------------------------------------------------------");
	}

	smokey_trace("%8.2f  %6Ld  %6Ld     %.03g     %.03g     %.03g     %.03g     %.03g",
		count / (diff / 1000000000.0),
		glost,
		glate,
		count ? min / 1000.0 : 0,
		count ? max / 1000.0 : 0,
		gcount ? gmin / 1000.0 : 0,
		gcount ? (gsum / (double)gcount) / 1000 : 0,
		gcount ? gmax / 1000.0 : 0);

	min = ~0ULL;
	max = 0;
	sum = 0;
	count = 0;
	lost = 0;
	late = 0;
	last_print = now;

	return err;
}

static int smokey_net_client_loop(struct smokey_net_client *client)
{
	struct smokey_net_payload payload;
	int sock, err, linesout = 0;
	struct timespec next_shot;
	struct sched_param prio;
	char packet[256];
	long long limit;

	sock = client->create_socket(client);
	if (sock < 0)
		return sock;

	prio.sched_priority = 20;
	err = smokey_check_status(
		pthread_setschedparam(pthread_self(), SCHED_FIFO, &prio));
	if (err < 0)
		return err;

	err = smokey_check_errno(
		__RT(clock_gettime(CLOCK_MONOTONIC, &next_shot)));
	if (err < 0)
		goto err;

	smokey_trace("\nPPS, LOST, LATE: packet count");
	smokey_trace("MIN, MAX, BEST, AVG, WORST: microseconds");

	limit = (long long)rate * duration;
	for (payload.seq = 1;
	     limit <= 0 || payload.seq < limit + 1; payload.seq++) {
		unsigned seq = payload.seq;

		next_shot.tv_nsec += 1000000000 / rate;
		if (next_shot.tv_nsec > 1000000000) {
			next_shot.tv_nsec -= 1000000000;
			next_shot.tv_sec++;
		}

		err = smokey_check_errno(
			__RT(clock_gettime(CLOCK_MONOTONIC, &payload.ts)));
		if (err < 0)
			goto err;

		err = client->prepare(client, packet, sizeof(packet), &payload);
		if (err < 0)
			goto err;

		err = smokey_check_errno(
			__RT(sendto(sock, packet, err, 0,
					&client->peer, client->peer_len)));
		if (err < 0)
			goto err;

		do {
			err = rcv_packet(client, sock, seq, &next_shot,
					 payload.seq == limit, &linesout);
			if (!err)
				seq = 0;
		} while (err != -ETIMEDOUT);
	}

	if (glost || glate)
		fprintf(stderr, "RTnet %s test failed", client->name);
	if (glost) {
		if (glost == limit)
			fprintf(stderr, ", all packets lost"
				" (is smokey_net_server running ?)");
		else
			fprintf(stderr, ", %Lu packets lost (%g %%)",
				glost, 100.0 * glost / limit);
	}
	if (glate)
		fprintf(stderr, ", %Lu overruns", glate);
	if (glost || glate)
		fputc('\n', stderr);
	err = glost || glate ? -EPROTO : 0;

  err:
	sock = smokey_check_errno(__RT(close(sock)));
	if (err == 0)
		err = sock;

	return err;
}

static void *trampoline(void *cookie)
{
	int err = smokey_net_client_loop(cookie);
	pthread_exit((void *)(long)err);
}

int smokey_net_client_run(struct smokey_test *t,
			struct smokey_net_client *client,
			int argc, char *const argv[])
{
	int err, err_teardown;
	void *status;

	smokey_parse_args(t, argc, argv);

	if (SMOKEY_ARG_ISSET(*t, rtnet_driver))
		driver = SMOKEY_ARG_STRING(*t, rtnet_driver);

	if (SMOKEY_ARG_ISSET(*t, rtnet_interface))
		intf = SMOKEY_ARG_STRING(*t, rtnet_interface);

	if (SMOKEY_ARG_ISSET(*t, rtnet_duration))
		duration = SMOKEY_ARG_INT(*t, rtnet_duration);

	if (SMOKEY_ARG_ISSET(*t, rtnet_rate)) {
		rate = SMOKEY_ARG_INT(*t, rtnet_rate);
		if (rate == 0) {
			smokey_warning("rate can not be null");
			return -EINVAL;
		}
	}

	if (!intf)
		intf = strcmp(driver, "rt_loopback") ? "rteth0" : "rtlo";

	smokey_trace("Configuring interface %s (driver %s) for RTnet %s test",
		intf, driver, client->name);

	err = smokey_net_setup(driver, intf, client->option, &client->peer);
	if (err < 0)
		return err;

	smokey_trace("Running RTnet %s test on interface %s",
		client->name, intf);

	err = smokey_check_status(
		__RT(pthread_create(&tid, NULL, trampoline, client)));
	if (err < 0)
		return err;

	err = smokey_check_status(pthread_join(tid, &status));
	if (err < 0)
		return err;

	err = (int)(long)status;

	err_teardown = smokey_net_teardown(driver, intf, client->option);
	if (err == 0)
		err = err_teardown;

	return err;
}

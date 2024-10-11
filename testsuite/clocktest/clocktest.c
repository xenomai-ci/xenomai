/*
 * Copyright (C) 2007 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <error.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <boilerplate/ancillaries.h>
#include <boilerplate/atomic.h>
#include <xeno_config.h>

/*
 * We can't really trust POSIX headers to check for features, since
 * some archs may not implement all of the declared uClibc POSIX
 * features (e.g. NIOS2).
 */
#ifdef HAVE_PTHREAD_SPIN_LOCK
pthread_spinlock_t lock;
#define init_lock(lock)				pthread_spin_init(lock, 0)
#define acquire_lock(lock)			pthread_spin_lock(lock)
#define release_lock(lock)			pthread_spin_unlock(lock)
#else
pthread_mutex_t lock;
#define init_lock(lock)				pthread_mutex_init(lock, NULL)
#define acquire_lock(lock)			pthread_mutex_lock(lock)
#define release_lock(lock)			pthread_mutex_unlock(lock)
#endif

static uint64_t last_common = 0;
static clockid_t clock_id = CLOCK_REALTIME;
static cpu_set_t cpu_realtime_set, cpu_online_set;

struct per_cpu_data {
	uint64_t first_tod, first_clock;
	int first_round;
	int64_t offset;
	double drift;
	unsigned long warps;
	uint64_t max_warp;
	pthread_t thread;
} *per_cpu_data;

static inline uint64_t read_clock(clockid_t clock_id)
{
	struct timespec ts;
	int res;

	res = clock_gettime(clock_id, &ts);
	if (res != 0) {
		fprintf(stderr, "clock_gettime failed for clock id %d\n",
			clock_id);
		exit(-1);
	}
	return ts.tv_nsec + ts.tv_sec * 1000000000ULL;
}

static inline uint64_t read_reference_clock(void)
{
	struct timespec ts;

	/*
	 * Make sure we do not pick the vsyscall variant. It won't
	 * switch us into secondary mode and can easily deadlock.
	 */
#if __USE_TIME_BITS64 && __TIMESIZE == 32
	syscall(SYS_clock_gettime64, CLOCK_REALTIME, &ts);
#else
	syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
#endif

	return ts.tv_nsec + ts.tv_sec * 1000000000ULL;
}

static void check_reference(struct per_cpu_data *per_cpu_data)
{
	uint64_t clock_val[10], tod_val[10];
	int64_t delta, min_delta;
	int i, idx;

	for (i = 0; i < 10; i++) {
		tod_val[i] = read_reference_clock();
		clock_val[i] = read_clock(clock_id);
	}

	min_delta = tod_val[1] - tod_val[0];
	idx = 1;

	for (i = 2; i < 10; i++) {
		delta = tod_val[i] - tod_val[i-1];
		if (delta < min_delta) {
			min_delta = delta;
			idx = i;
		}
	}

	if (per_cpu_data->first_round) {
		per_cpu_data->first_round = 0;

		per_cpu_data->first_tod = tod_val[idx];
		per_cpu_data->first_clock = clock_val[idx];
	} else
		per_cpu_data->drift =
			(clock_val[idx] - per_cpu_data->first_clock) /
			(double)(tod_val[idx] - per_cpu_data->first_tod) - 1;

	per_cpu_data->offset = clock_val[idx] - tod_val[idx];
}

static void check_time_warps(struct per_cpu_data *per_cpu_data)
{
	int i;
	uint64_t last, now;
	int64_t incr;

	for (i = 0; i < 100; i++) {
		acquire_lock(&lock);
		now = read_clock(clock_id);
		last = last_common;
		last_common = now;
		release_lock(&lock);

		incr = now - last;
		if (incr < 0) {
			acquire_lock(&lock);
			per_cpu_data->warps++;
			if (-incr > per_cpu_data->max_warp)
				per_cpu_data->max_warp = -incr;
			release_lock(&lock);
		}
	}
}

static void *cpu_thread(void *arg)
{
	int cpuid = (long)arg;
	struct sched_param param = { .sched_priority = 1 };
	struct timespec delay = { 0, 0 };
	cpu_set_t cpu_set;

	srandom(read_reference_clock());

	CPU_ZERO(&cpu_set);
	CPU_SET(cpuid, &cpu_set);
	sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

	while (1) {
		check_reference(&per_cpu_data[cpuid]);

		check_time_warps(&per_cpu_data[cpuid]);

		delay.tv_nsec = 1000000 + random() * (100000.0 / RAND_MAX);
		nanosleep(&delay, NULL);
	}

	return NULL;
}

static void sighand(int signal)
{
	exit(0);
}

static clockid_t resolve_clock_name(const char *name,
				    const char **real_name, int ext)
{
	char *path, buf[BUFSIZ];
	clockid_t clock_id;
	FILE *fp;
	int id;

	*real_name = name;

	if (!ext) {
		if (isdigit(*name)) {
			clock_id = (clockid_t)atoi(name);
			switch (clock_id) {
			case CLOCK_REALTIME:
				*real_name = "CLOCK_REALTIME";
				break;
			case CLOCK_MONOTONIC:
				*real_name = "CLOCK_MONOTONIC";
				break;
			case CLOCK_MONOTONIC_RAW:
				*real_name = "CLOCK_MONOTONIC_RAW";
				break;
			default:
				error(1, EINVAL, "bad built-in clock id '%d'",
				      clock_id);
			}
			return clock_id;
		}
		if (strcmp(name, "CLOCK_REALTIME") == 0)
			return CLOCK_REALTIME;
		if (strcmp(name, "CLOCK_MONOTONIC") == 0)
			return CLOCK_MONOTONIC;
		if (strcmp(name, "CLOCK_MONOTONIC_RAW") == 0)
			return CLOCK_MONOTONIC_RAW;
		if (strcmp(name, "coreclk") == 0) {
			/* Read the core clock as CLOCK_MONOTONIC_RAW */
			*real_name = "CLOCK_MONOTONIC_RAW";
			return CLOCK_MONOTONIC_RAW;
		}
	}

	if (!isdigit(*name)) {
		if (asprintf(&path, "/proc/xenomai/clock/%s", name) < 0)
			error(1, ENOMEM, "resolve_clock_name");
		fp = fopen(path, "r");
		if (fp == NULL)
			error(1, EINVAL, "bad extension clock name '%s'", name);
		if (fgets(buf, sizeof(buf), fp) == NULL)
			error(1, EIO, "cannot read %s", path);
		free(path);
		if (sscanf(buf, "%*[ ]id: %d\n", &id) != 1)
			error(1, EINVAL, "bad extension clock name '%s'", name);
		return __COBALT_CLOCK_EXT(id);
	}

	id = atoi(name);
	if (!__COBALT_CLOCK_EXT_P(id) ||
	    __COBALT_CLOCK_EXT_INDEX(id) >= COBALT_MAX_EXTCLOCKS)
		error(1, EINVAL, "bad extension clock id '%d'", id);
		
	*real_name = "CLOCK_UNKNOWN";

	return __COBALT_CLOCK_EXT(id);
}

int main(int argc, char *argv[])
{
	const char *clock_name = NULL, *real_clock_name = "CLOCK_REALTIME";
	int max_cpu, cpus;
	int i;
	int c;
	int ext = 0;

	while ((c = getopt(argc, argv, "C:ET:")) != EOF)
		switch (c) {
		case 'C':
			clock_name = optarg;
			break;

		case 'E':
			ext = 1;
			break;

		case 'T':
			alarm(atoi(optarg));
			break;

		default:
			fprintf(stderr, "usage: clocktest [options]\n"
				"  [-C <clock_id|clock_name>]   # tested clock, defaults to CLOCK_REALTIME\n"
				"  [-E]                         # -C specifies extension clock\n"
				"  [-T <test_duration_seconds>] # default=0, so ^C to end\n");
			exit(2);
		}

	if (clock_name)
		clock_id = resolve_clock_name(clock_name, &real_clock_name, ext);

	signal(SIGALRM, sighand);

	init_lock(&lock);

	if (get_realtime_cpu_set(&cpu_realtime_set) != 0)
		error(1, ENOSYS, "get_realtime_cpu_set");

	if (get_online_cpu_set(&cpu_online_set) != 0)
		error(1, ENOSYS, "get_online_cpu_set");

	CPU_AND(&cpu_realtime_set, &cpu_realtime_set, &cpu_online_set);

	max_cpu = 0;
	cpus = 0;
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &cpu_realtime_set))
			continue;
		cpus++;
		if (i > max_cpu)
			max_cpu = i;
	}

	per_cpu_data = malloc(sizeof(*per_cpu_data) * (max_cpu + 1));
	if (per_cpu_data == NULL)
		error(1, ENOMEM, "malloc");

	memset(per_cpu_data, 0, sizeof(*per_cpu_data) * (max_cpu + 1));

	for (i = 0; i <= max_cpu; i++) {
		if (!CPU_ISSET(i, &cpu_realtime_set))
			continue;
		per_cpu_data[i].first_round = 1;
		pthread_create(&per_cpu_data[i].thread, NULL, cpu_thread,
			       (void *)(long)i);
	}

	printf("== Testing %s %s (%d)\n",
	       ext ? "extension" : "built-in", real_clock_name, clock_id);
	printf("CPU      ToD offset [us] ToD drift [us/s]      warps max delta [us]\n"
	       "--- -------------------- ---------------- ---------- --------------\n");

	while (1) {
		for (i = 0; i <= max_cpu; i++)
			if (CPU_ISSET(i, &cpu_realtime_set))
				printf("%3d %20.1f %16.3f %10lu %14.1f\n",
				       i,
				       per_cpu_data[i].offset/1000.0,
				       per_cpu_data[i].drift * 1000000.0,
				       per_cpu_data[i].warps,
				       per_cpu_data[i].max_warp/1000.0);
		usleep(250000);
		printf("\033[%dA", cpus);
	}
}

/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <rtdm/testing.h>
#include "memcheck/memcheck.h"

smokey_test_plugin(memory_coreheap,
		   MEMCHECK_ARGS,
		   "Check for the Cobalt core allocator sanity.\n"
		   MEMCHECK_HELP_STRINGS
	);

#define MIN_HEAP_SIZE  8192
#define MAX_HEAP_SIZE  (1024 * 1024 * 2)
#define RANDOM_ROUNDS  1024

#define PATTERN_HEAP_SIZE  (128*1024)
#define PATTERN_ROUNDS     128

static int kernel_test_seq(struct memcheck_descriptor *md,
		    size_t heap_size, size_t block_size, int flags)
{
	struct rttst_heap_stathdr sthdr;
	struct rttst_heap_parms parms;
	struct rttst_heap_stats *p;
	struct memcheck_stat *st;
	struct sched_param param;
	int fd, ret, n;

	fd = __RT(open("/dev/rtdm/heapcheck", O_RDWR));
	if (fd < 0)
		return -ENOSYS;

	/* This switches to real-time mode over Cobalt. */
	param.sched_priority = 1;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

	parms.heap_size = heap_size;
	parms.block_size = block_size;
	parms.flags = flags;
	ret = __RT(ioctl(fd, RTTST_RTIOC_HEAP_CHECK, &parms));
	if (ret)
		goto out;

	if (parms.nrstats == 0)
		goto out;

	sthdr.nrstats = parms.nrstats;
	sthdr.buf = __STD(malloc(sizeof(*sthdr.buf) * parms.nrstats));
	if (sthdr.buf == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	
	ret = __RT(ioctl(fd, RTTST_RTIOC_HEAP_STAT_COLLECT, &sthdr));
	if (ret)
		goto out;

	for (n = sthdr.nrstats, p = sthdr.buf; n > 0; n--, p++) {
		st = __STD(malloc(sizeof(*st)));
		if (st == NULL) {
			ret = -ENOMEM;
			goto out;
		}
		st->heap_size = p->heap_size;
		st->user_size = p->user_size;
		st->block_size = p->block_size;
		st->nrblocks = p->nrblocks;
		st->alloc_avg_ns = p->alloc_avg_ns;
		st->alloc_max_ns = p->alloc_max_ns;
		st->free_avg_ns = p->free_avg_ns;
		st->free_max_ns = p->free_max_ns;
		st->maximum_free = p->maximum_free;
		st->largest_free = p->largest_free;
		st->flags = p->flags;
		memcheck_log_stat(st);
	}
out:
	__RT(close(fd));

	param.sched_priority = 0;
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);

	return ret;
}

static struct memcheck_descriptor coreheap_descriptor = {
	.name = "coreheap",
	.seq_min_heap_size = MIN_HEAP_SIZE,
	.seq_max_heap_size = MAX_HEAP_SIZE,
	.random_rounds = RANDOM_ROUNDS,
	.pattern_heap_size = PATTERN_HEAP_SIZE,
	.pattern_rounds = PATTERN_ROUNDS,
	.valid_flags = MEMCHECK_ALL_FLAGS,
	.test_seq = kernel_test_seq,
};

static int run_memory_coreheap(struct smokey_test *t,
			       int argc, char *const argv[])
{
	return memcheck_run(&coreheap_descriptor, t, argc, argv);
}

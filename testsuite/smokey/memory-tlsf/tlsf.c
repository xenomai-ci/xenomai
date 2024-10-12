/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <tlsf/tlsf.h>
#include <stdlib.h>
#include <pthread.h>
#include "memcheck/memcheck.h"

smokey_test_plugin(memory_tlsf,
		   MEMCHECK_ARGS,
		   "Check for the TLSF allocator sanity.\n"
		   MEMCHECK_HELP_STRINGS
	);

#define MIN_HEAP_SIZE  8192
#define MAX_HEAP_SIZE  (1024 * 1024 * 2)
#define RANDOM_ROUNDS  1024

#define PATTERN_HEAP_SIZE  (128*1024)
#define PATTERN_ROUNDS     128

static struct memcheck_descriptor tlsf_descriptor;

static pthread_mutex_t tlsf_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t overhead;

static size_t test_pool_size; /* TLSF does not save this information. */

static int do_tlsf_init(void *dummy, void *mem, size_t pool_size)
{
	tlsf_descriptor.heap = mem;
	return init_memory_pool(pool_size, mem) == -1L ? -ENOMEM : 0;
}

static void do_tlsf_destroy(void *pool)
{
	destroy_memory_pool(pool);
}

static void *do_tlsf_alloc(void *pool, size_t size)
{
	void *p;

	pthread_mutex_lock(&tlsf_lock);
	p = malloc_ex(size, pool);
	pthread_mutex_unlock(&tlsf_lock);

	return p;
}

static int do_tlsf_free(void *pool, void *block)
{
	pthread_mutex_lock(&tlsf_lock);
	free_ex(block, pool);
	pthread_mutex_unlock(&tlsf_lock);

	return 0;	/* Yeah, well... */
}

static size_t do_tlsf_used_size(void *pool)
{
	/* Do not count the overhead memory for the TLSF header. */
	return get_used_size(pool) - overhead;
}

static size_t do_tlsf_usable_size(void *pool)
{
	return test_pool_size;
}

static size_t do_tlsf_arena_size(size_t pool_size)
{
	size_t available_size;
	void *pool;

	/*
	 * The area size is the total amount of memory some allocator
	 * may need for managing a heap, including its metadata. We
	 * need to figure out how much memory overhead TLSF has for a
	 * given pool size, which we add to the ideal pool_size for
	 * determining the arena size.
	 */
	test_pool_size = pool_size;
	pool = __STD(malloc(pool_size));
	available_size = init_memory_pool(pool_size, pool);
	if (available_size == (size_t)-1) {
		__STD(free(pool));
		return 0;
	}

	destroy_memory_pool(pool);
	overhead = pool_size - available_size;
	__STD(free(pool));

	return pool_size + overhead;
}

static struct memcheck_descriptor tlsf_descriptor = {
	.name = "tlsf",
	.init = HEAP_INIT_T(do_tlsf_init),
	.destroy = HEAP_DESTROY_T(do_tlsf_destroy),
	.alloc = HEAP_ALLOC_T(do_tlsf_alloc),
	.free = HEAP_FREE_T(do_tlsf_free),
	.get_usable_size = HEAP_USABLE_T(do_tlsf_usable_size),
	.get_used_size = HEAP_USED_T(do_tlsf_used_size),
	.get_arena_size = do_tlsf_arena_size,
	.seq_min_heap_size = MIN_HEAP_SIZE,
	.seq_max_heap_size = MAX_HEAP_SIZE,
	.random_rounds = RANDOM_ROUNDS,
	.pattern_heap_size = PATTERN_HEAP_SIZE,
	.pattern_rounds = PATTERN_ROUNDS,
	/* TLSF always has overhead, can't check for ZEROOVRD. */
	.valid_flags = MEMCHECK_ALL_FLAGS & ~MEMCHECK_ZEROOVRD,
};

static int run_memory_tlsf(struct smokey_test *t,
			   int argc, char *const argv[])
{
	return memcheck_run(&tlsf_descriptor, t, argc, argv);
}

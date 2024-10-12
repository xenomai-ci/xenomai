/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <xenomai/init.h>
#include <xenomai/tunables.h>
#include <copperplate/heapobj.h>
#include "memcheck/memcheck.h"

smokey_test_plugin(memory_pshared,
		   MEMCHECK_ARGS,
		   "Check for the pshared allocator sanity.\n"
		   MEMCHECK_HELP_STRINGS
	);

#define MIN_HEAP_SIZE  8192
#define MAX_HEAP_SIZE  (1024 * 1024 * 2)
#define RANDOM_ROUNDS  1024

#define PATTERN_HEAP_SIZE  (128*1024)
#define PATTERN_ROUNDS     128

static struct heapobj heap;

static int do_pshared_init(void *heap, void *mem, size_t arena_size)
{
	/* mem is ignored, pshared uses its own memory. */
	return heapobj_init(heap, "memcheck", arena_size);
}

static void do_pshared_destroy(void *heap)
{
	heapobj_destroy(heap);
}

static void *do_pshared_alloc(void *heap, size_t size)
{
	return heapobj_alloc(heap, size);
}

static int do_pshared_free(void *heap, void *block)
{
	heapobj_free(heap, block);

	return 0;	/* Hope for the best. */
}

static size_t do_pshared_used_size(void *heap)
{
	return heapobj_inquire(heap);
}

static size_t do_pshared_usable_size(void *heap)
{
	return heapobj_get_size(heap);
}

static size_t do_pshared_arena_size(size_t heap_size)
{
	struct heapobj h;
	size_t overhead;
	int ret;

	ret = heapobj_init(&h, "memcheck", heap_size);
	if (ret)
		return 0;

	overhead = heap_size - heapobj_get_size(&h);
	heapobj_destroy(&h);

	/*
	 * pshared must have no external overhead, since
	 * heapobj_init() allocates the memory it needs.  Make sure
	 * this assumption is correct for any tested size.
	 */
	return overhead == 0 ? heap_size : 0;
}

static struct memcheck_descriptor pshared_descriptor = {
	.name = "pshared",
	.init = HEAP_INIT_T(do_pshared_init),
	.destroy = HEAP_DESTROY_T(do_pshared_destroy),
	.alloc = HEAP_ALLOC_T(do_pshared_alloc),
	.free = HEAP_FREE_T(do_pshared_free),
	.get_usable_size = HEAP_USABLE_T(do_pshared_usable_size),
	.get_used_size = HEAP_USED_T(do_pshared_used_size),
	.get_arena_size = do_pshared_arena_size,
	.seq_min_heap_size = MIN_HEAP_SIZE,
	.seq_max_heap_size = MAX_HEAP_SIZE,
	.random_rounds = RANDOM_ROUNDS,
	.pattern_heap_size = PATTERN_HEAP_SIZE,
	.pattern_rounds = PATTERN_ROUNDS,
	/* heapobj-pshared has overgead even for ^2 sizes, can't check for ZEROOVRD. */
	.valid_flags = MEMCHECK_ALL_FLAGS & ~MEMCHECK_ZEROOVRD,
	.heap = &heap,
};

static int run_memory_pshared(struct smokey_test *t,
			      int argc, char *const argv[])
{
	return memcheck_run(&pshared_descriptor, t, argc, argv);
}

static int memcheck_pshared_tune(void)
{
	/*
	 * We create test pools from the main one: make sure the
	 * latter is large enough.
	 */
	set_config_tunable(mem_pool_size, MAX_HEAP_SIZE + 1024 * 1024);

	return 0;
}

static struct setup_descriptor memcheck_pshared_setup = {
	.name = "memcheck_pshared",
	.tune = memcheck_pshared_tune,
};

user_setup_call(memcheck_pshared_setup);

/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <boilerplate/heapmem.h>
#include "memcheck/memcheck.h"

smokey_test_plugin(memory_heapmem,
		   MEMCHECK_ARGS,
		   "Check for the heapmem allocator sanity.\n"
		   MEMCHECK_HELP_STRINGS
	);

#define MIN_HEAP_SIZE  8192
#define MAX_HEAP_SIZE  (1024 * 1024 * 2)
#define RANDOM_ROUNDS  1024

#define PATTERN_HEAP_SIZE  (128*1024)
#define PATTERN_ROUNDS     128

static struct heap_memory heap;

static size_t get_arena_size(size_t heap_size)
{
	return HEAPMEM_ARENA_SIZE(heap_size);
}

static struct memcheck_descriptor heapmem_descriptor = {
	.name = "heapmem",
	.init = HEAP_INIT_T(heapmem_init),
	.destroy = HEAP_DESTROY_T(heapmem_destroy),
	.alloc = HEAP_ALLOC_T(heapmem_alloc),
	.free = HEAP_FREE_T(heapmem_free),
	.get_usable_size = HEAP_USABLE_T(heapmem_usable_size),
	.get_used_size = HEAP_USED_T(heapmem_used_size),
	.seq_min_heap_size = MIN_HEAP_SIZE,
	.seq_max_heap_size = MAX_HEAP_SIZE,
	.random_rounds = RANDOM_ROUNDS,
	.pattern_heap_size = PATTERN_HEAP_SIZE,
	.pattern_rounds = PATTERN_ROUNDS,
	.heap = &heap,
	.get_arena_size = get_arena_size,
	.valid_flags = MEMCHECK_ALL_FLAGS,
};

static int run_memory_heapmem(struct smokey_test *t,
			      int argc, char *const argv[])
{
	return memcheck_run(&heapmem_descriptor, t, argc, argv);
}

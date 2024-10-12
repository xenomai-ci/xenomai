/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <memory.h>
#include <sched.h>
#include <pthread.h>
#include "memcheck.h"

enum pattern {
	alphabet_series,
	digit_series,
	binary_series,
};

struct chunk {
	void *ptr;
	enum pattern pattern;
};

static struct memcheck_stat *statistics;

static int nrstats;

static int max_results = 4;

#ifdef CONFIG_XENO_COBALT

#include <sys/cobalt.h>

static inline void breathe(int loops)
{
	struct timespec idle = {
		.tv_sec = 0,
		.tv_nsec = 300000,
	};

	/*
	 * There is not rt throttling over Cobalt, so we may need to
	 * keep the host kernel breathing by napping during the test
	 * sequences.
	 */
	if ((loops % 1000) == 0)
		__RT(clock_nanosleep(CLOCK_MONOTONIC, 0, &idle, NULL));
}

static inline void harden(void)
{
	cobalt_thread_harden();
}

#else

static inline void breathe(int loops) { }

static inline void harden(void) { }

#endif

static inline long diff_ts(struct timespec *left, struct timespec *right)
{
	return (long long)(left->tv_sec - right->tv_sec) * ONE_BILLION
		+ left->tv_nsec - right->tv_nsec;
}

static inline void swap(void *left, void *right, const size_t size)
{
	char trans[size];

	memcpy(trans, left, size);
	memcpy(left, right, size);
	memcpy(right, trans, size);
}

static void random_shuffle(void *vbase, size_t nmemb, const size_t size)
{
	char *base = (char *)vbase;
	unsigned int j, k;
	double u;

	for(j = nmemb; j > 0; j--) {
		breathe(j);
		u = (double)random() / RAND_MAX;
		k = (unsigned int)(j * u) + 1;
		if (j == k)
			continue;
		swap(base + (j - 1) * size, base + (k - 1) * size, size);
	}
}

/* Reverse sort, high values first. */

#define compare_values(l, r)			\
	({					\
		typeof(l) _l = (l);		\
		typeof(r) _r = (r);		\
		(_l > _r) - (_l < _r);		\
	})

static int sort_by_heap_size(const void *l, const void *r)
{
	const struct memcheck_stat *ls = l, *rs = r;

	return compare_values(rs->heap_size, ls->heap_size);
}

static int sort_by_alloc_time(const void *l, const void *r)
{
	const struct memcheck_stat *ls = l, *rs = r;

	return compare_values(rs->alloc_max_ns, ls->alloc_max_ns);
}

static int sort_by_free_time(const void *l, const void *r)
{
	const struct memcheck_stat *ls = l, *rs = r;

	return compare_values(rs->free_max_ns, ls->free_max_ns);
}

static int sort_by_frag(const void *l, const void *r)
{
	const struct memcheck_stat *ls = l, *rs = r;

	return compare_values(rs->maximum_free - rs->largest_free,
			      ls->maximum_free - ls->largest_free);
}

static int sort_by_overhead(const void *l, const void *r)
{
	const struct memcheck_stat *ls = l, *rs = r;

	return compare_values(rs->heap_size - rs->user_size,
			      ls->heap_size - ls->user_size);
}

static inline const char *get_debug_state(void)
{
#if defined(CONFIG_XENO_DEBUG_FULL)
	return "\n(CAUTION: full debug enabled)";
#elif defined(CONFIG_XENO_DEBUG)
	return "\n(debug partially enabled)";
#else
	return "";
#endif
}

static void __dump_stats(struct memcheck_descriptor *md,
			 struct memcheck_stat *stats,
			 int (*sortfn)(const void *l, const void *r),
			 int nr, const char *key)
{
	struct memcheck_stat *p;
	int n;

	qsort(stats, nrstats, sizeof(*p), sortfn);

	smokey_trace("\nsorted by: max %s\n%8s  %7s  %7s  %5s  %5s  %5s  %5s   %5s  %5s  %s",
		     key, "HEAPSZ", "BLOCKSZ", "NRBLKS", "AVG-A",
		     "AVG-F", "MAX-A", "MAX-F", "OVRH%", "FRAG%", "FLAGS");

	for (n = 0; n < nr; n++) {
		p = stats + n;
		smokey_trace("%7zuk  %7zu%s  %6d  %5.1f  %5.1f  %5.1f  %5.1f   %4.1f  %5.1f   %s%s%s",
			     p->heap_size / 1024,
			     p->block_size < 1024 ? p->block_size : p->block_size / 1024,
			     p->block_size < 1024 ? " " : "k",
			     p->nrblocks,
			     (double)p->alloc_avg_ns/1000.0,
			     (double)p->free_avg_ns/1000.0,
			     (double)p->alloc_max_ns/1000.0,
			     (double)p->free_max_ns/1000.0,
			     100.0 - (p->user_size * 100.0 / p->heap_size),
			     (1.0 - ((double)p->largest_free / p->maximum_free)) * 100.0,
			     p->alloc_avg_ns == 0 && p->free_avg_ns == 0 ? "FAILED " : "",
			     p->flags & MEMCHECK_SHUFFLE ? "+shuffle " : "",
			     p->flags & MEMCHECK_HOT ? "+hot" : "");
	}

	if (nr < nrstats)
		smokey_trace("  ... (%d results following) ...", nrstats - nr);
}

static int dump_stats(struct memcheck_descriptor *md, const char *title)
{
	long worst_alloc_max = 0, worst_free_max = 0;
	double overhead_sum = 0.0, frag_sum = 0.0;
	long max_alloc_sum = 0, max_free_sum = 0;
	long avg_alloc_sum = 0, avg_free_sum = 0;
	struct memcheck_stat *stats, *p, *next;
	int n;

	stats = __STD(malloc(sizeof(*p) * nrstats));
	if (stats == NULL) {
		smokey_warning("failed allocating memory");
		return -ENOMEM;
	}

	for (n = 0, p = statistics; n < nrstats; n++, p = p->next)
		stats[n] = *p;

	smokey_trace("\n[%s] ON '%s'%s\n",
		     title, md->name, get_debug_state());

	smokey_trace("HEAPSZ	test heap size");
	smokey_trace("BLOCKSZ	tested block size");
	smokey_trace("NRBLKS	number of blocks allocatable in heap");
	smokey_trace("AVG-A	average time to allocate block (us)");
	smokey_trace("AVG-F	average time to free block (us)");
	smokey_trace("MAX-A	max time to allocate block (us)");
	smokey_trace("MAX-F	max time to free block (us)");
	smokey_trace("OVRH%	overhead");
	smokey_trace("FRAG%	external fragmentation");
	smokey_trace("FLAGS	+shuffle: randomized free");
	smokey_trace("    	+hot: measure after initial alloc/free pass (hot heap)");

	if (max_results > 0) {
		if (max_results > nrstats)
			max_results = nrstats;
		__dump_stats(md, stats, sort_by_alloc_time, max_results, "alloc time");
		__dump_stats(md, stats, sort_by_free_time, max_results, "free time");
		__dump_stats(md, stats, sort_by_overhead, max_results, "overhead");
		__dump_stats(md, stats, sort_by_frag, max_results, "fragmentation");
	} else if (max_results < 0)
		__dump_stats(md, stats, sort_by_heap_size, nrstats, "heap size");

	__STD(free(stats));

	for (p = statistics; p; p = next) {
		max_alloc_sum += p->alloc_max_ns;
		max_free_sum += p->free_max_ns;
		avg_alloc_sum += p->alloc_avg_ns;
		avg_free_sum += p->free_avg_ns;
		overhead_sum += 100.0 - (p->user_size * 100.0 / p->heap_size);
		frag_sum += (1.0 - ((double)p->largest_free / p->maximum_free)) * 100.0;
		if (p->alloc_max_ns > worst_alloc_max)
			worst_alloc_max = p->alloc_max_ns;
		if (p->free_max_ns > worst_free_max)
			worst_free_max = p->free_max_ns;
		next = p->next;
		__STD(free(p));
	}

	smokey_trace("\noverall:");
	smokey_trace("  worst alloc time: %.1f (us)",
		     (double)worst_alloc_max / 1000.0);
	smokey_trace("  worst free time: %.1f (us)",
		     (double)worst_free_max / 1000.0);
	smokey_trace("  average of max. alloc times: %.1f (us)",
		     (double)max_alloc_sum / nrstats / 1000.0);
	smokey_trace("  average of max. free times: %.1f (us)",
		     (double)max_free_sum / nrstats / 1000.0);
	smokey_trace("  average alloc time: %.1f (us)",
		     (double)avg_alloc_sum / nrstats / 1000.0);
	smokey_trace("  average free time: %.1f (us)",
		     (double)avg_free_sum / nrstats / 1000.0);
	smokey_trace("  average overhead: %.1f%%",
		     (double)overhead_sum / nrstats);
	smokey_trace("  average fragmentation: %.1f%%",
		     (double)frag_sum / nrstats);

	statistics = NULL;
	nrstats = 0;

	return 0;
}

static void fill_pattern(char *p, size_t size, enum pattern pat)
{
	unsigned int val, count;

	switch (pat) {
	case alphabet_series:
		val = 'a';
		count = 26;
		break;
	case digit_series:
		val = '0';
		count = 10;
		break;
	default:
		val = 0;
		count = 255;
		break;
	}

	while (size-- > 0) {
		*p++ = (char)(val % count);
		val++;
	}
}

static int check_pattern(const char *p, size_t size, enum pattern pat)
{
	unsigned int val, count;

	switch (pat) {
	case alphabet_series:
		val = 'a';
		count = 26;
		break;
	case digit_series:
		val = '0';
		count = 10;
		break;
	default:
		val = 0;
		count = 255;
		break;
	}

	while (size-- > 0) {
		if (*p++ != (char)(val % count))
			return 0;
		val++;
	}

	return 1;
}

static size_t find_largest_free(struct memcheck_descriptor *md,
				size_t free_size, size_t block_size)
{
	void *p;

	for (;;) {
		p = md->alloc(md->heap, free_size);
		if (p) {
			md->free(md->heap, p);
			break;
		}
		if (free_size <= block_size)
			break;
		free_size -= block_size;
	}

	return free_size;
}

/*
 * The default test helper can exercise heap managers implemented in
 * userland.
 */
static int default_test_seq(struct memcheck_descriptor *md,
		    size_t heap_size, size_t block_size, int flags)
{
	size_t arena_size, user_size, largest_free, maximum_free, freed;
	long alloc_sum_ns, alloc_avg_ns, free_sum_ns, free_avg_ns,
		alloc_max_ns, free_max_ns, d;
	int ret, n, k, maxblocks, nrblocks;
	struct timespec start, end;
	struct memcheck_stat *st;
	struct sched_param param;
	struct chunk *chunks;
	bool done_frag;
	void *mem, *p;

	/* This switches to real-time mode over Cobalt. */
	param.sched_priority = 1;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

	arena_size = heap_size;
	if (md->get_arena_size) {
		arena_size = md->get_arena_size(heap_size);
		if (arena_size == 0) {
			smokey_trace("cannot get arena size for heap size %zu",
				     heap_size);
			return -ENOMEM;
		}
	}
	
	mem = __STD(malloc(arena_size));
	if (mem == NULL)
		return -ENOMEM;

	maxblocks = heap_size / block_size;

	ret = md->init(md->heap, mem, arena_size);
	if (ret) {
		smokey_trace("cannot init heap with arena size %zu",
			     arena_size);
		goto out;
	}

	chunks = calloc(sizeof(*chunks), maxblocks);
	if (chunks == NULL) {
		ret = -ENOMEM;
		goto no_chunks;
	}

	if (md->get_usable_size(md->heap) != heap_size) {
		smokey_trace("memory size inconsistency (%zu / %zu bytes)",
			     heap_size, md->get_usable_size(md->heap));
		goto bad;
	}

	user_size = 0;
	alloc_avg_ns = 0;
	free_avg_ns = 0;
	alloc_max_ns = 0;
	free_max_ns = 0;
	largest_free = 0;
	maximum_free = 0;

	/*
	 * With Cobalt, make sure to run in primary mode before the
	 * first allocation call takes place, not to charge any switch
	 * time to the allocator.
	 */
	harden();
	for (n = 0, alloc_sum_ns = 0; ; n++) {
		__RT(clock_gettime(CLOCK_MONOTONIC, &start));
		p = md->alloc(md->heap, block_size);
		__RT(clock_gettime(CLOCK_MONOTONIC, &end));
		d = diff_ts(&end, &start);
		if (d > alloc_max_ns)
			alloc_max_ns = d;
		alloc_sum_ns += d;
		if (p == NULL)
			break;
		user_size += block_size;
		if (n >= maxblocks) {
			smokey_trace("too many blocks fetched"
				     " (heap=%zu, block=%zu, "
				     "got more than %d blocks)",
				     heap_size, block_size, maxblocks);
			goto bad;
		}
		chunks[n].ptr = p;
		if (flags & MEMCHECK_PATTERN) {
			chunks[n].pattern = (enum pattern)(random() % 3);
			fill_pattern(chunks[n].ptr, block_size, chunks[n].pattern);
		}
		breathe(n);
	}

	nrblocks = n;
	if (nrblocks == 0)
		goto do_stats;

	if ((flags & MEMCHECK_ZEROOVRD) && nrblocks != maxblocks) {
		smokey_trace("too few blocks fetched, unexpected overhead"
			     " (heap=%zu, block=%zu, "
			     "got %d, less than %d blocks)",
			     heap_size, block_size, nrblocks, maxblocks);
		goto bad;
	}

	breathe(0);

	/* Make sure we did not trash any busy block while allocating. */
	if (flags & MEMCHECK_PATTERN) {
		for (n = 0; n < nrblocks; n++) {
			if (!check_pattern(chunks[n].ptr, block_size,
					   chunks[n].pattern)) {
				smokey_trace("corrupted block #%d on alloc"
					     " sequence (pattern %d)",
					     n, chunks[n].pattern);
				goto bad;
			}
			breathe(n);
		}
	}
	
	if (flags & MEMCHECK_SHUFFLE)
		random_shuffle(chunks, nrblocks, sizeof(*chunks));

	/*
	 * Release all blocks.
	 */
	harden();
	for (n = 0, free_sum_ns = 0, freed = 0, done_frag = false;
	     n < nrblocks; n++) {
		__RT(clock_gettime(CLOCK_MONOTONIC, &start));
		ret = md->free(md->heap, chunks[n].ptr);
		__RT(clock_gettime(CLOCK_MONOTONIC, &end));
		if (ret) {
			smokey_trace("failed to free block %p "
				     "(heap=%zu, block=%zu)",
				     chunks[n].ptr, heap_size, block_size);
			goto bad;
		}
		d = diff_ts(&end, &start);
		if (d > free_max_ns)
			free_max_ns = d;
		free_sum_ns += d;
		chunks[n].ptr = NULL;
		/* Make sure we did not trash busy blocks while freeing. */
		if (flags & MEMCHECK_PATTERN) {
			for (k = 0; k < nrblocks; k++) {
				if (chunks[k].ptr &&
				    !check_pattern(chunks[k].ptr, block_size,
						   chunks[k].pattern)) {
					smokey_trace("corrupted block #%d on release"
						     " sequence (pattern %d)",
						     k, chunks[k].pattern);
					goto bad;
				}
				breathe(k);
			}
		}
		freed += block_size;
		/*
		 * Get a sense of the fragmentation for the tested
		 * allocation pattern, heap and block sizes when half
		 * of the usable heap size should be available to us.
		 * NOTE: user_size excludes the overhead, this is
		 * actually what we managed to get from the current
		 * heap out of the allocation loop.
		 */
		if (!done_frag && freed >= user_size / 2) {
			/* Calculate the external fragmentation. */
			largest_free = find_largest_free(md, freed, block_size);
			maximum_free = freed;
			done_frag = true;
		}
		breathe(n);
	}

	/*
	 * If the deallocation mechanism is broken, we might not be
	 * able to reproduce the same allocation pattern with the same
	 * outcome, check this.
	 */
	if (flags & MEMCHECK_HOT) {
		for (n = 0, alloc_max_ns = alloc_sum_ns = 0; ; n++) {
			__RT(clock_gettime(CLOCK_MONOTONIC, &start));
			p = md->alloc(md->heap, block_size);
			__RT(clock_gettime(CLOCK_MONOTONIC, &end));
			d = diff_ts(&end, &start);
			if (d > alloc_max_ns)
				alloc_max_ns = d;
			alloc_sum_ns += d;
			if (p == NULL)
				break;
			if (n >= maxblocks) {
				smokey_trace("too many blocks fetched during hot pass"
					     " (heap=%zu, block=%zu, "
					     "got more than %d blocks)",
					     heap_size, block_size, maxblocks);
				goto bad;
			}
			chunks[n].ptr = p;
			breathe(n);
		}
		if (n != nrblocks) {
			smokey_trace("inconsistent block count fetched during hot pass"
				     " (heap=%zu, block=%zu, "
				     "got %d blocks vs %d during alloc)",
				     heap_size, block_size, n, nrblocks);
			goto bad;
		}
		for (n = 0, free_max_ns = free_sum_ns = 0; n < nrblocks; n++) {
			__RT(clock_gettime(CLOCK_MONOTONIC, &start));
			ret = md->free(md->heap, chunks[n].ptr);
			__RT(clock_gettime(CLOCK_MONOTONIC, &end));
			if (ret) {
				smokey_trace("failed to free block %p during hot pass"
					     "(heap=%zu, block=%zu)",
					     chunks[n].ptr, heap_size, block_size);
				goto bad;
			}
			d = diff_ts(&end, &start);
			if (d > free_max_ns)
				free_max_ns = d;
			free_sum_ns += d;
			breathe(n);
		}
	}

	alloc_avg_ns = alloc_sum_ns / nrblocks;
	free_avg_ns = free_sum_ns / nrblocks;

	if ((flags & MEMCHECK_ZEROOVRD) && heap_size != user_size) {
		smokey_trace("unexpected overhead reported");
		goto bad;
	}

	if (md->get_used_size(md->heap) > 0) {
		smokey_trace("memory leakage reported: %zu bytes missing",
			     md->get_used_size(md->heap));
		goto bad;
	}
		
	/*
	 * Don't report stats when running a pattern check, timings
	 * are affected.
	 */
do_stats:
	breathe(0);
	ret = 0;
	if (!(flags & MEMCHECK_PATTERN)) {
		st = __STD(malloc(sizeof(*st)));
		if (st == NULL) {
			smokey_warning("failed allocating memory");
			ret = -ENOMEM;
			goto oom;
		}
		st->heap_size = heap_size;
		st->user_size = user_size;
		st->block_size = block_size;
		st->nrblocks = nrblocks;
		st->alloc_avg_ns = alloc_avg_ns;
		st->alloc_max_ns = alloc_max_ns;
		st->free_avg_ns = free_avg_ns;
		st->free_max_ns = free_max_ns;
		st->largest_free = largest_free;
		st->maximum_free = maximum_free;
		st->flags = flags;
		memcheck_log_stat(st);
	}

done:
	__STD(free(chunks));
no_chunks:
	md->destroy(md->heap);
out:
	if (ret)
		smokey_trace("** '%s' FAILED(overhead %s, %sshuffle, %scheck, %shot): heapsz=%zuk, "
			     "blocksz=%zu, overhead=%zu (%.1f%%)",
			     md->name,
			     flags & MEMCHECK_ZEROOVRD ? "disallowed" : "allowed",
			     flags & MEMCHECK_SHUFFLE ? "" : "no ",
			     flags & MEMCHECK_PATTERN ? "" : "no ",
			     flags & MEMCHECK_HOT ? "" : "no ",
			     heap_size / 1024, block_size,
			     arena_size - heap_size,
			     (arena_size * 100.0 / heap_size) - 100.0);
oom:
	__STD(free(mem));

	param.sched_priority = 0;
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);

	return ret;
bad:
	ret = -EPROTO;
	goto done;
}

static inline int test_flags(struct memcheck_descriptor *md, int flags)
{
	return md->valid_flags & flags;
}

void memcheck_log_stat(struct memcheck_stat *st)
{
	st->next = statistics;
	statistics = st;
	nrstats++;
}

int memcheck_run(struct memcheck_descriptor *md,
		 struct smokey_test *t,
		 int argc, char *const argv[])
{
	int (*test_seq)(struct memcheck_descriptor *md,
			size_t heap_size, size_t block_size, int flags);
	size_t heap_size, block_size;
	cpu_set_t affinity;
	unsigned long seed;
	int ret, runs;
	time_t now;
	void *p;

	/* Populate the malloc arena early to limit mode switching. */
	p = __STD(malloc(2 * 1024 * 1024));
	__STD(free(p));

	smokey_parse_args(t, argc, argv);
	
	if (smokey_arg_isset(t, "seq_heap_size"))
		md->seq_max_heap_size = smokey_arg_size(t, "seq_heap_size");
	
	if (smokey_arg_isset(t, "random_alloc_rounds"))
		md->random_rounds = smokey_arg_int(t, "random_alloc_rounds");
	
	if (smokey_arg_isset(t, "pattern_heap_size"))
		md->pattern_heap_size = smokey_arg_size(t, "pattern_heap_size");
	
	if (smokey_arg_isset(t, "pattern_check_rounds"))
		md->pattern_rounds = smokey_arg_int(t, "pattern_check_rounds");

	if (smokey_arg_isset(t, "max_results"))
		max_results = smokey_arg_int(t, "max_results");

	test_seq = md->test_seq;
	if (test_seq == NULL)
		test_seq = default_test_seq;

	now = time(NULL);
	seed = (unsigned long)now * getpid();
	srandom(seed);

	smokey_trace("== memcheck started for %s at %s", md->name, ctime(&now));
	smokey_trace("     seq_heap_size=%zuk", md->seq_max_heap_size / 1024);
	smokey_trace("     random_alloc_rounds=%d", md->random_rounds);
	smokey_trace("     pattern_heap_size=%zuk", md->pattern_heap_size / 1024);
	smokey_trace("     pattern_check_rounds=%d", md->pattern_rounds);
	
	CPU_ZERO(&affinity);
	CPU_SET(0, &affinity);
	ret = sched_setaffinity(0, sizeof(affinity), &affinity);
	if (ret) {
		smokey_warning("failed setting CPU affinity");
		return -ret;
	}

	/*
	 * Create a series of heaps of increasing size, allocating
	 * then freeing all blocks sequentially from them, ^2 block
	 * sizes up to half of the heap size. Test multiple patterns:
	 *
	 * - alloc -> free_in_alloc_order
	 * - alloc -> free_in_alloc_order -> (re)alloc
	 * - alloc -> free_in_random_order
	 * - alloc -> free_in_random_order -> (re)alloc
	 */
	for (heap_size = md->seq_min_heap_size;
	     heap_size < md->seq_max_heap_size; heap_size <<= 1) {
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(md, heap_size, block_size,
					   test_flags(md, MEMCHECK_ZEROOVRD));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(md, heap_size, block_size,
			   test_flags(md, MEMCHECK_ZEROOVRD|MEMCHECK_HOT));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2, hot)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(md, heap_size, block_size,
			       test_flags(md, MEMCHECK_ZEROOVRD|MEMCHECK_SHUFFLE));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2, shuffle)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(md, heap_size, block_size,
			       test_flags(md, MEMCHECK_ZEROOVRD|MEMCHECK_HOT|MEMCHECK_SHUFFLE));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2, shuffle, hot)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}

	ret = dump_stats(md, "SEQUENTIAL ALLOC->FREE, ^2 BLOCK SIZES");
	if (ret)
		return ret;

	/*
	 * Create a series of heaps of increasing size, allocating
	 * then freeing all blocks sequentially from them, random
	 * block sizes. Test multiple patterns as previously with ^2
	 * block sizes.
	 */
	for (heap_size = md->seq_min_heap_size;
	     heap_size < md->seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < md->random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(md, heap_size, block_size, 0);
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (random)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}
	
	for (heap_size = md->seq_min_heap_size;
	     heap_size < md->seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < md->random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(md, heap_size, block_size,
				       test_flags(md, MEMCHECK_HOT));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (random, hot)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}
	
	for (heap_size = md->seq_min_heap_size;
	     heap_size < md->seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < md->random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(md, heap_size, block_size,
				       test_flags(md, MEMCHECK_SHUFFLE));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (random, shuffle)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}
	
	for (heap_size = md->seq_min_heap_size;
	     heap_size < md->seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < md->random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(md, heap_size, block_size,
			       test_flags(md, MEMCHECK_HOT|MEMCHECK_SHUFFLE));
			if (ret) {
				smokey_trace("failed with %zuk heap, "
					     "%zu-byte block (random, shuffle, hot)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}

	ret = dump_stats(md, "SEQUENTIAL ALLOC->FREE, RANDOM BLOCK SIZES");
	if (ret)
		return ret;

	smokey_trace("\n(running the pattern check test for '%s'"
		     " -- this may take some time)", md->name);

	for (runs = 0; runs < md->pattern_rounds; runs++) {
		block_size = (random() % (md->pattern_heap_size / 2)) ?: 1;
		ret = test_seq(md, md->pattern_heap_size, block_size,
			       test_flags(md, MEMCHECK_SHUFFLE|MEMCHECK_PATTERN));
		if (ret) {
			smokey_trace("failed with %zuk heap, "
				     "%zu-byte block (random, shuffle, check)",
				     md->pattern_heap_size / 1024, block_size);
			return ret;
		}
	}
	
	now = time(NULL);
	smokey_trace("\n== memcheck finished for %s at %s",
		     md->name, ctime(&now));

	return ret;
}

#ifdef CONFIG_XENO_COBALT

#include <cobalt/tunables.h>

static int memcheck_tune(void)
{
	set_config_tunable(print_buffer_size, 512 * 1024);

	return 0;
}

static struct setup_descriptor memcheck_setup = {
	.name = "memcheck",
	.tune = memcheck_tune,
};

user_setup_call(memcheck_setup);

#endif

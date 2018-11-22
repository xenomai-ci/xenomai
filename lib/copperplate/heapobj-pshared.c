/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 * This code is adapted from Xenomai's original dual kernel xnheap
 * support. It is simple and efficient enough for managing dynamic
 * memory allocation backed by a tmpfs file, we can share between
 * multiple processes in user-space.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include "boilerplate/list.h"
#include "boilerplate/hash.h"
#include "boilerplate/lock.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"
#include "xenomai/init.h"
#include "internal.h"

enum sheapmem_pgtype {
	page_free =0,
	page_cont =1,
	page_list =2
};

static struct shavl_searchops size_search_ops;
static struct shavl_searchops addr_search_ops;

/*
 * The main heap consists of a shared heap at its core, with
 * additional session-wide information.
 */
struct session_heap {
	struct shared_heap_memory heap;
	int cpid;
	memoff_t maplen;
	struct hash_table catalog;
	struct sysgroup sysgroup;
};

/*
 * The base address of the shared memory heap, as seen by each
 * individual process. Its control block is always first, so that
 * different processes can access this information right after the
 * segment is mmapped. This also ensures that offset 0 will never
 * refer to a valid page or block.
 */
void *__main_heap;
#define main_heap	(*(struct session_heap *)__main_heap)
/*
 *  Base address for offset-based addressing, which is the start of
 *  the session heap since all memory objects are allocated from it,
 *  including other (sub-)heaps.
 */
#define main_base	__main_heap

/* A table of shared clusters for the session. */
struct hash_table *__main_catalog;

/* Pointer to the system list group. */
struct sysgroup *__main_sysgroup;

static struct heapobj main_pool;

#define __shoff(b, p)		((void *)(p) - (void *)(b))
#define __shoff_check(b, p)	((p) ? __shoff(b, p) : 0)
#define __shref(b, o)		((void *)((void *)(b) + (o)))
#define __shref_check(b, o)	((o) ? __shref(b, o) : NULL)

static inline uint32_t __attribute__ ((always_inline))
gen_block_mask(int log2size)
{
	return -1U >> (32 - (SHEAPMEM_PAGE_SIZE >> log2size));
}

static inline  __attribute__ ((always_inline))
int addr_to_pagenr(struct sheapmem_extent *ext, void *p)
{
	return (p - __shref(main_base, ext->membase)) >> SHEAPMEM_PAGE_SHIFT;
}

static inline  __attribute__ ((always_inline))
void *pagenr_to_addr(struct sheapmem_extent *ext, int pg)
{
	return __shref(main_base, ext->membase + (pg << SHEAPMEM_PAGE_SHIFT));
}

#ifdef CONFIG_XENO_DEBUG_FULL
/*
 * Setting page_cont/page_free in the page map is only required for
 * enabling full checking of the block address in free requests, which
 * may be extremely time-consuming when deallocating huge blocks
 * spanning thousands of pages. We only do such marking when running
 * in full debug mode.
 */
static inline bool
page_is_valid(struct sheapmem_extent *ext, int pg)
{
	switch (ext->pagemap[pg].type) {
	case page_free:
	case page_cont:
		return false;
	case page_list:
	default:
		return true;
	}
}

static void mark_pages(struct sheapmem_extent *ext,
		       int pg, int nrpages,
		       enum sheapmem_pgtype type)
{
	while (nrpages-- > 0)
		ext->pagemap[pg].type = type;
}

#else

static inline bool
page_is_valid(struct sheapmem_extent *ext, int pg)
{
	return true;
}

static void mark_pages(struct sheapmem_extent *ext,
		       int pg, int nrpages,
		       enum sheapmem_pgtype type)
{ }

#endif

ssize_t sheapmem_check(struct shared_heap_memory *heap, void *block)
{
	struct sheapmem_extent *ext;
	memoff_t pg, pgoff, boff;
	ssize_t ret = -EINVAL;
	size_t bsize;

	read_lock_nocancel(&heap->lock);

	/*
	 * Find the extent the checked block is originating from.
	 */
	__list_for_each_entry(main_base, ext, &heap->extents, next) {
		if (__shoff(main_base, block) >= ext->membase &&
		    __shoff(main_base, block) < ext->memlim)
			goto found;
	}
	goto out;
found:
	/* Calculate the page number from the block address. */
	pgoff = __shoff(main_base, block) - ext->membase;
	pg = pgoff >> SHEAPMEM_PAGE_SHIFT;
	if (page_is_valid(ext, pg)) {
		if (ext->pagemap[pg].type == page_list)
			bsize = ext->pagemap[pg].bsize;
		else {
			bsize = (1 << ext->pagemap[pg].type);
			boff = pgoff & ~SHEAPMEM_PAGE_MASK;
			if ((boff & (bsize - 1)) != 0) /* Not at block start? */
				goto out;
		}
		ret = (ssize_t)bsize;
	}
out:
	read_unlock(&heap->lock);

	return ret;
}

static inline struct sheapmem_range *
find_suitable_range(struct sheapmem_extent *ext, size_t size)
{
	struct sheapmem_range lookup;
	struct shavlh *node;

	lookup.size = size;
	node = shavl_search_ge(&ext->size_tree, &lookup.size_node,
			       &size_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct sheapmem_range, size_node);
}

static int reserve_page_range(struct sheapmem_extent *ext, size_t size)
{
	struct sheapmem_range *new, *splitr;

	new = find_suitable_range(ext, size);
	if (new == NULL)
		return -1;

	shavl_delete(&ext->size_tree, &new->size_node);
	if (new->size == size) {
		shavl_delete(&ext->addr_tree, &new->addr_node);
		return addr_to_pagenr(ext, new);
	}

	/*
	 * The free range fetched is larger than what we need: split
	 * it in two, the upper part goes to the user, the lower part
	 * is returned to the free list, which makes reindexing by
	 * address pointless.
	 */
	splitr = new;
	splitr->size -= size;
	new = (struct sheapmem_range *)((void *)new + splitr->size);
	shavlh_init(&splitr->size_node);
	shavl_insert_back(&ext->size_tree, &splitr->size_node,
			  &size_search_ops);

	return addr_to_pagenr(ext, new);
}

static inline struct sheapmem_range *
find_left_neighbour(struct sheapmem_extent *ext, struct sheapmem_range *r)
{
	struct shavlh *node;

	node = shavl_search_le(&ext->addr_tree, &r->addr_node,
			       &addr_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct sheapmem_range, addr_node);
}

static inline struct sheapmem_range *
find_right_neighbour(struct sheapmem_extent *ext, struct sheapmem_range *r)
{
	struct shavlh *node;

	node = shavl_search_ge(&ext->addr_tree, &r->addr_node,
			       &addr_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct sheapmem_range, addr_node);
}

static inline struct sheapmem_range *
find_next_neighbour(struct sheapmem_extent *ext, struct sheapmem_range *r)
{
	struct shavlh *node;

	node = shavl_next(&ext->addr_tree, &r->addr_node);
	if (node == NULL)
		return NULL;

	return container_of(node, struct sheapmem_range, addr_node);
}

static inline bool
ranges_mergeable(struct sheapmem_range *left, struct sheapmem_range *right)
{
	return (void *)left + left->size == (void *)right;
}

static void release_page_range(struct sheapmem_extent *ext,
			       void *page, size_t size)
{
	struct sheapmem_range *freed = page, *left, *right;
	bool addr_linked = false;

	freed->size = size;

	left = find_left_neighbour(ext, freed);
	if (left && ranges_mergeable(left, freed)) {
		shavl_delete(&ext->size_tree, &left->size_node);
		left->size += freed->size;
		freed = left;
		addr_linked = true;
		right = find_next_neighbour(ext, freed);
	} else
		right = find_right_neighbour(ext, freed);

	if (right && ranges_mergeable(freed, right)) {
		shavl_delete(&ext->size_tree, &right->size_node);
		freed->size += right->size;
		if (addr_linked)
			shavl_delete(&ext->addr_tree, &right->addr_node);
		else
			shavl_replace(&ext->addr_tree, &right->addr_node,
				      &freed->addr_node, &addr_search_ops);
	} else if (!addr_linked) {
		shavlh_init(&freed->addr_node);
		if (left)
			shavl_insert(&ext->addr_tree, &freed->addr_node,
				&addr_search_ops);
		else
			shavl_prepend(&ext->addr_tree, &freed->addr_node,
				      &addr_search_ops);
	}

	shavlh_init(&freed->size_node);
	shavl_insert_back(&ext->size_tree, &freed->size_node,
			  &size_search_ops);
	mark_pages(ext, addr_to_pagenr(ext, page),
		   size >> SHEAPMEM_PAGE_SHIFT, page_free);
}

static void add_page_front(struct shared_heap_memory *heap,
			   struct sheapmem_extent *ext,
			   int pg, int log2size)
{
	struct sheapmem_pgentry *new, *head, *next;
	int ilog;

	/* Insert page at front of the per-bucket page list. */
	
	ilog = log2size - SHEAPMEM_MIN_LOG2;
	new = &ext->pagemap[pg];
	if (heap->buckets[ilog] == -1U) {
		heap->buckets[ilog] = pg;
		new->prev = new->next = pg;
	} else {
		head = &ext->pagemap[heap->buckets[ilog]];
		new->prev = heap->buckets[ilog];
		new->next = head->next;
		next = &ext->pagemap[new->next];
		next->prev = pg;
		head->next = pg;
		heap->buckets[ilog] = pg;
	}
}

static void remove_page(struct shared_heap_memory *heap,
			struct sheapmem_extent *ext,
			int pg, int log2size)
{
	struct sheapmem_pgentry *old, *prev, *next;
	int ilog = log2size - SHEAPMEM_MIN_LOG2;

	/* Remove page from the per-bucket page list. */

	old = &ext->pagemap[pg];
	if (pg == old->next)
		heap->buckets[ilog] = -1U;
	else {
		if (pg == heap->buckets[ilog])
			heap->buckets[ilog] = old->next;
		prev = &ext->pagemap[old->prev];
		prev->next = old->next;
		next = &ext->pagemap[old->next];
		next->prev = old->prev;
	}
}

static void move_page_front(struct shared_heap_memory *heap,
			    struct sheapmem_extent *ext,
			    int pg, int log2size)
{
	int ilog = log2size - SHEAPMEM_MIN_LOG2;

	/* Move page at front of the per-bucket page list. */
	
	if (heap->buckets[ilog] == pg)
		return;	 /* Already at front, no move. */
		
	remove_page(heap, ext, pg, log2size);
	add_page_front(heap, ext, pg, log2size);
}

static void move_page_back(struct shared_heap_memory *heap,
			   struct sheapmem_extent *ext,
			   int pg, int log2size)
{
	struct sheapmem_pgentry *old, *last, *head, *next;
	int ilog;

	/* Move page at end of the per-bucket page list. */
	
	old = &ext->pagemap[pg];
	if (pg == old->next) /* Singleton, no move. */
		return;
		
	remove_page(heap, ext, pg, log2size);

	ilog = log2size - SHEAPMEM_MIN_LOG2;
	head = &ext->pagemap[heap->buckets[ilog]];
	last = &ext->pagemap[head->prev];
	old->prev = head->prev;
	old->next = last->next;
	next = &ext->pagemap[old->next];
	next->prev = pg;
	last->next = pg;
}

static void *add_free_range(struct shared_heap_memory *heap, size_t bsize, int log2size)
{
	struct sheapmem_extent *ext;
	size_t rsize;
	int pg;

	/*
	 * Scanning each extent, search for a range of contiguous
	 * pages in the extent. The range must be at least @bsize
	 * long. @pg is the heading page number on success.
	 */
	rsize =__align_to(bsize, SHEAPMEM_PAGE_SIZE);
	__list_for_each_entry(main_base, ext, &heap->extents, next) {
		pg = reserve_page_range(ext, rsize);
		if (pg >= 0)
			goto found;
	}

	return NULL;

found:	
	/*
	 * Update the page entry.  If @log2size is non-zero
	 * (i.e. bsize < SHEAPMEM_PAGE_SIZE), bsize is (1 << log2Size)
	 * between 2^SHEAPMEM_MIN_LOG2 and 2^(SHEAPMEM_PAGE_SHIFT - 1).
	 * Save the log2 power into entry.type, then update the
	 * per-page allocation bitmap to reserve the first block.
	 *
	 * Otherwise, we have a larger block which may span multiple
	 * pages: set entry.type to page_list, indicating the start of
	 * the page range, and entry.bsize to the overall block size.
	 */
	if (log2size) {
		ext->pagemap[pg].type = log2size;
		/*
		 * Mark the first object slot (#0) as busy, along with
		 * the leftmost bits we won't use for this log2 size.
		 */
		ext->pagemap[pg].map = ~gen_block_mask(log2size) | 1;
		/*
		 * Insert the new page at front of the per-bucket page
		 * list, enforcing the assumption that pages with free
		 * space live close to the head of this list.
		 */
		add_page_front(heap, ext, pg, log2size);
	} else {
		ext->pagemap[pg].type = page_list;
		ext->pagemap[pg].bsize = (uint32_t)bsize;
		mark_pages(ext, pg + 1,
			   (bsize >> SHEAPMEM_PAGE_SHIFT) - 1, page_cont);
	}

	heap->used_size += bsize;

	return pagenr_to_addr(ext, pg);
}

static void *sheapmem_alloc(struct shared_heap_memory *heap, size_t size)
{
	struct sheapmem_extent *ext;
	int log2size, ilog, pg, b;
	uint32_t bmask;
	size_t bsize;
	void *block;

	if (size == 0)
		return NULL;

	if (size < SHEAPMEM_MIN_ALIGN) {
		bsize = size = SHEAPMEM_MIN_ALIGN;
		log2size = SHEAPMEM_MIN_LOG2;
	} else {
		log2size = sizeof(size) * CHAR_BIT - 1 -
			xenomai_count_leading_zeros(size);
		if (log2size < SHEAPMEM_PAGE_SHIFT) {
			if (size & (size - 1))
				log2size++;
			bsize = 1 << log2size;
		} else
			bsize = __align_to(size, SHEAPMEM_PAGE_SIZE);
	}
	
	/*
	 * Allocate entire pages directly from the pool whenever the
	 * block is larger or equal to SHEAPMEM_PAGE_SIZE.  Otherwise,
	 * use bucketed memory.
	 *
	 * NOTE: Fully busy pages from bucketed memory are moved back
	 * at the end of the per-bucket page list, so that we may
	 * always assume that either the heading page has some room
	 * available, or no room is available from any page linked to
	 * this list, in which case we should immediately add a fresh
	 * page.
	 */
	if (bsize < SHEAPMEM_PAGE_SIZE) {
		ilog = log2size - SHEAPMEM_MIN_LOG2;
		assert(ilog >= 0 && ilog < SHEAPMEM_MAX);

		write_lock_nocancel(&heap->lock);

		__list_for_each_entry(main_base, ext, &heap->extents, next) {
			pg = heap->buckets[ilog];
			if (pg < 0) /* Empty page list? */
				continue;

			/*
			 * Find a block in the heading page. If there
			 * is none, there won't be any down the list:
			 * add a new page right away.
			 */
			bmask = ext->pagemap[pg].map;
			if (bmask == -1U)
				break;
			b = __ctz(~bmask);

			/*
			 * Got one block from the heading per-bucket
			 * page, tag it as busy in the per-page
			 * allocation map.
			 */
			ext->pagemap[pg].map |= (1U << b);
			heap->used_size += bsize;
			block = __shref(main_base, ext->membase) +
				(pg << SHEAPMEM_PAGE_SHIFT) +
				(b << log2size);
			if (ext->pagemap[pg].map == -1U)
				move_page_back(heap, ext, pg, log2size);
			goto out;
		}

		/* No free block in bucketed memory, add one page. */
		block = add_free_range(heap, bsize, log2size);
	} else {
		write_lock_nocancel(&heap->lock);
		/* Add a range of contiguous free pages. */
		block = add_free_range(heap, bsize, 0);
	}
out:
	write_unlock(&heap->lock);

	return block;
}

static int sheapmem_free(struct shared_heap_memory *heap, void *block)
{
	int log2size, ret = 0, pg, n;
	struct sheapmem_extent *ext;
	memoff_t pgoff, boff;
	uint32_t oldmap;
	size_t bsize;

	write_lock_nocancel(&heap->lock);

	/*
	 * Find the extent from which the returned block is
	 * originating from.
	 */
	__list_for_each_entry(main_base, ext, &heap->extents, next) {
		if (__shoff(main_base, block) >= ext->membase &&
		    __shoff(main_base, block) < ext->memlim)
			goto found;
	}

	goto bad;
found:
	/* Compute the heading page number in the page map. */
	pgoff = __shoff(main_base, block) - ext->membase;
	pg = pgoff >> SHEAPMEM_PAGE_SHIFT;
	if (!page_is_valid(ext, pg))
		goto bad;
	
	switch (ext->pagemap[pg].type) {
	case page_list:
		bsize = ext->pagemap[pg].bsize;
		assert((bsize & (SHEAPMEM_PAGE_SIZE - 1)) == 0);
		release_page_range(ext, pagenr_to_addr(ext, pg), bsize);
		break;

	default:
		log2size = ext->pagemap[pg].type;
		bsize = (1 << log2size);
		assert(bsize < SHEAPMEM_PAGE_SIZE);
		boff = pgoff & ~SHEAPMEM_PAGE_MASK;
		if ((boff & (bsize - 1)) != 0) /* Not at block start? */
			goto bad;

		n = boff >> log2size; /* Block position in page. */
		oldmap = ext->pagemap[pg].map;
		ext->pagemap[pg].map &= ~(1U << n);

		/*
		 * If the page the block was sitting on is fully idle,
		 * return it to the pool. Otherwise, check whether
		 * that page is transitioning from fully busy to
		 * partially busy state, in which case it should move
		 * toward the front of the per-bucket page list.
		 */
		if (ext->pagemap[pg].map == ~gen_block_mask(log2size)) {
			remove_page(heap, ext, pg, log2size);
			release_page_range(ext, pagenr_to_addr(ext, pg),
					   SHEAPMEM_PAGE_SIZE);
		} else if (oldmap == -1U)
			move_page_front(heap, ext, pg, log2size);
	}

	heap->used_size -= bsize;
out:
	write_unlock(&heap->lock);

	return __bt(ret);
bad:
	ret = -EINVAL;
	goto out;
}

static inline int compare_range_by_size(const struct shavlh *l, const struct shavlh *r)
{
	struct sheapmem_range *rl = container_of(l, typeof(*rl), size_node);
	struct sheapmem_range *rr = container_of(r, typeof(*rl), size_node);

	return avl_sign((long)(rl->size - rr->size));
}
static DECLARE_SHAVL_SEARCH(search_range_by_size, compare_range_by_size);

static struct shavl_searchops size_search_ops = {
	.search = search_range_by_size,
	.cmp = compare_range_by_size,
};

static inline int compare_range_by_addr(const struct shavlh *l, const struct shavlh *r)
{
	uintptr_t al = (uintptr_t)l, ar = (uintptr_t)r;

	return avl_cmp_sign(al, ar);
}
static DECLARE_SHAVL_SEARCH(search_range_by_addr, compare_range_by_addr);

static struct shavl_searchops addr_search_ops = {
	.search = search_range_by_addr,
	.cmp = compare_range_by_addr,
};

static int add_extent(struct shared_heap_memory *heap, void *base,
		      void *mem, size_t size)
{
	size_t user_size, overhead;
	struct sheapmem_extent *ext;
	int nrpages, state;

	/*
	 * @size must include the overhead memory we need for storing
	 * our meta data as calculated by SHEAPMEM_ARENA_SIZE(), find
	 * this amount back.
	 *
	 * o = overhead
	 * e = sizeof(sheapmem_extent)
	 * p = SHEAPMEM_PAGE_SIZE
	 * m = SHEAPMEM_PGMAP_BYTES
	 *
	 * o = align_to(((a * m + e * p) / (p + m)), minlog2)
	 */
	overhead = __align_to((size * SHEAPMEM_PGMAP_BYTES +
			       sizeof(*ext) * SHEAPMEM_PAGE_SIZE) /
			      (SHEAPMEM_PAGE_SIZE + SHEAPMEM_PGMAP_BYTES),
			      SHEAPMEM_MIN_ALIGN);

	user_size = size - overhead;
	if (user_size & ~SHEAPMEM_PAGE_MASK)
		return -EINVAL;

	if (user_size < SHEAPMEM_PAGE_SIZE ||
	    user_size > SHEAPMEM_MAX_EXTSZ)
		return -EINVAL;
		
	/*
	 * Setup an extent covering user_size bytes of user memory
	 * starting at @mem. user_size must be a multiple of
	 * SHEAPMEM_PAGE_SIZE.  The extent starts with a descriptor,
	 * followed by the array of page entries.
	 *
	 * Page entries contain per-page metadata for managing the
	 * page pool.
	 *
	 * +-------------------+ <= mem
	 * | extent descriptor |
	 * /...................\
	 * \...page entries[]../
	 * /...................\
	 * +-------------------+ <= extent->membase
	 * |                   |
	 * |                   |
	 * |    (page pool)    |
	 * |                   |
	 * |                   |
	 * +-------------------+
	 *                       <= extent->memlim == mem + size
	 */
	nrpages = user_size >> SHEAPMEM_PAGE_SHIFT;
	ext = mem;
	ext->membase = __shoff(base, mem) + overhead;
	ext->memlim = __shoff(base, mem) + size;
		      
	memset(ext->pagemap, 0, nrpages * sizeof(struct sheapmem_pgentry));

	/*
	 * The free page pool is maintained as a set of ranges of
	 * contiguous pages indexed by address and size in AVL
	 * trees. Initially, we have a single range in those trees
	 * covering the whole user memory we have been given for the
	 * extent. Over time, that range will be split then possibly
	 * re-merged back as allocations and deallocations take place.
	 */
	shavl_init(&ext->size_tree);
	shavl_init(&ext->addr_tree);
	release_page_range(ext, __shref(base, ext->membase), user_size);

	write_lock_safe(&heap->lock, state);
	__list_append(base, &ext->next, &heap->extents);
	heap->arena_size += size;
	heap->usable_size += user_size;
	write_unlock_safe(&heap->lock, state);

	return 0;
}

static int sheapmem_init(struct shared_heap_memory *heap, void *base,
			 const char *name,
			 void *mem, size_t size)
{
	pthread_mutexattr_t mattr;
	int ret, n;

	namecpy(heap->name, name);
	heap->used_size = 0;
	heap->usable_size = 0;
	heap->arena_size = 0;
	__list_init_nocheck(base, &heap->extents);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	ret = __bt(-__RT(pthread_mutex_init(&heap->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	/* Reset bucket page lists, all empty. */
	for (n = 0; n < SHEAPMEM_MAX; n++)
		heap->buckets[n] = -1U;

	ret = add_extent(heap, base, mem, size);
	if (ret) {
		__RT(pthread_mutex_destroy(&heap->lock));
		return ret;
	}

	return 0;
}

static int init_main_heap(struct session_heap *m_heap,
			  size_t size)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = sheapmem_init(&m_heap->heap, m_heap, "main", m_heap + 1, size);
	if (ret)
		return __bt(ret);

	m_heap->cpid = get_thread_pid();

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	ret = __bt(-__RT(pthread_mutex_init(&m_heap->sysgroup.lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	__hash_init(m_heap, &m_heap->catalog);
	m_heap->sysgroup.thread_count = 0;
	__list_init(m_heap, &m_heap->sysgroup.thread_list);
	m_heap->sysgroup.heap_count = 0;
	__list_init(m_heap, &m_heap->sysgroup.heap_list);

	return 0;
}

#ifndef CONFIG_XENO_REGISTRY
static void unlink_main_heap(void)
{
	/*
	 * Only the master process run this when there is no registry
	 * support (i.e. the one which has initialized the main shared
	 * heap for the session). When the registry is enabled,
	 * sysregd does the housekeeping.
	 */
	shm_unlink(main_pool.fsname);
}
#endif

static int create_main_heap(pid_t *cnode_r)
{
	const char *session = __copperplate_setup_data.session_label;
	size_t size = __copperplate_setup_data.mem_pool, pagesz;
	gid_t gid =__copperplate_setup_data.session_gid;
	struct heapobj *hobj = &main_pool;
	struct session_heap *m_heap;
	struct stat sbuf;
	memoff_t len;
	int ret, fd;

	*cnode_r = -1;
	pagesz = sysconf(_SC_PAGESIZE);

	/*
	 * A storage page should be obviously larger than an extent
	 * header, but we still make sure of this in debug mode, so
	 * that we can rely on __align_to() for rounding to the
	 * minimum size in production builds, without any further
	 * test (e.g. like size >= sizeof(struct sheapmem_extent)).
	 */
	assert(SHEAPMEM_PAGE_SIZE > sizeof(struct sheapmem_extent));
	size = SHEAPMEM_ARENA_SIZE(size);
	len = __align_to(size + sizeof(*m_heap), pagesz);

	/*
	 * Bind to (and optionally create) the main session's heap:
	 *
	 * If the heap already exists, check whether the leading
	 * process who created it is still alive, in which case we'll
	 * bind to it, unless the requested size differs.
	 *
	 * Otherwise, create the heap for the new emerging session and
	 * bind to it.
	 */
	snprintf(hobj->name, sizeof(hobj->name), "%s.heap", session);
	snprintf(hobj->fsname, sizeof(hobj->fsname),
		 "/xeno:%s", hobj->name);

	fd = shm_open(hobj->fsname, O_RDWR|O_CREAT, 0660);
	if (fd < 0)
		return __bt(-errno);

	ret = flock(fd, LOCK_EX);
	if (__bterrno(ret))
		goto errno_fail;

	ret = fstat(fd, &sbuf);
	if (__bterrno(ret))
		goto errno_fail;

	if (sbuf.st_size == 0)
		goto init;

	m_heap = __STD(mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
	if (m_heap == MAP_FAILED) {
		ret = __bt(-errno);
		goto close_fail;
	}

	if (m_heap->cpid == 0)
		goto reset;

	if (copperplate_probe_tid(m_heap->cpid) == 0) {
		if (m_heap->maplen == len) {
			/* CAUTION: __moff() depends on __main_heap. */
			__main_heap = m_heap;
			__main_sysgroup = &m_heap->sysgroup;
			hobj->pool_ref = __moff(&m_heap->heap);
			goto done;
		}
		*cnode_r = m_heap->cpid;
		munmap(m_heap, len);
		__STD(close(fd));
		return __bt(-EEXIST);
	}
reset:
	munmap(m_heap, len);
	/*
	 * Reset shared memory ownership to revoke permissions from a
	 * former session with more permissive access rules, such as
	 * group-controlled access.
	 */
	ret = fchown(fd, geteuid(), getegid());
	(void)ret;
init:
#ifndef CONFIG_XENO_REGISTRY
	atexit(unlink_main_heap);
#endif

	ret = ftruncate(fd, 0);  /* Clear all previous contents if any. */
	if (__bterrno(ret))
		goto unlink_fail;

	ret = ftruncate(fd, len);
	if (__bterrno(ret))
		goto unlink_fail;

	/*
	 * If we need to share the heap between members of a group,
	 * give the group RW access to the shared memory file backing
	 * the heap.
	 */
	if (gid != USHRT_MAX) {
		ret = fchown(fd, geteuid(), gid);
		if (__bterrno(ret) < 0)
			goto unlink_fail;
		ret = fchmod(fd, 0660);
		if (__bterrno(ret) < 0)
			goto unlink_fail;
	}

	m_heap = __STD(mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
	if (m_heap == MAP_FAILED) {
		ret = __bt(-errno);
		goto unlink_fail;
	}

	__main_heap = m_heap;

	m_heap->maplen = len;
	/* CAUTION: init_main_heap() depends on hobj->pool_ref. */
	hobj->pool_ref = __moff(&m_heap->heap);
	ret = __bt(init_main_heap(m_heap, size));
	if (ret) {
		errno = -ret;
		goto unmap_fail;
	}

	/* We need these globals set up before updating a sysgroup. */
	__main_sysgroup = &m_heap->sysgroup;
	sysgroup_add(heap, &m_heap->heap.memspec);
done:
	flock(fd, LOCK_UN);
	__STD(close(fd));
	hobj->size = m_heap->heap.usable_size;
	__main_catalog = &m_heap->catalog;

	return 0;
unmap_fail:
	munmap(m_heap, len);
unlink_fail:
	ret = -errno;
	shm_unlink(hobj->fsname);
	goto close_fail;
errno_fail:
	ret = __bt(-errno);
close_fail:
	__STD(close(fd));

	return ret;
}

static int bind_main_heap(const char *session)
{
	struct heapobj *hobj = &main_pool;
	struct session_heap *m_heap;
	int ret, fd, cpid;
	struct stat sbuf;
	memoff_t len;

	/* No error tracking, this is for internal users. */

	snprintf(hobj->name, sizeof(hobj->name), "%s.heap", session);
	snprintf(hobj->fsname, sizeof(hobj->fsname),
		 "/xeno:%s", hobj->name);

	fd = shm_open(hobj->fsname, O_RDWR, 0400);
	if (fd < 0)
		return -errno;

	ret = flock(fd, LOCK_EX);
	if (ret)
		goto errno_fail;

	ret = fstat(fd, &sbuf);
	if (ret)
		goto errno_fail;

	len = sbuf.st_size;
	if (len < sizeof(*m_heap)) {
		ret = -EINVAL;
		goto fail;
	}

	m_heap = __STD(mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0));
	if (m_heap == MAP_FAILED)
		goto errno_fail;

	cpid = m_heap->cpid;
	__STD(close(fd));

	if (cpid == 0 || copperplate_probe_tid(cpid)) {
		munmap(m_heap, len);
		return -ENOENT;
	}

	hobj->pool_ref = __moff(&m_heap->heap);
	hobj->size = m_heap->heap.usable_size;
	__main_heap = m_heap;
	__main_catalog = &m_heap->catalog;
	__main_sysgroup = &m_heap->sysgroup;

	return 0;

errno_fail:
	ret = -errno;
fail:
	__STD(close(fd));

	return ret;
}

int pshared_check(void *__heap, void *__addr)
{
	struct shared_heap_memory *heap = __heap;
	struct sheapmem_extent *extent;
	struct session_heap *m_heap;

	/*
	 * Fast check for the main heap: we have a single extent for
	 * this one, so the address shall fall into the file-backed
	 * memory range.
	 */
	if (__moff(heap) == main_pool.pool_ref) {
		m_heap = container_of(heap, struct session_heap, heap);
		return __addr >= (void *)m_heap &&
			__addr < (void *)m_heap + m_heap->maplen;
	}

	/*
	 * Secondary (nested) heap: some refs may fall into the
	 * header, check for this first.
	 */
	if (__addr >= __heap && __addr < __heap + sizeof(*heap))
		return 1;

	/*
	 * This address must be referring to some payload data within
	 * the nested heap, check that it falls into one of the heap
	 * extents.
	 */
	assert(!list_empty(&heap->extents));

	__list_for_each_entry(main_base, extent, &heap->extents, next) {
		if (__shoff(main_base, __addr) >= extent->membase &&
		    __shoff(main_base, __addr) < extent->memlim)
			return 1;
	}

	return 0;
}

int heapobj_init(struct heapobj *hobj, const char *name, size_t size)
{
	const char *session = __copperplate_setup_data.session_label;
	struct shared_heap_memory *heap;
	size_t len;

	size = SHEAPMEM_ARENA_SIZE(size);
	len = size + sizeof(*heap);

	/*
	 * Create a heap nested in the main shared heap to hold data
	 * we can share among processes which belong to the same
	 * session.
	 */
	heap = sheapmem_alloc(&main_heap.heap, len);
	if (heap == NULL) {
		warning("%s() failed for %Zu bytes, raise --mem-pool-size?",
			__func__, len);
		return __bt(-ENOMEM);
	}

	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s.%s",
			 session, name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%s.%p",
			 session, hobj);

	sheapmem_init(heap, main_base, hobj->name, heap + 1, size);
	hobj->pool_ref = __moff(heap);
	hobj->size = heap->usable_size;
	sysgroup_add(heap, &heap->memspec);

	return 0;
}

int heapobj_init_array(struct heapobj *hobj, const char *name,
		       size_t size, int elems)
{
	size = __align_to(size, SHEAPMEM_MIN_ALIGN);
	return __bt(heapobj_init(hobj, name, size * elems));
}

void heapobj_destroy(struct heapobj *hobj)
{
	struct shared_heap_memory *heap = __mptr(hobj->pool_ref);
	int cpid;

	if (hobj != &main_pool) {
		__RT(pthread_mutex_destroy(&heap->lock));
		sysgroup_remove(heap, &heap->memspec);
		sheapmem_free(&main_heap.heap, heap);
		return;
	}

	cpid = main_heap.cpid;
	if (cpid != 0 && cpid != get_thread_pid() &&
	    copperplate_probe_tid(cpid) == 0) {
		munmap(&main_heap, main_heap.maplen);
		return;
	}
	
	__RT(pthread_mutex_destroy(&heap->lock));
	__RT(pthread_mutex_destroy(&main_heap.sysgroup.lock));
	munmap(&main_heap, main_heap.maplen);
	shm_unlink(hobj->fsname);
}

int heapobj_extend(struct heapobj *hobj, size_t size, void *unused)
{
	struct shared_heap_memory *heap = __mptr(hobj->pool_ref);
	void *mem;
	int ret;

	if (hobj == &main_pool)	/* Can't extend the main pool. */
		return __bt(-EINVAL);

	size = SHEAPMEM_ARENA_SIZE(size);
	mem = sheapmem_alloc(&main_heap.heap, size);
	if (mem == NULL)
		return __bt(-ENOMEM);

	ret = add_extent(heap, main_base, mem, size);
	if (ret) {
		sheapmem_free(&main_heap.heap, mem);
		return __bt(ret);
	}

	hobj->size += size;

	return 0;
}

void *heapobj_alloc(struct heapobj *hobj, size_t size)
{
	return sheapmem_alloc(__mptr(hobj->pool_ref), size);
}

void heapobj_free(struct heapobj *hobj, void *ptr)
{
	sheapmem_free(__mptr(hobj->pool_ref), ptr);
}

size_t heapobj_validate(struct heapobj *hobj, void *ptr)
{
	ssize_t ret = sheapmem_check(__mptr(hobj->pool_ref), ptr);
	return ret < 0 ? 0 : ret;
}

size_t heapobj_inquire(struct heapobj *hobj)
{
	struct shared_heap_memory *heap = __mptr(hobj->pool_ref);
	return heap->used_size;
}

size_t heapobj_get_size(struct heapobj *hobj)
{
	struct shared_heap_memory *heap = __mptr(hobj->pool_ref);
	return heap->usable_size;
}

void *xnmalloc(size_t size)
{
	return sheapmem_alloc(&main_heap.heap, size);
}

void xnfree(void *ptr)
{
	sheapmem_free(&main_heap.heap, ptr);
}

char *xnstrdup(const char *ptr)
{
	char *str;

	str = xnmalloc(strlen(ptr) + 1);
	if (str == NULL)
		return NULL;

	return strcpy(str, ptr);
}

int heapobj_pkg_init_shared(void)
{
	pid_t cnode;
	int ret;

	ret = create_main_heap(&cnode);
	if (ret == -EEXIST)
		warning("session %s is still active (pid %d)\n",
			__copperplate_setup_data.session_label, cnode);

	return __bt(ret);
}

int heapobj_bind_session(const char *session)
{
	/* No error tracking, this is for internal users. */
	return bind_main_heap(session);
}

void heapobj_unbind_session(void)
{
	size_t len = main_heap.maplen;

	munmap(&main_heap, len);
}

int heapobj_unlink_session(const char *session)
{
	char *path;
	int ret;

	ret = asprintf(&path, "/xeno:%s.heap", session);
	if (ret < 0)
		return -ENOMEM;
	ret = shm_unlink(path) ? -errno : 0;
	free(path);

	return ret;
}

/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>.
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
 * This code implements a variant of the allocator described in
 * "Design of a General Purpose Memory Allocator for the 4.3BSD Unix
 * Kernel" by Marshall K. McKusick and Michael J. Karels (USENIX
 * 1988), see http://docs.FreeBSD.org/44doc/papers/kernmalloc.pdf.
 * The free page list is maintained in AVL trees for fast lookups of
 * multi-page memory ranges, and pages holding bucketed memory have a
 * fast allocation bitmap to manage their blocks internally.
 */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <boilerplate/heapmem.h>

enum heapmem_pgtype {
	page_free =0,
	page_cont =1,
	page_list =2
};

static struct avl_searchops size_search_ops;
static struct avl_searchops addr_search_ops;

static inline uint32_t __attribute__ ((always_inline))
gen_block_mask(int log2size)
{
	return -1U >> (32 - (HEAPMEM_PAGE_SIZE >> log2size));
}

static inline  __attribute__ ((always_inline))
int addr_to_pagenr(struct heapmem_extent *ext, void *p)
{
	return ((void *)p - ext->membase) >> HEAPMEM_PAGE_SHIFT;
}

static inline  __attribute__ ((always_inline))
void *pagenr_to_addr(struct heapmem_extent *ext, int pg)
{
	return ext->membase + (pg << HEAPMEM_PAGE_SHIFT);
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
page_is_valid(struct heapmem_extent *ext, int pg)
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

static void mark_pages(struct heapmem_extent *ext,
		       int pg, int nrpages,
		       enum heapmem_pgtype type)
{
	while (nrpages-- > 0)
		ext->pagemap[pg].type = type;
}

#else

static inline bool
page_is_valid(struct heapmem_extent *ext, int pg)
{
	return true;
}

static void mark_pages(struct heapmem_extent *ext,
		       int pg, int nrpages,
		       enum heapmem_pgtype type)
{ }

#endif

ssize_t heapmem_check(struct heap_memory *heap, void *block)
{
	struct heapmem_extent *ext;
	memoff_t pg, pgoff, boff;
	ssize_t ret = -EINVAL;
	size_t bsize;

	read_lock_nocancel(&heap->lock);

	/*
	 * Find the extent the checked block is originating from.
	 */
	pvlist_for_each_entry(ext, &heap->extents, next) {
		if (block >= ext->membase &&
		    block < ext->memlim)
			goto found;
	}
	goto out;
found:
	/* Calculate the page number from the block address. */
	pgoff = block - ext->membase;
	pg = pgoff >> HEAPMEM_PAGE_SHIFT;
	if (page_is_valid(ext, pg)) {
		if (ext->pagemap[pg].type == page_list)
			bsize = ext->pagemap[pg].bsize;
		else {
			bsize = (1 << ext->pagemap[pg].type);
			boff = pgoff & ~HEAPMEM_PAGE_MASK;
			if ((boff & (bsize - 1)) != 0) /* Not at block start? */
				goto out;
		}
		ret = (ssize_t)bsize;
	}
out:
	read_unlock(&heap->lock);

	return ret;
}

static inline struct heapmem_range *
find_suitable_range(struct heapmem_extent *ext, size_t size)
{
	struct heapmem_range lookup;
	struct avlh *node;

	lookup.size = size;
	node = avl_search_ge(&ext->size_tree, &lookup.size_node,
			     &size_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct heapmem_range, size_node);
}

static int reserve_page_range(struct heapmem_extent *ext, size_t size)
{
	struct heapmem_range *new, *splitr;

	new = find_suitable_range(ext, size);
	if (new == NULL)
		return -1;

	avl_delete(&ext->size_tree, &new->size_node);
	if (new->size == size) {
		avl_delete(&ext->addr_tree, &new->addr_node);
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
	new = (struct heapmem_range *)((void *)new + splitr->size);
	avlh_init(&splitr->size_node);
	avl_insert_back(&ext->size_tree, &splitr->size_node,
			&size_search_ops);

	return addr_to_pagenr(ext, new);
}

static inline struct heapmem_range *
find_left_neighbour(struct heapmem_extent *ext, struct heapmem_range *r)
{
	struct avlh *node;

	node = avl_search_le(&ext->addr_tree, &r->addr_node,
			     &addr_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct heapmem_range, addr_node);
}

static inline struct heapmem_range *
find_right_neighbour(struct heapmem_extent *ext, struct heapmem_range *r)
{
	struct avlh *node;

	node = avl_search_ge(&ext->addr_tree, &r->addr_node,
			     &addr_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct heapmem_range, addr_node);
}

static inline struct heapmem_range *
find_next_neighbour(struct heapmem_extent *ext, struct heapmem_range *r)
{
	struct avlh *node;

	node = avl_next(&ext->addr_tree, &r->addr_node);
	if (node == NULL)
		return NULL;

	return container_of(node, struct heapmem_range, addr_node);
}

static inline bool
ranges_mergeable(struct heapmem_range *left, struct heapmem_range *right)
{
	return (void *)left + left->size == (void *)right;
}

static void release_page_range(struct heapmem_extent *ext,
			       void *page, size_t size)
{
	struct heapmem_range *freed = page, *left, *right;
	bool addr_linked = false;

	freed->size = size;

	left = find_left_neighbour(ext, freed);
	if (left && ranges_mergeable(left, freed)) {
		avl_delete(&ext->size_tree, &left->size_node);
		left->size += freed->size;
		freed = left;
		addr_linked = true;
		right = find_next_neighbour(ext, freed);
	} else
		right = find_right_neighbour(ext, freed);

	if (right && ranges_mergeable(freed, right)) {
		avl_delete(&ext->size_tree, &right->size_node);
		freed->size += right->size;
		if (addr_linked)
			avl_delete(&ext->addr_tree, &right->addr_node);
		else
			avl_replace(&ext->addr_tree, &right->addr_node,
				    &freed->addr_node, &addr_search_ops);
	} else if (!addr_linked) {
		avlh_init(&freed->addr_node);
		if (left)
			avl_insert(&ext->addr_tree, &freed->addr_node,
				   &addr_search_ops);
		else
			avl_prepend(&ext->addr_tree, &freed->addr_node,
				    &addr_search_ops);
	}

	avlh_init(&freed->size_node);
	avl_insert_back(&ext->size_tree, &freed->size_node,
			&size_search_ops);
	mark_pages(ext, addr_to_pagenr(ext, page),
		   size >> HEAPMEM_PAGE_SHIFT, page_free);
}

static void add_page_front(struct heap_memory *heap,
			   struct heapmem_extent *ext,
			   int pg, int log2size)
{
	struct heapmem_pgentry *new, *head, *next;
	int ilog;

	/* Insert page at front of the per-bucket page list. */
	
	ilog = log2size - HEAPMEM_MIN_LOG2;
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

static void remove_page(struct heap_memory *heap,
			struct heapmem_extent *ext,
			int pg, int log2size)
{
	struct heapmem_pgentry *old, *prev, *next;
	int ilog = log2size - HEAPMEM_MIN_LOG2;

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

static void move_page_front(struct heap_memory *heap,
			    struct heapmem_extent *ext,
			    int pg, int log2size)
{
	int ilog = log2size - HEAPMEM_MIN_LOG2;

	/* Move page at front of the per-bucket page list. */
	
	if (heap->buckets[ilog] == pg)
		return;	 /* Already at front, no move. */
		
	remove_page(heap, ext, pg, log2size);
	add_page_front(heap, ext, pg, log2size);
}

static void move_page_back(struct heap_memory *heap,
			   struct heapmem_extent *ext,
			   int pg, int log2size)
{
	struct heapmem_pgentry *old, *last, *head, *next;
	int ilog;

	/* Move page at end of the per-bucket page list. */
	
	old = &ext->pagemap[pg];
	if (pg == old->next) /* Singleton, no move. */
		return;
		
	remove_page(heap, ext, pg, log2size);

	ilog = log2size - HEAPMEM_MIN_LOG2;
	head = &ext->pagemap[heap->buckets[ilog]];
	last = &ext->pagemap[head->prev];
	old->prev = head->prev;
	old->next = last->next;
	next = &ext->pagemap[old->next];
	next->prev = pg;
	last->next = pg;
}

static void *add_free_range(struct heap_memory *heap, size_t bsize, int log2size)
{
	struct heapmem_extent *ext;
	size_t rsize;
	int pg;

	/*
	 * Scanning each extent, search for a range of contiguous
	 * pages in the extent. The range must be at least @bsize
	 * long. @pg is the heading page number on success.
	 */
	rsize =__align_to(bsize, HEAPMEM_PAGE_SIZE);
	pvlist_for_each_entry(ext, &heap->extents, next) {
		pg = reserve_page_range(ext, rsize);
		if (pg >= 0)
			goto found;
	}

	return NULL;

found:	
	/*
	 * Update the page entry.  If @log2size is non-zero
	 * (i.e. bsize < HEAPMEM_PAGE_SIZE), bsize is (1 << log2Size)
	 * between 2^HEAPMEM_MIN_LOG2 and 2^(HEAPMEM_PAGE_SHIFT - 1).
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
			   (bsize >> HEAPMEM_PAGE_SHIFT) - 1, page_cont);
	}

	heap->used_size += bsize;

	return pagenr_to_addr(ext, pg);
}

void *heapmem_alloc(struct heap_memory *heap, size_t size)
{
	struct heapmem_extent *ext;
	int log2size, ilog, pg, b;
	uint32_t bmask;
	size_t bsize;
	void *block;

	if (size == 0)
		return NULL;

	if (size < HEAPMEM_MIN_ALIGN) {
		bsize = size = HEAPMEM_MIN_ALIGN;
		log2size = HEAPMEM_MIN_LOG2;
	} else {
		log2size = sizeof(size) * CHAR_BIT - 1 -
			xenomai_count_leading_zeros(size);
		if (log2size < HEAPMEM_PAGE_SHIFT) {
			if (size & (size - 1))
				log2size++;
			bsize = 1 << log2size;
		} else
			bsize = __align_to(size, HEAPMEM_PAGE_SIZE);
	}
	
	/*
	 * Allocate entire pages directly from the pool whenever the
	 * block is larger or equal to HEAPMEM_PAGE_SIZE.  Otherwise,
	 * use bucketed memory.
	 *
	 * NOTE: Fully busy pages from bucketed memory are moved back
	 * at the end of the per-bucket page list, so that we may
	 * always assume that either the heading page has some room
	 * available, or no room is available from any page linked to
	 * this list, in which case we should immediately add a fresh
	 * page.
	 */
	if (bsize < HEAPMEM_PAGE_SIZE) {
		ilog = log2size - HEAPMEM_MIN_LOG2;
		assert(ilog >= 0 && ilog < HEAPMEM_MAX);

		write_lock_nocancel(&heap->lock);

		pvlist_for_each_entry(ext, &heap->extents, next) {
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
			block = ext->membase +
				(pg << HEAPMEM_PAGE_SHIFT) +
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

int heapmem_free(struct heap_memory *heap, void *block)
{
	int log2size, ret = 0, pg, n;
	struct heapmem_extent *ext;
	memoff_t pgoff, boff;
	uint32_t oldmap;
	size_t bsize;

	write_lock_nocancel(&heap->lock);

	/*
	 * Find the extent from which the returned block is
	 * originating from.
	 */
	pvlist_for_each_entry(ext, &heap->extents, next) {
		if (block >= ext->membase && block < ext->memlim)
			goto found;
	}

	goto bad;
found:
	/* Compute the heading page number in the page map. */
	pgoff = block - ext->membase;
	pg = pgoff >> HEAPMEM_PAGE_SHIFT;
	if (!page_is_valid(ext, pg))
		goto bad;
	
	switch (ext->pagemap[pg].type) {
	case page_list:
		bsize = ext->pagemap[pg].bsize;
		assert((bsize & (HEAPMEM_PAGE_SIZE - 1)) == 0);
		release_page_range(ext, pagenr_to_addr(ext, pg), bsize);
		break;

	default:
		log2size = ext->pagemap[pg].type;
		bsize = (1 << log2size);
		assert(bsize < HEAPMEM_PAGE_SIZE);
		boff = pgoff & ~HEAPMEM_PAGE_MASK;
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
					   HEAPMEM_PAGE_SIZE);
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

static inline int compare_range_by_size(const struct avlh *l, const struct avlh *r)
{
	struct heapmem_range *rl = container_of(l, typeof(*rl), size_node);
	struct heapmem_range *rr = container_of(r, typeof(*rl), size_node);

	return avl_sign((long)(rl->size - rr->size));
}
static DECLARE_AVL_SEARCH(search_range_by_size, compare_range_by_size);

static struct avl_searchops size_search_ops = {
	.search = search_range_by_size,
	.cmp = compare_range_by_size,
};

static inline int compare_range_by_addr(const struct avlh *l, const struct avlh *r)
{
	uintptr_t al = (uintptr_t)l, ar = (uintptr_t)r;

	return avl_cmp_sign(al, ar);
}
static DECLARE_AVL_SEARCH(search_range_by_addr, compare_range_by_addr);

static struct avl_searchops addr_search_ops = {
	.search = search_range_by_addr,
	.cmp = compare_range_by_addr,
};
		
static int add_extent(struct heap_memory *heap, void *mem, size_t size)
{
	size_t user_size, overhead;
	struct heapmem_extent *ext;
	int nrpages, state;

	/*
	 * @size must include the overhead memory we need for storing
	 * our meta data as calculated by HEAPMEM_ARENA_SIZE(), find
	 * this amount back.
	 *
	 * o = overhead
	 * e = sizeof(heapmem_extent)
	 * p = HEAPMEM_PAGE_SIZE
	 * m = HEAPMEM_PGMAP_BYTES
	 *
	 * o = align_to(((a * m + e * p) / (p + m)), minlog2)
	 */
	overhead = __align_to((size * HEAPMEM_PGMAP_BYTES +
			       sizeof(*ext) * HEAPMEM_PAGE_SIZE) /
			      (HEAPMEM_PAGE_SIZE + HEAPMEM_PGMAP_BYTES),
			      HEAPMEM_MIN_ALIGN);

	user_size = size - overhead;
	if (user_size & ~HEAPMEM_PAGE_MASK)
		return -EINVAL;

	if (user_size < HEAPMEM_PAGE_SIZE ||
	    user_size > HEAPMEM_MAX_EXTSZ)
		return -EINVAL;
		
	/*
	 * Setup an extent covering user_size bytes of user memory
	 * starting at @mem. user_size must be a multiple of
	 * HEAPMEM_PAGE_SIZE.  The extent starts with a descriptor,
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
	nrpages = user_size >> HEAPMEM_PAGE_SHIFT;
	ext = mem;
	ext->membase = mem + overhead;
	ext->memlim = mem + size;
		      
	memset(ext->pagemap, 0, nrpages * sizeof(struct heapmem_pgentry));
	/*
	 * The free page pool is maintained as a set of ranges of
	 * contiguous pages indexed by address and size in AVL
	 * trees. Initially, we have a single range in those trees
	 * covering the whole user memory we have been given for the
	 * extent. Over time, that range will be split then possibly
	 * re-merged back as allocations and deallocations take place.
	 */
	avl_init(&ext->size_tree);
	avl_init(&ext->addr_tree);
	release_page_range(ext, ext->membase, user_size);

	write_lock_safe(&heap->lock, state);
	pvlist_append(&ext->next, &heap->extents);
	heap->arena_size += size;
	heap->usable_size += user_size;
	write_unlock_safe(&heap->lock, state);

	return 0;
}

int heapmem_init(struct heap_memory *heap, void *mem, size_t size)
{
	pthread_mutexattr_t mattr;
	int ret, n;

	heap->used_size = 0;
	heap->usable_size = 0;
	heap->arena_size = 0;
	pvlist_init(&heap->extents);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&heap->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	/* Reset bucket page lists, all empty. */
	for (n = 0; n < HEAPMEM_MAX; n++)
		heap->buckets[n] = -1U;

	ret = add_extent(heap, mem, size);
	if (ret) {
		__RT(pthread_mutex_destroy(&heap->lock));
		return ret;
	}

	return 0;
}

int heapmem_extend(struct heap_memory *heap, void *mem, size_t size)
{
	return add_extent(heap, mem, size);
}

void heapmem_destroy(struct heap_memory *heap)
{
	__RT(pthread_mutex_destroy(&heap->lock));
}

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

enum {				/* FIXME: page_free redundant with bitmap */
	page_free =0,
	page_cont =1,
	page_list =2
};

struct page_entry {
	unsigned int type : 8;	  /* free, cont, list or log2 */
	unsigned int bcount : 24; /* Number of active blocks. */
};

struct shared_extent {
	struct holder link;
	memoff_t membase;	/* Base offset of page array */
	memoff_t memlim;	/* Offset limit of page array */
	memoff_t bitmap;	/* Offset of allocation bitmap */
	int bitwords;		/* 32bit words in bitmap */
	struct page_entry pagemap[0]; /* Start of page map */
};

/*
 * The main heap consists of a shared heap at its core, with
 * additional session-wide information.
 */
struct session_heap {
	struct shared_heap heap;
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

#define __shoff(b, p)		((caddr_t)(p) - (caddr_t)(b))
#define __shoff_check(b, p)	((p) ? __shoff(b, p) : 0)
#define __shref(b, o)		((void *)((caddr_t)(b) + (o)))
#define __shref_check(b, o)	((o) ? __shref(b, o) : NULL)

static inline size_t __align_to(size_t size, size_t al)
{
	/* The alignment value must be a power of 2 */
	return ((size+al-1)&(~(al-1)));
}

static inline size_t get_pagemap_size(size_t h,
				      memoff_t *bmapoff, int *bmapwords)
{
	int nrpages = h >> HOBJ_PAGE_SHIFT, bitmapw;
	size_t pagemapsz;

	/*
	 * Return the size of the meta data required to map 'h' bytes
	 * of user memory in pages of HOBJ_PAGE_SIZE bytes. The meta
	 * data includes the length of the extent descriptor, plus the
	 * length of the page mapping array followed by the allocation
	 * bitmap. 'h' must be a multiple of HOBJ_PAGE_SIZE on entry.
	 */
	assert((h & ~HOBJ_PAGE_MASK) == 0);
	pagemapsz = __align_to(nrpages * sizeof(struct page_entry),
			       sizeof(uint32_t));
	bitmapw =__align_to(nrpages, 32) / 32;
	if (bmapoff)
		*bmapoff = offsetof(struct shared_extent, pagemap) + pagemapsz;
	if (bmapwords)
		*bmapwords = bitmapw;

	return __align_to(pagemapsz
			  + sizeof(struct shared_extent)
			  + bitmapw * sizeof(uint32_t),
			  HOBJ_MINALIGNSZ);
}

static void init_extent(void *base, struct shared_extent *extent)
{
	int lastpgnum;
	uint32_t *p;

	__holder_init_nocheck(base, &extent->link);

	lastpgnum = ((extent->memlim - extent->membase) >> HOBJ_PAGE_SHIFT) - 1;
	/*
	 * An extent must contain at least two addressable pages to
	 * cope with allocation sizes between PAGESIZE and 2 *
	 * PAGESIZE.
	 */
	assert(lastpgnum >= 1);

	/* Mark all pages as free in the page map. */
	memset(extent->pagemap, 0, (lastpgnum + 1) * sizeof(struct page_entry));

	/* Clear the allocation bitmap. */
	p = __shref(base, extent->bitmap);
	memset(p, 0, extent->bitwords * sizeof(uint32_t));
	/*
	 * Mark the unused trailing bits (due to alignment) as busy,
	 * we don't want to pick them since they don't map any actual
	 * memory from the page pool.
	 */
	p[lastpgnum / 32] |= ~(-1U >> (31 - (lastpgnum & 31)));
}

static int init_heap(struct shared_heap *heap, void *base,
		     const char *name,
		     void *mem, size_t size)
{
	struct shared_extent *extent;
	pthread_mutexattr_t mattr;
	int ret, bmapwords;
	memoff_t bmapoff;
	size_t metasz;

	namecpy(heap->name, name);

	heap->ubytes = 0;
	heap->total = size;
	heap->maxcont = heap->total;
	__list_init_nocheck(base, &heap->extents);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	ret = __bt(-__RT(pthread_mutex_init(&heap->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	memset(heap->buckets, 0, sizeof(heap->buckets));

	/*
	 * The heap descriptor is followed in memory by the initial
	 * extent covering the 'size' bytes of user memory, which is a
	 * multiple of HOBJ_PAGE_SIZE. The extent starts with a
	 * descriptor, which is in turn followed by a page mapping
	 * array. The length of the page mapping array depends on the
	 * size of the user memory to map.
	 *
	 * +-------------------+
	 * |  heap descriptor  |
	 * +-------------------+
	 * | extent descriptor |
	 * /...................\
	 * \....(page map)...../
	 * /...................\
	 * \.....(bitmap)....../
	 * /...................\
	 * +-------------------+ <= extent->membase
	 * |                   |
	 * |    (page pool)    |
	 * |                   |
	 * +-------------------+
	 *                       <= extent->memlim
	 */
	extent = mem;
	metasz = get_pagemap_size(size, &bmapoff, &bmapwords);
	extent->bitmap = __shoff(base, mem) + bmapoff;
	extent->bitwords = bmapwords;
	extent->membase = __shoff(base, mem) + metasz;
	extent->memlim = extent->membase + size;
	init_extent(base, extent);
	__list_append(base, &extent->link, &heap->extents);

	return 0;
}

static int init_main_heap(struct session_heap *m_heap,
			  size_t size)
{
	pthread_mutexattr_t mattr;
	int ret;

	ret = init_heap(&m_heap->heap, m_heap, "main", m_heap + 1, size);
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

static inline void flip_page_range(uint32_t *p, int b, int nr)
{
	for (;;) {
		*p ^= (1 << b);
		if (--nr == 0)
			return;
		if (--b < 0) {
			b = 31;
			p--;
		}
	}
}

static int reserve_page_range(uint32_t *bitmap, int bitwords, int nrpages)
{
	int n, b, r, seq, beg, end;
	uint32_t v = -1U;

	/*
	 * Look for a free contiguous range of at least nrpages
	 * page(s) in the bitmap. Once found, flip the corresponding
	 * bit sequence from clear to set, then return the heading
	 * page number. Otherwise, return -1 on failure.
	 */
	for (n = 0, seq = 0; n < bitwords; n++) {
		v = bitmap[n];
		if (v == -1U) {
			seq = 0;
			continue;
		}
		b = 0;
		for (;;) {
			r = __ctz(v);
			if (r) {
				seq += r;
				if (seq >= nrpages) {
					beg = n * 32 + b + r - seq;
					end = beg + nrpages - 1;
					flip_page_range(bitmap + end / 32,
							end & 31, nrpages);
					return beg;
				}
			} else {
				seq = 0;
				r = 1;
			}
			b += r;
			v >>= r;
			v |= -1U << (32 - r);
			/*
			 * No more zero bits on the left, and not at
			 * the leftmost position: any ongoing zero bit
			 * sequence stops here in the current word,
			 * short of free bits. Reset the sequence, and
			 * keep searching for one which is at least
			 * nrpages-bit long.
			 */
			if (v == -1U) {
				if (b < 32)
					seq = 0;
				break;
			}
		}
	}
	
	return -1;
}

static inline
caddr_t get_page_addr(void *base,
		      struct shared_extent *extent, int pgnum)
{
	return __shref(base, extent->membase) + (pgnum << HOBJ_PAGE_SHIFT);
}

static caddr_t get_free_range(struct shared_heap *heap, size_t bsize, int log2size)
{
	struct shared_extent *extent;
	void *base = main_base;
	caddr_t block, eblock;
	uint32_t *bitmap;
	size_t areasz;
	int pstart, n;

	/*
	 * Scanning each extent, search for a range of contiguous
	 * pages in the extent's bitmap. The range must be at least
	 * 'bsize' long.
	 */

	areasz =__align_to(bsize, HOBJ_PAGE_SIZE) >> HOBJ_PAGE_SHIFT;
	__list_for_each_entry(base, extent, &heap->extents, link) {
		bitmap = __shref(base, extent->bitmap);
		pstart = reserve_page_range(bitmap, extent->bitwords, areasz);
		if (pstart >= 0)
			goto splitpage;
	}

	return NULL;

splitpage:	
	/*
	 * pstart is the starting page number of a range of contiguous
	 * free pages larger or equal than 'bsize'.
	 */
	if (bsize < HOBJ_PAGE_SIZE) {
		/*
		 * If the allocation size is smaller than the internal
		 * page size, split the page in smaller blocks of this
		 * size, building a free list of bucketed free blocks.
		 */
		for (block = get_page_addr(base, extent, pstart),
		     eblock = block + HOBJ_PAGE_SIZE - bsize;
		     block < eblock; block += bsize)
			*((memoff_t *)block) = __shoff(base, block) + bsize;

		*((memoff_t *)eblock) = 0;
	}

	/*
	 * Update the page map.  If log2size is non-zero (i.e. bsize
	 * <= 2 * PAGESIZE), store it in the slot heading the page
	 * range to record the exact block size (which is a power of
	 * two).
	 *
	 * Otherwise, store the special marker page_list, indicating
	 * the start of a block which size is a multiple of the
	 * standard page size, but not necessarily a power of two.
	 *
	 * Page slots following the heading one bear the page_cont
	 * marker.
	 */
	extent->pagemap[pstart].type = log2size ?: page_list;
	extent->pagemap[pstart].bcount = 1;

	for (n = bsize >> HOBJ_PAGE_SHIFT; n > 1; n--) {
		extent->pagemap[pstart + n - 1].type = page_cont;
		extent->pagemap[pstart + n - 1].bcount = 0;
	}

	return get_page_addr(base, extent, pstart);
}

static inline size_t  __attribute__ ((always_inline))
align_alloc_size(size_t size)
{
	/*
	 * Sizes greater than the page size are rounded to a multiple
	 * of the page size.
	 */
	if (size > HOBJ_PAGE_SIZE)
		return __align_to(size, HOBJ_PAGE_SIZE);

	return __align_to(size, HOBJ_MINALIGNSZ);
}

static void *alloc_block(struct shared_heap *heap, size_t size)
{
	struct shared_extent *extent;
	void *base = main_base;
	size_t pgnum, bsize;
	int log2size, ilog;
	caddr_t block;

	if (size == 0)
		return NULL;

	size = align_alloc_size(size);
	/*
	 * It becomes more space efficient to directly allocate pages
	 * from the free page pool whenever the requested size is
	 * greater than 2 times the page size. Otherwise, use the
	 * bucketed memory blocks.
	 */
	if (size <= HOBJ_PAGE_SIZE * 2) {
		/* Find log2(size). */
		log2size = sizeof(size) * 8 - 1 -
			xenomai_count_leading_zeros(size);
		if (size & (size - 1))
			log2size++;
		/* That is the actual block size we need. */
		bsize = 1 << log2size;
		ilog = log2size - HOBJ_MINLOG2;
		assert(ilog < HOBJ_NBUCKETS);

		write_lock_nocancel(&heap->lock);

		block = __shref_check(base, heap->buckets[ilog].freelist);
		if (block == NULL) {
			block = get_free_range(heap, bsize, log2size);
			if (block == NULL)
				goto done;
			if (bsize < HOBJ_PAGE_SIZE) {
				heap->buckets[ilog].fcount += (HOBJ_PAGE_SIZE >> log2size) - 1;
				heap->buckets[ilog].freelist = *((memoff_t *)block);
			}
		} else {
			if (bsize < HOBJ_PAGE_SIZE)
				--heap->buckets[ilog].fcount;

			/* Search for the source extent of block. */
			__list_for_each_entry(base, extent, &heap->extents, link) {
				if (__shoff(base, block) >= extent->membase &&
				    __shoff(base, block) < extent->memlim)
					goto found;
			}
			assert(0);
		found:
			pgnum = (__shoff(base, block) - extent->membase) >> HOBJ_PAGE_SHIFT;
			++extent->pagemap[pgnum].bcount;
			heap->buckets[ilog].freelist = *((memoff_t *)block);
		}

		heap->ubytes += bsize;
	} else {
		if (size > heap->maxcont)
			return NULL;

		write_lock_nocancel(&heap->lock);

		/* Directly request a free page range. */
		block = get_free_range(heap, size, 0);
		if (block)
			heap->ubytes += size;
	}
done:
	write_unlock(&heap->lock);

	return block;
}

static int free_block(struct shared_heap *heap, void *block)
{
	int log2size, ret = 0, nblocks, xpage, ilog, pagenr,
		maxpages, pghead, pgtail, n;
	struct shared_extent *extent;
	memoff_t *tailp, pgoff, boff;
	caddr_t freep, startp, endp;
	void *base = main_base;
	uint32_t *bitmap;
	size_t bsize;

	write_lock_nocancel(&heap->lock);

	/*
	 * Find the extent from which the returned block is
	 * originating from.
	 */
	__list_for_each_entry(base, extent, &heap->extents, link) {
		if (__shoff(base, block) >= extent->membase &&
		    __shoff(base, block) < extent->memlim)
			goto found;
	}

	ret = -EFAULT;
	goto out;
found:
	/* Compute the heading page number in the page map. */
	pgoff = __shoff(base, block) - extent->membase;
	pghead = pgoff >> HOBJ_PAGE_SHIFT;
	boff = pgoff & ~HOBJ_PAGE_MASK;

	switch (extent->pagemap[pghead].type) {
	case page_free:	/* Unallocated page? */
	case page_cont:	/* Not a range heading page? */
		ret = -EINVAL;
		goto out;

	case page_list:
		pagenr = 1;
		maxpages = (extent->memlim - extent->membase) >> HOBJ_PAGE_SHIFT;
		while (pagenr < maxpages &&
		       extent->pagemap[pghead + pagenr].type == page_cont)
			pagenr++;
		bsize = pagenr * HOBJ_PAGE_SIZE;

	free_pages:
		/* Mark the released pages as free in the extent's page map. */
		for (n = 0; n < pagenr; n++)
			extent->pagemap[pghead + n].type = page_free;

		/* Likewise for the allocation bitmap. */
		bitmap = __shref(base, extent->bitmap);
		pgtail = pghead + pagenr - 1;
		/*
		 * Mark the released page(s) as free in the
		 * bitmap. Caution: this is a reverse scan from the
		 * end of the bitfield mapping the area.
		 */
		flip_page_range(bitmap + pgtail / 32, pgtail & 31, pagenr);
		break;

	default:
		log2size = extent->pagemap[pghead].type;
		bsize = (1 << log2size);
		if ((boff & (bsize - 1)) != 0) { /* Not at block start? */
			ret = -EINVAL;
			goto out;
		}
		/*
		 * Return the page to the free pool if we've just
		 * freed its last busy block. Pages from multi-page
		 * blocks are always pushed to the free pool (bcount
		 * value for the heading page is always 1).
		 */
		ilog = log2size - HOBJ_MINLOG2;
		if (--extent->pagemap[pghead].bcount > 0) {
			/*
			 * Page is still busy after release, return
			 * the block to the free list then bail out.
			 */
			*((memoff_t *)block) = heap->buckets[ilog].freelist;
			heap->buckets[ilog].freelist = __shoff(base, block);
			++heap->buckets[ilog].fcount;
			break;
		}

		/*
		 * The page the block was sitting on is idle, return
		 * it to the pool.
		 */
		pagenr = bsize >> HOBJ_PAGE_SHIFT;
		/*
		 * In the simplest case, we only have a single block
		 * to deal with, which spans multiple consecutive
		 * pages: release it as a range of pages.
		 */
		if (pagenr > 1)
			goto free_pages;

		pagenr = 1;
		nblocks = HOBJ_PAGE_SIZE >> log2size;
		/*
		 * Decrease the free bucket count by the number of
		 * blocks that the empty page we are returning to the
		 * pool may contain. The block we are releasing can't
		 * be part of the free list by definition, hence
		 * nblocks - 1.
		 */
		heap->buckets[ilog].fcount -= (nblocks - 1);
		assert(heap->buckets[ilog].fcount >= 0);

		/*
		 * Easy case: all free blocks are laid on a single
		 * page we are now releasing. Just clear the bucket
		 * and bail out.
		 */
		if (heap->buckets[ilog].fcount == 0) {
			heap->buckets[ilog].freelist = 0;
			goto free_pages;
		}

		/*
		 * Worst case: multiple pages are traversed by the
		 * bucket list. Scan the list to remove all blocks
		 * belonging to the freed page. We are done whenever
		 * all possible blocks from the freed page have been
		 * traversed, or we hit the end of list, whichever
		 * comes first.
		 */
		startp = get_page_addr(base, extent, pghead);
		endp = startp + HOBJ_PAGE_SIZE;
		for (tailp = &heap->buckets[ilog].freelist,
			     freep = __shref_check(base, *tailp), xpage = 1;
		     freep && nblocks > 0;
		     freep = __shref_check(base, *((memoff_t *)freep))) {
			if (freep < startp || freep >= endp) {
				if (xpage) { /* Limit random writes */
					*tailp = __shoff(base, freep);
					xpage = 0;
				}
				tailp = (memoff_t *)freep;
			} else {
				--nblocks;
				xpage = 1;
			}
		}
		*tailp = __shoff_check(base, freep);

		goto free_pages;
	}

	heap->ubytes -= bsize;
out:
	write_unlock(&heap->lock);

	return __bt(ret);
}

static size_t check_block(struct shared_heap *heap, void *block)
{
	size_t pgnum, pgoff, boff, bsize, ret = 0;
	struct shared_extent *extent;
	int ptype, maxpages, pagenr;
	void *base = main_base;

	read_lock_nocancel(&heap->lock);

	/*
	 * Find the extent the checked block is originating from.
	 */
	__list_for_each_entry(base, extent, &heap->extents, link) {
		if (__shoff(base, block) >= extent->membase &&
		    __shoff(base, block) < extent->memlim)
			goto found;
	}
	goto out;
found:
	/* Compute the heading page number in the page map. */
	pgoff = __shoff(base, block) - extent->membase;
	pgnum = pgoff >> HOBJ_PAGE_SHIFT;
	ptype = extent->pagemap[pgnum].type;
	if (ptype == page_free || ptype == page_cont)
		goto out;

	if (ptype == page_list) {
		pagenr = 1;
		maxpages = (extent->memlim - extent->membase) >> HOBJ_PAGE_SHIFT;
		while (pagenr < maxpages &&
		       extent->pagemap[pgnum + pagenr].type == page_cont)
			pagenr++;
		bsize = pagenr * HOBJ_PAGE_SIZE;
	} else {
		bsize = (1 << ptype);
		boff = pgoff & ~HOBJ_PAGE_MASK;
		if ((boff & (bsize - 1)) != 0) /* Not at block start? */
			goto out;
	}

	ret = bsize;
out:
	read_unlock(&heap->lock);

	return ret;
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
	gid_t gid =__copperplate_setup_data.session_gid;
	size_t size = __copperplate_setup_data.mem_pool;
	struct heapobj *hobj = &main_pool;
	struct session_heap *m_heap;
	struct stat sbuf;
	memoff_t len;
	int ret, fd;

	/*
	 * A storage page should be obviously larger than an extent
	 * header, but we still make sure of this in debug mode, so
	 * that we can rely on __align_to() for rounding to the
	 * minimum size in production builds, without any further
	 * test (e.g. like size >= sizeof(struct shared_extent)).
	 */
	assert(HOBJ_PAGE_SIZE > sizeof(struct shared_extent));

	*cnode_r = -1;
	size = __align_to(size, HOBJ_PAGE_SIZE);
	if (size > HOBJ_MAXEXTSZ)
		return __bt(-EINVAL);

	if (size < HOBJ_PAGE_SIZE * 2)
		size = HOBJ_PAGE_SIZE * 2;

	len = size + sizeof(*m_heap);
	len += get_pagemap_size(size, NULL, NULL);

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

	m_heap->maplen = len;
	/* CAUTION: init_main_heap() depends on hobj->pool_ref. */
	hobj->pool_ref = __moff(&m_heap->heap);
	ret = __bt(init_main_heap(m_heap, size));
	if (ret) {
		errno = -ret;
		goto unmap_fail;
	}

	/* We need these globals set up before updating a sysgroup. */
	__main_heap = m_heap;
	__main_sysgroup = &m_heap->sysgroup;
	sysgroup_add(heap, &m_heap->heap.memspec);
done:
	flock(fd, LOCK_UN);
	__STD(close(fd));
	hobj->size = m_heap->heap.total;
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
	hobj->size = m_heap->heap.total;
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
	struct shared_heap *heap = __heap;
	struct shared_extent *extent;
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

	__list_for_each_entry(main_base, extent, &heap->extents, link) {
		if (__shoff(main_base, __addr) >= extent->membase &&
		    __shoff(main_base, __addr) < extent->memlim)
			return 1;
	}

	return 0;
}

int heapobj_init(struct heapobj *hobj, const char *name, size_t size)
{
	const char *session = __copperplate_setup_data.session_label;
	struct shared_heap *heap;
	size_t len;

	size = __align_to(size, HOBJ_PAGE_SIZE);
	if (size > HOBJ_MAXEXTSZ)
		return __bt(-EINVAL);

	if (size < HOBJ_PAGE_SIZE * 2)
		size = HOBJ_PAGE_SIZE * 2;

	len = size + sizeof(*heap);
	len += get_pagemap_size(size, NULL, NULL);

	/*
	 * Create a heap nested in the main shared heap to hold data
	 * we can share among processes which belong to the same
	 * session.
	 */
	heap = alloc_block(&main_heap.heap, len);
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

	init_heap(heap, main_base, hobj->name, heap + 1, size);
	hobj->pool_ref = __moff(heap);
	hobj->size = heap->total;
	sysgroup_add(heap, &heap->memspec);

	return 0;
}

int heapobj_init_array(struct heapobj *hobj, const char *name,
		       size_t size, int elems)
{
	size = align_alloc_size(size);
	return __bt(heapobj_init(hobj, name, size * elems));
}

void heapobj_destroy(struct heapobj *hobj)
{
	struct shared_heap *heap = __mptr(hobj->pool_ref);
	int cpid;

	if (hobj != &main_pool) {
		__RT(pthread_mutex_destroy(&heap->lock));
		sysgroup_remove(heap, &heap->memspec);
		free_block(&main_heap.heap, heap);
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
	struct shared_heap *heap = __mptr(hobj->pool_ref);
	struct shared_extent *extent;
	int state, bmapwords;
	memoff_t bmapoff;
	size_t metasz;

	if (hobj == &main_pool)	/* Can't extend the main pool. */
		return __bt(-EINVAL);

	size = __align_to(size, HOBJ_PAGE_SIZE);
	metasz = get_pagemap_size(size, &bmapoff, &bmapwords);
	extent = alloc_block(&main_heap.heap, size + metasz);
	if (extent == NULL)
		return __bt(-ENOMEM);

	extent->bitmap = __shoff(main_base, extent) + bmapoff;
	extent->bitwords = bmapwords;
	extent->membase = __shoff(main_base, extent) + metasz;
	extent->memlim = extent->membase + size;
	init_extent(main_base, extent);
	write_lock_safe(&heap->lock, state);
	__list_append(heap, &extent->link, &heap->extents);
	if (size > heap->maxcont)
		heap->maxcont = size;
	heap->total += size;
	hobj->size += size;
	write_unlock_safe(&heap->lock, state);

	return 0;
}

void *heapobj_alloc(struct heapobj *hobj, size_t size)
{
	return alloc_block(__mptr(hobj->pool_ref), size);
}

void heapobj_free(struct heapobj *hobj, void *ptr)
{
	free_block(__mptr(hobj->pool_ref), ptr);
}

size_t heapobj_validate(struct heapobj *hobj, void *ptr)
{
	return __bt(check_block(__mptr(hobj->pool_ref), ptr));
}

size_t heapobj_inquire(struct heapobj *hobj)
{
	struct shared_heap *heap = __mptr(hobj->pool_ref);
	return heap->ubytes;
}

void *xnmalloc(size_t size)
{
	return alloc_block(&main_heap.heap, size);
}

void xnfree(void *ptr)
{
	free_block(&main_heap.heap, ptr);
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

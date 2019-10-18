/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <pthread.h>
#include "boilerplate/lock.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"

#define MALLOC_MAGIC 0xabbfcddc

struct pool_header {
	pthread_mutex_t lock;
	size_t used;
};

struct block_header {
	unsigned int magic;
	size_t size;
};

int __heapobj_init_private(struct heapobj *hobj, const char *name,
			   size_t size, void *mem)
{
	pthread_mutexattr_t mattr;
	struct pool_header *ph;
	int ret;

	/*
	 * There is no local pool when working with malloc, we just
	 * use the global process arena. This should not be an issue
	 * since this mode is aimed at debugging, particularly to be
	 * used along with Valgrind.
	 *
	 * However, we maintain a control header to track the amount
	 * of memory currently consumed in each heap.
	 */
	ph = malloc(sizeof(*ph));
	if (ph == NULL)
		return __bt(-ENOMEM);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&ph->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret) {
		free(ph);
		return ret;
	}

	ph->used = 0;

	hobj->pool = ph;
	hobj->size = size;
	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s", name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%p", hobj);

	return 0;
}

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems)
{
	if (size == 0 || elems <= 0)
		return __bt(-EINVAL);

	return __bt(__heapobj_init_private(hobj, name, size * elems, NULL));
}

void pvheapobj_destroy(struct heapobj *hobj)
{
	struct pool_header *ph = hobj->pool;

	__RT(pthread_mutex_destroy(&ph->lock));
	free(ph);
}

int pvheapobj_extend(struct heapobj *hobj, size_t size, void *mem)
{
	struct pool_header *ph = hobj->pool;

	write_lock_nocancel(&ph->lock);
	hobj->size += size;
	write_unlock(&ph->lock);

	return 0;
}

void *pvheapobj_alloc(struct heapobj *hobj, size_t size)
{
	struct pool_header *ph = hobj->pool;
	struct block_header *bh;
	void *ptr;

	write_lock(&ph->lock);

	ph->used += size;
	/* Enforce hard limit. */
	if (ph->used > hobj->size)
		goto fail;

	write_unlock(&ph->lock);

	/* malloc(3) is not a cancellation point. */
	ptr = malloc(size + sizeof(*bh));
	if (ptr == NULL) {
		write_lock(&ph->lock);
		goto fail;
	}

	bh = ptr;
	bh->magic = MALLOC_MAGIC;
	bh->size = size;

	return bh + 1;
fail:
	ph->used -= size;
	write_unlock(&ph->lock);

	return NULL;
}

void pvheapobj_free(struct heapobj *hobj, void *ptr)
{
	struct block_header *bh = ptr - sizeof(*bh);
	struct pool_header *ph = hobj->pool;

	assert(hobj->size >= bh->size);
	write_lock(&ph->lock);
	ph->used -= bh->size;
	write_unlock(&ph->lock);
	free(bh);
}

size_t pvheapobj_inquire(struct heapobj *hobj)
{
	struct pool_header *ph = hobj->pool;

	return ph->used;
}

size_t pvheapobj_validate(struct heapobj *hobj, void *ptr)
{
	struct block_header *bh;

	/* Catch trivially wrong cases: NULL or unaligned. */
	if (ptr == NULL)
		return 0;

	if ((unsigned long)ptr & (sizeof(unsigned long)-1))
		return 0;

	/*
	 * We will likely get hard validation here, i.e. crash or
	 * abort if the pointer is out of the address space. TLSF is a
	 * bit smarter, and pshared definitely does the right thing.
	 */

	bh = ptr - sizeof(*bh);
	if (bh->magic != MALLOC_MAGIC)
		return 0;

	return bh->size;
}

int heapobj_pkg_init_private(void)
{
	return 0;
}

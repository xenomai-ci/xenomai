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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <boilerplate/lock.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/heapobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/memPartLib.h>

#include "memPartLib.h"

#define mempart_magic	0x5a6b7c8d

static struct wind_mempart *find_mempart_from_id(PART_ID partId)
{
	struct wind_mempart *mp = mainheap_deref(partId, struct wind_mempart);

	if (mp == NULL || ((uintptr_t)mp & (sizeof(uintptr_t)-1)) != 0 ||
	    mp->magic != mempart_magic)
		return NULL;
	/*
	 * XXX: memory partitions may not be deleted, so we don't need
	 * to protect against references to stale objects.
	 */
	return mp;
}

PART_ID memPartCreate(char *pPool, unsigned int poolSize)
{
	pthread_mutexattr_t mattr;
	struct wind_mempart *mp;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	mp = xnmalloc(sizeof(*mp));
	if (mp == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		goto fail;
	}

	ret = __heapobj_init(&mp->hobj, NULL, poolSize, pPool);
	if (ret) {
		xnfree(mp);
		errno = S_memLib_INVALID_NBYTES;
	fail:
		CANCEL_RESTORE(svc);
		return (PART_ID)0;
	}

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	__RT(pthread_mutex_init(&mp->lock, &mattr));
	pthread_mutexattr_destroy(&mattr);
	memset(&mp->stats, 0, sizeof(mp->stats));
	mp->stats.numBytesFree = poolSize;
	mp->stats.numBlocksFree = 1;
	mp->magic = mempart_magic;

	CANCEL_RESTORE(svc);

	return mainheap_ref(mp, PART_ID);
}

STATUS memPartAddToPool(PART_ID partId,
			char *pPool, unsigned int poolSize)
{
	struct wind_mempart *mp;
	struct service svc;
	STATUS ret = OK;

	if (poolSize == 0) {
		errno = S_memLib_INVALID_NBYTES;
		return ERROR;
	}

	mp = find_mempart_from_id(partId);
	if (mp == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	CANCEL_DEFER(svc);

	__RT(pthread_mutex_lock(&mp->lock));

	if (heapobj_extend(&mp->hobj, poolSize, pPool)) {
		errno = S_memLib_INVALID_NBYTES;
		ret = ERROR;
	} else {
		mp->stats.numBytesFree += poolSize;
		mp->stats.numBlocksFree++;
	}

	__RT(pthread_mutex_unlock(&mp->lock));

	CANCEL_RESTORE(svc);

	return ret;
}

void *memPartAlignedAlloc(PART_ID partId,
			  unsigned int nBytes, unsigned int alignment)
{
	unsigned int xtra = 0;
	void *ptr;

	/*
	 * XXX: We assume that our underlying allocator (TLSF, pshared
	 * or Glibc's malloc()) aligns at worst on a 8-bytes boundary,
	 * so we only have to care for larger constraints.
	 */
	if ((alignment & (alignment - 1)) != 0) {
		warning("%s: alignment value '%u' is not a power of two",
			__FUNCTION__, alignment);
		alignment = 8;
	}
	else if (alignment > 8)
		xtra = alignment;

	ptr = memPartAlloc(partId, nBytes + xtra);
	if (ptr == NULL)
		return NULL;

	return (void *)(((uintptr_t)ptr + xtra) & ~(alignment - 1));
}

void *memPartAlloc(PART_ID partId, unsigned int nBytes)
{
	struct wind_mempart *mp;
	void *p;

	if (nBytes == 0)
		return NULL;

	mp = find_mempart_from_id(partId);
	if (mp == NULL)
		return NULL;

	__RT(pthread_mutex_lock(&mp->lock));

	p = heapobj_alloc(&mp->hobj, nBytes);
	if (p == NULL)
		goto out;

	mp->stats.numBytesAlloc += nBytes;
	mp->stats.numBlocksAlloc++;
	mp->stats.numBytesFree -= nBytes;
	mp->stats.numBlocksFree--;
	if (mp->stats.numBytesAlloc > mp->stats.maxBytesAlloc)
		mp->stats.maxBytesAlloc = mp->stats.numBytesAlloc;
out:
	__RT(pthread_mutex_unlock(&mp->lock));

	return p;
}

STATUS memPartFree(PART_ID partId, char *pBlock)
{
	struct wind_mempart *mp;
	struct service svc;
	size_t size;

	if (pBlock == NULL)
		return ERROR;

	mp = find_mempart_from_id(partId);
	if (mp == NULL)
		return ERROR;

	CANCEL_DEFER(svc);

	__RT(pthread_mutex_lock(&mp->lock));

	heapobj_free(&mp->hobj, pBlock);

	size = heapobj_validate(&mp->hobj, pBlock);
	mp->stats.numBytesAlloc -= size;
	mp->stats.numBlocksAlloc--;
	mp->stats.numBytesFree += size;
	mp->stats.numBlocksFree++;
	
	__RT(pthread_mutex_unlock(&mp->lock));

	CANCEL_RESTORE(svc);

	return OK;
}

void memAddToPool(char *pPool, unsigned int poolSize)
{
	/*
	 * XXX: Since Glibc's malloc() is at least as efficient as
	 * VxWork's first-fit allocator, we just route allocation
	 * requests on the main partition to the regular malloc() and
	 * free() routines. Given that, our main pool is virtually
	 * infinite already, so we just give a hint to the user about
	 * this when asked to extend it.
	 */
	warning("%s: extending the main partition is useless", __FUNCTION__);
}

STATUS memPartInfoGet(PART_ID partId, MEM_PART_STATS *ppartStats)
{
	struct wind_mempart *mp;
	struct service svc;

	mp = find_mempart_from_id(partId);
	if (mp == NULL)
		return ERROR;

	CANCEL_DEFER(svc);

	__RT(pthread_mutex_lock(&mp->lock));
	*ppartStats = mp->stats;
	__RT(pthread_mutex_unlock(&mp->lock));

	CANCEL_RESTORE(svc);

	return OK;
}

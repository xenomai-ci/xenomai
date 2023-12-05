/*
* Copyright (C) 2008 Niklaus Giger <niklaus.giger@member.fsf.org>.
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

#include <stdlib.h>
#include <boilerplate/lock.h>
#include <copperplate/heapobj.h>
#include <vxworks/errnoLib.h>

#include "rngLib.h"

#define ring_magic 0x5432affe

static struct wind_ring *find_ring_from_id(RING_ID rid)
{
	struct wind_ring *ring = mainheap_deref(rid, struct wind_ring);

	if (ring == NULL || ((uintptr_t)ring & (sizeof(uintptr_t)-1)) != 0 ||
	    ring->magic != ring_magic)
		return NULL;

	return ring;
}

RING_ID rngCreate(int nbytes)
{
	struct wind_ring *ring;
	struct service svc;
	void *ring_mem;
	RING_ID rid;

	if (nbytes <= 0) {
		errnoSet(S_memLib_NOT_ENOUGH_MEMORY);
		return 0;
	}

	CANCEL_DEFER(svc);

	ring_mem = xnmalloc(sizeof(*ring) + nbytes + 1);
	if (ring_mem == NULL) {
		rid = 0;
		errno = errnoSet(S_memLib_NOT_ENOUGH_MEMORY);
		goto out;
	}

	ring = ring_mem;
	ring->magic = ring_magic;
	ring->bufSize = nbytes;
	ring->readPos = 0;
	ring->writePos = 0;
	rid = mainheap_ref(ring, RING_ID);
out:
	CANCEL_RESTORE(svc);

	return rid;
}

void rngDelete(RING_ID rid)
{
	struct wind_ring *ring = find_ring_from_id(rid);
	struct service svc;

	if (ring) {
		ring->magic = 0;
		CANCEL_DEFER(svc);
		xnfree(ring);
		CANCEL_RESTORE(svc);
	}
}

void rngFlush(RING_ID rid)
{
	struct wind_ring *ring = find_ring_from_id(rid);

	if (ring) {
		ring->readPos = 0;
		ring->writePos = 0;
	}
}

int rngBufGet(RING_ID rid, char *buffer, int maxbytes)
{
	struct wind_ring *ring = find_ring_from_id(rid);
	unsigned int savedWritePos;
	int j, bytesRead = 0;

	if (ring == NULL)
		return ERROR;

	savedWritePos = ring->writePos;

	for (j = 0; j < maxbytes; j++) {
		if ((ring->readPos) % (ring->bufSize + 1) == savedWritePos)
			break;
		buffer[j] = ring->buffer[ring->readPos];
		++bytesRead;
		ring->readPos = (ring->readPos + 1) % (ring->bufSize + 1);
	}

	return bytesRead;
}

int rngBufPut(RING_ID rid, char *buffer, int nbytes)
{
	struct wind_ring *ring = find_ring_from_id(rid);
	unsigned int savedReadPos;
	int j, bytesWritten = 0;

	if (ring == NULL)
		return ERROR;

	savedReadPos = ring->readPos;

	for (j = 0; j < nbytes; j++) {
		if ((ring->writePos + 1) % (ring->bufSize + 1) == savedReadPos)
			break;
		ring->buffer[ring->writePos] = buffer[j];
		++bytesWritten;
		ring->writePos = (ring->writePos + 1) % (ring->bufSize + 1);
	}

	return bytesWritten;
}

BOOL rngIsEmpty(RING_ID rid)
{
	struct wind_ring *ring = find_ring_from_id(rid);

	if (ring == NULL)
		return ERROR;

	return rngFreeBytes(rid) == (int)ring->bufSize;
}

BOOL rngIsFull(RING_ID rid)
{
	struct wind_ring *ring = find_ring_from_id(rid);

	if (ring == NULL)
		return ERROR;

	return rngFreeBytes(rid) == 0;
}

int rngFreeBytes(RING_ID rid)
{
	struct wind_ring *ring = find_ring_from_id(rid);

	if (ring == NULL)
		return ERROR;

	return ((ring->bufSize -
		 (ring->writePos - ring->readPos)) % (ring->bufSize + 1));
}

int rngNBytes(RING_ID rid)
{
	struct wind_ring *ring = find_ring_from_id(rid);

	if (ring == NULL)
		return ERROR;

	return ring->bufSize - rngFreeBytes(rid);
}

void rngPutAhead(RING_ID rid, char byte, int offset)
{
	struct wind_ring *ring = find_ring_from_id(rid);
	int where;

	if (ring) {
		where = (ring->writePos + offset) % (ring->bufSize + 1);
		ring->buffer[where] = byte;
	}
}

void rngMoveAhead(RING_ID rid, int n)
{
	struct wind_ring *ring = find_ring_from_id(rid);

	if (ring) {
		ring->writePos += n;
		ring->writePos %= (ring->bufSize + 1);
	}
}

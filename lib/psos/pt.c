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
#include <boilerplate/ancillaries.h>
#include <boilerplate/lock.h>
#include <copperplate/cluster.h>
#include <psos/psos.h>

#include "internal.h"
#include "pt.h"

#define pt_magic	0x8181fefe

#define pt_align_mask   (sizeof(void *)-1)

#define pt_bitmap_pos(pt,n) \
pt->bitmap[((n) / (sizeof(u_long) * 8))]

#define pt_block_pos(n) \
(1L << ((n) % (sizeof(u_long) * 8)))

#define pt_bitmap_setbit(pt,n) \
(pt_bitmap_pos(pt,n) |= pt_block_pos(n))

#define pt_bitmap_clrbit(pt,n) \
(pt_bitmap_pos(pt,n) &= ~pt_block_pos(n))

#define pt_bitmap_tstbit(pt,n) \
(pt_bitmap_pos(pt,n) & pt_block_pos(n))

struct pvcluster psos_pt_table;

static unsigned long anon_ptids;

/*
 * XXX: Status wrt caller cancellation: all these routines are not
 * supposed to traverse any cancellation point (*), so we don't bother
 * adding any cleanup handler to release the partition lock, given
 * that copperplate-protected sections disables asynchronous thread
 * cancellation temporarily and therefore will allow callees to grab
 * mutexes safely.
 *
 * (*) all private cluster management routines are expected to be free
 * from cancellation points. You have been warned.
 */

static struct psos_pt *get_pt_from_id(u_long ptid, int *err_r)
{
	struct psos_pt *pt = (struct psos_pt *)ptid;

	/*
	 * Unlike most other pSOS objects (except timers), the
	 * partition control block is NOT laid into the main heap, so
	 * don't have to apply mainheap_deref() to convert partition
	 * handles to pointers, but we do a plain cast instead.  (This
	 * said, mainheap_deref() is smart enough to deal with private
	 * pointers, but we just avoid useless overhead).
	 */
	if (pt == NULL || ((uintptr_t)pt & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (pt->magic == pt_magic) {
		if (__RT(pthread_mutex_lock(&pt->lock)) == 0) {
			if (pt->magic == pt_magic)
				return pt;
			__RT(pthread_mutex_unlock(&pt->lock));
			/* Will likely fall down to ERR_OBJDEL. */
		}
	}

	if (pt->magic == ~pt_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((pt->magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static inline void put_pt(struct psos_pt *pt)
{
	__RT(pthread_mutex_unlock(&pt->lock));
}

static inline size_t pt_overhead(size_t psize, size_t bsize)
{
	size_t m = (bsize * 8);
	size_t q = ((psize - sizeof(struct psos_pt)) * m) / (m + 1);
	return (psize - q + pt_align_mask) & ~pt_align_mask;
}

u_long pt_create(const char *name,
		 void *paddr, void *laddr  __attribute__ ((unused)),
		 u_long psize, u_long bsize, u_long flags,
		 u_long *ptid_r, u_long *nbuf)
{
	pthread_mutexattr_t mattr;
	char short_name[5];
	struct service svc;
	struct psos_pt *pt;
	int ret = SUCCESS;
	u_long overhead;
	caddr_t mp;
	u_long n;

	if ((uintptr_t)paddr & (sizeof(uintptr_t) - 1))
		return ERR_PTADDR;

	if (bsize <= pt_align_mask)
		return ERR_BUFSIZE;

	if (bsize & (bsize - 1))
		return ERR_BUFSIZE;	/* Not a power of two. */

	if (psize < sizeof(*pt))
		return ERR_TINYPT;

	pt = paddr;

	if (name == NULL || *name == '\0')
		sprintf(pt->name, "pt%lu", ++anon_ptids);
	else {
		name = psos_trunc_name(short_name, name);
		namecpy(pt->name, name);
	}

	CANCEL_DEFER(svc);

	if (pvcluster_addobj_dup(&psos_pt_table, pt->name, &pt->cobj)) {
		warning("cannot register partition: %s", pt->name);
		ret = ERR_OBJID;
		goto out;
	}

	pt->flags = flags;
	pt->bsize = (bsize + pt_align_mask) & ~pt_align_mask;
	overhead = pt_overhead(psize, pt->bsize);

	pt->nblks = (psize - overhead) / pt->bsize;
	if (pt->nblks == 0) {
		ret = ERR_TINYPT;
		goto out;
	}

	pt->psize = pt->nblks * pt->bsize;
	pt->data = (caddr_t)pt + overhead;
	pt->freelist = mp = pt->data;
	pt->ublks = 0;

	for (n = pt->nblks; n > 1; n--) {
		caddr_t nmp = mp + pt->bsize;
		*((void **)mp) = nmp;
		mp = nmp;
	}

	*((void **)mp) = NULL;
	memset(pt->bitmap, 0, overhead - sizeof(*pt) + sizeof(pt->bitmap));
	*nbuf = pt->nblks;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	__RT(pthread_mutex_init(&pt->lock, &mattr));
	pthread_mutexattr_destroy(&mattr);

	pt->magic = pt_magic;
	*ptid_r = (u_long)pt;
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long pt_delete(u_long ptid)
{
	struct psos_pt *pt;
	struct service svc;
	int ret;

	pt = get_pt_from_id(ptid, &ret);
	if (pt == NULL)
		return ret;

	if ((pt->flags & PT_DEL) == 0 && pt->ublks > 0) {
		put_pt(pt);
		return ERR_BUFINUSE;
	}

	CANCEL_DEFER(svc);
	pvcluster_delobj(&psos_pt_table, &pt->cobj);
	CANCEL_RESTORE(svc);
	pt->magic = ~pt_magic; /* Prevent further reference. */
	put_pt(pt);
	__RT(pthread_mutex_destroy(&pt->lock));

	return SUCCESS;
}

u_long pt_getbuf(u_long ptid, void **bufaddr)
{
	struct psos_pt *pt;
	u_long numblk;
	void *buf;
	int ret;

	pt = get_pt_from_id(ptid, &ret);
	if (pt == NULL)
		return ret;

	buf = pt->freelist;
	if (buf) {
		pt->freelist = *((void **)buf);
		pt->ublks++;
		numblk = ((caddr_t)buf - pt->data) / pt->bsize;
		pt_bitmap_setbit(pt, numblk);
	}

	put_pt(pt);

	*bufaddr = buf;
	if (buf == NULL)
		return ERR_NOBUF;

	return SUCCESS;
}

u_long pt_retbuf(u_long ptid, void *buf)
{
	struct psos_pt *pt;
	u_long numblk;
	int ret;

	pt = get_pt_from_id(ptid, &ret);
	if (pt == NULL)
		return ret;

	if ((caddr_t)buf < pt->data ||
	    (caddr_t)buf >= pt->data + pt->psize ||
	    (((caddr_t)buf - pt->data) % pt->bsize) != 0) {
		ret = ERR_BUFADDR;
		goto done;
	}

	numblk = ((caddr_t)buf - pt->data) / pt->bsize;

	if (!pt_bitmap_tstbit(pt, numblk)) {
		ret = ERR_BUFFREE;
		goto done;
	}

	pt_bitmap_clrbit(pt, numblk);
	*((void **)buf) = pt->freelist;
	pt->freelist = buf;
	pt->ublks--;
	ret = SUCCESS;
done:
	put_pt(pt);

	return ret;
}

u_long pt_ident(const char *name, u_long node, u_long *ptid_r)
{
	struct pvclusterobj *cobj;
	struct service svc;
	struct psos_pt *pt;
	char short_name[5];

	if (node)
		return ERR_NODENO;

	name = psos_trunc_name(short_name, name);

	CANCEL_DEFER(svc);
	cobj = pvcluster_findobj(&psos_pt_table, name);
	CANCEL_RESTORE(svc);
	if (cobj == NULL)
		return ERR_OBJNF;

	pt = container_of(cobj, struct psos_pt, cobj);
	*ptid_r = (u_long)pt;

	return SUCCESS;
}

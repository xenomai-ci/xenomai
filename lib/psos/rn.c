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
#include <boilerplate/ancillaries.h>
#include <copperplate/threadobj.h>
#include <psos/psos.h>

#include "internal.h"
#include "tm.h"
#include "rn.h"

#define rn_magic	0x8181efef

struct pvcluster psos_rn_table;

static unsigned long anon_rnids;

static struct psos_rn *get_rn_from_id(u_long rnid, int *err_r)
{
	struct psos_rn *rn = mainheap_deref(rnid, struct psos_rn);

	if (rn == NULL || ((uintptr_t)rn & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (rn->magic == rn_magic)
		return rn;

	if (rn->magic == ~rn_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((rn->magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

u_long rn_create(const char *name, void *saddr, u_long length,
		 u_long usize, u_long flags, u_long *rnid_r,
		 u_long *asize_r)
{
	int sobj_flags = 0, ret = SUCCESS;
	struct psos_rn *rn;
	struct service svc;
	char short_name[5];

	if ((uintptr_t)saddr & (sizeof(uintptr_t) - 1))
		return ERR_RNADDR;

	if (usize < 16)
		return ERR_TINYUNIT;

	if ((usize & (usize - 1)) != 0)
		return ERR_UNITSIZE;	/* Not a power of two. */

	if (length <= sizeof(*rn))
		return ERR_TINYRN;

	if (flags & RN_PRIOR)
		sobj_flags = SYNCOBJ_PRIO;

	/*
	 * XXX: We may not put the region control block directly into
	 * the user-provided area, because shared mode requires us to
	 * pull shareable object memory from the main heap. Albeit the
	 * region per se is not shareable between processes, the
	 * syncobj it embeds for synchronization is implicitely
	 * shareable by design (there is no pvsyncobj, which would be
	 * a very seldom use). So we allocate space for the control
	 * block from the main pool instead.
	 */
	rn = xnmalloc(sizeof(*rn));
	if (rn == NULL)
		/*
		 * mmmfff... When error codes are plain silly and we
		 * don't have generic failure codes but braindamage
		 * per-feature errnos to extend the interface, we can
		 * only try to pick the least idiotic value.
		 */
		return ERR_NOSEG;

	/* Skip the unused space. */
	saddr += sizeof(*rn);
	length -= sizeof(*rn);

	CANCEL_DEFER(svc);

	if (name == NULL || *name == '\0')
		sprintf(rn->name, "rn%lu", ++anon_rnids);
	else {
		name = psos_trunc_name(short_name, name);
		namecpy(rn->name, name);
	}

	if (pvcluster_addobj_dup(&psos_rn_table, rn->name, &rn->cobj)) {
		warning("cannot register region: %s", rn->name);
		xnfree(rn);
		ret = ERR_OBJID;
		goto out;
	}

	ret = __heapobj_init(&rn->hobj, name, length, saddr);
	if (ret) {
		pvcluster_delobj(&psos_rn_table, &rn->cobj);
		ret = ERR_TINYRN;
		xnfree(rn);
		goto out;
	}

	if (flags & RN_PRIOR)
		sobj_flags = SYNCOBJ_PRIO;

	rn->length = length;
	rn->usize = usize;	/* Not actually used, just checked. */
	rn->flags = flags;
	rn->busynr = 0;
	rn->usedmem = 0;
	ret = syncobj_init(&rn->sobj, CLOCK_COPPERPLATE, sobj_flags, fnref_null);
	if (ret) {
		heapobj_destroy(&rn->hobj);
		pvcluster_delobj(&psos_rn_table, &rn->cobj);
		xnfree(rn);
		goto out;
	}

	rn->magic = rn_magic;
	*asize_r = rn->hobj.size;
	*rnid_r = mainheap_ref(rn, u_long);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long rn_delete(u_long rnid)
{
	struct syncstate syns;
	struct psos_rn *rn;
	struct service svc;
	int ret;

	rn = get_rn_from_id(rnid, &ret);
	if (rn == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&rn->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if ((rn->flags & RN_DEL) == 0 && rn->busynr > 0) {
		syncobj_unlock(&rn->sobj, &syns);
		ret = ERR_SEGINUSE;
		goto out;
	}

	pvcluster_delobj(&psos_rn_table, &rn->cobj);
	rn->magic = ~rn_magic; /* Prevent further reference. */
	ret = syncobj_destroy(&rn->sobj, &syns);
	if (ret)
		ret = ERR_TATRNDEL;
	xnfree(rn);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long rn_ident(const char *name, u_long *rnid_r)
{
	struct pvclusterobj *cobj;
	struct psos_rn *rn;
	struct service svc;
	char short_name[5];

	name = psos_trunc_name(short_name, name);

	CANCEL_DEFER(svc);
	cobj = pvcluster_findobj(&psos_rn_table, name);
	CANCEL_RESTORE(svc);
	if (cobj == NULL)
		return ERR_OBJNF;

	rn = container_of(cobj, struct psos_rn, cobj);
	*rnid_r = mainheap_ref(rn, u_long);

	return SUCCESS;
}

u_long rn_getseg(u_long rnid, u_long size, u_long flags,
		 u_long timeout, void **segaddr)
{
	struct psos_rn_wait *wait = NULL;
	struct timespec ts, *timespec;
	struct syncstate syns;
	struct psos_rn *rn;
	struct service svc;
	int ret = SUCCESS;
	void *seg;

	rn = get_rn_from_id(rnid, &ret);
	if (rn == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&rn->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	/*
	 * The heap manager does not enforce any allocation limit; so
	 * we have to do it by ourselves.
	 */
	if (rn->usedmem + size > rn->length)
		goto starve;

	seg = heapobj_alloc(&rn->hobj, size);
	if (seg) {
		*segaddr = seg;
		rn->busynr++;
		rn->usedmem += heapobj_validate(&rn->hobj, seg);
		goto done;
	}

starve:
	if (flags & RN_NOWAIT) {
		ret = ERR_NOSEG;
		goto done;
	}

	if (timeout != 0) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&psos_clock, timeout, timespec);
	} else
		timespec = NULL;

	wait = threadobj_prepare_wait(struct psos_rn_wait);
	wait->ptr = __moff_nullable(NULL);
	wait->size = size;

	ret = syncobj_wait_grant(&rn->sobj, timespec, &syns);
	if (ret == -ETIMEDOUT)
		ret = ERR_TIMEOUT;
	/*
	 * There is no explicit flush operation on pSOS regions,
	 * only an implicit one through deletion.
	 */
	else if (ret == -EIDRM) {
		ret = ERR_RNKILLD;
		goto out;
	}

	*segaddr = __mptr(wait->ptr);
done:
	syncobj_unlock(&rn->sobj, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	CANCEL_RESTORE(svc);

	return ret;
}

u_long rn_retseg(u_long rnid, void *segaddr)
{
	struct threadobj *thobj, *tmp;
	struct psos_rn_wait *wait;
	struct syncstate syns;
	struct psos_rn *rn;
	struct service svc;
	int ret = SUCCESS;
	u_long size;
	void *seg;

	rn = get_rn_from_id(rnid, &ret);
	if (rn == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&rn->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	rn->usedmem -= heapobj_validate(&rn->hobj, segaddr);
	heapobj_free(&rn->hobj, segaddr);
	rn->busynr--;

	if (!syncobj_grant_wait_p(&rn->sobj))
		goto done;

	syncobj_for_each_grant_waiter_safe(&rn->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		size = wait->size;
		if (rn->usedmem + size > rn->length)
			continue;
		seg = heapobj_alloc(&rn->hobj, size);
		if (seg) {
			rn->busynr++;
			rn->usedmem += heapobj_validate(&rn->hobj, seg);
			wait->ptr = __moff(seg);
			syncobj_grant_to(&rn->sobj, thobj);
		}
	}
done:
	syncobj_unlock(&rn->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

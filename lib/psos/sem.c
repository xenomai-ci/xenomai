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
#include <string.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/heapobj.h>
#include <copperplate/cluster.h>
#include <copperplate/clockobj.h>
#include <copperplate/semobj.h>

#include "reference.h"
#include "sem.h"
#include "tm.h"
#include "internal.h"
#include <psos/psos.h>

#define sem_magic	0x8181fbfb

struct cluster psos_sem_table;

static unsigned long anon_smids;

static struct psos_sem *get_sem_from_id(u_long smid, int *err_r)
{
	struct psos_sem *sem = mainheap_deref(smid, struct psos_sem);

	if (sem == NULL || ((uintptr_t)sem & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (sem->magic == sem_magic)
		return sem;

	if (sem->magic == ~sem_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((sem->magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static void sem_finalize(struct semobj *smobj)
{
	struct psos_sem *sem = container_of(smobj, struct psos_sem, smobj);
	xnfree(sem);
}
fnref_register(libpsos, sem_finalize);

u_long sm_create(const char *name,
		 u_long count, u_long flags, u_long *smid_r)
{
	int smobj_flags = SEMOBJ_WARNDEL;
	struct psos_sem *sem;
	struct service svc;
	char short_name[5];
	int ret;

	CANCEL_DEFER(svc);

	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL) {
		ret = ERR_NOSCB;
		goto out;
	}

	if (name == NULL || *name == '\0')
		sprintf(sem->name, "sm%lu", ++anon_smids);
	else {
		name = psos_trunc_name(short_name, name);
		namecpy(sem->name, name);
	}

	if (cluster_addobj_dup(&psos_sem_table, sem->name, &sem->cobj)) {
		warning("cannot register semaphore: %s", sem->name);
		xnfree(sem);
		ret = ERR_OBJID;
		goto out;
	}

	if (flags & SM_PRIOR)
		smobj_flags |= SEMOBJ_PRIO;

	sem->magic = sem_magic;
	ret = semobj_init(&sem->smobj, smobj_flags, count,
			  fnref_put(libpsos, sem_finalize));
	if (ret) {
		cluster_delobj(&psos_sem_table, &sem->cobj);
		xnfree(sem);
		goto out;
	}

	*smid_r = mainheap_ref(sem, u_long);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long sm_delete(u_long smid)
{
	struct psos_sem *sem;
	struct service svc;
	int ret;

	sem = get_sem_from_id(smid, &ret);
	if (sem == NULL)
		return ret;

	CANCEL_DEFER(svc);

	cluster_delobj(&psos_sem_table, &sem->cobj);
	sem->magic = ~sem_magic; /* Prevent further reference. */
	ret = semobj_destroy(&sem->smobj);
	if (ret)
		ret = ret > 0 ? ERR_TATSDEL : ERR_OBJDEL;

	CANCEL_RESTORE(svc);

	return ret;
}

u_long sm_ident(const char *name, u_long node, u_long *smid_r)
{
	struct clusterobj *cobj;
	struct psos_sem *sem;
	struct service svc;
	char short_name[5];

	if (node)
		return ERR_NODENO;

	name = psos_trunc_name(short_name, name);

	CANCEL_DEFER(svc);
	cobj = cluster_findobj(&psos_sem_table, name);
	CANCEL_RESTORE(svc);
	if (cobj == NULL)
		return ERR_OBJNF;

	sem = container_of(cobj, struct psos_sem, cobj);
	*smid_r = mainheap_ref(sem, u_long);

	return SUCCESS;
}

u_long sm_p(u_long smid, u_long flags, u_long timeout)
{
	struct timespec ts, *timespec = &ts;
	struct psos_sem *sem;
	struct service svc;
	int ret;

	sem = get_sem_from_id(smid, &ret);
	if (sem == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (flags & SM_NOWAIT) {
		timespec->tv_sec = 0;
		timespec->tv_nsec = 0;
	} else if (timeout != 0)
		clockobj_ticks_to_timeout(&psos_clock, timeout, timespec);
	else
		timespec = NULL;

	ret = semobj_wait(&sem->smobj, timespec);
	if (ret) {
		if (ret == -EIDRM)
			ret = ERR_SKILLD;
		else if (ret == -ETIMEDOUT)
			ret = ERR_TIMEOUT;
		else if (ret == -EWOULDBLOCK)
			ret = ERR_NOSEM;
		/*
		 * There is no explicit flush operation on pSOS
		 * semaphores, only an implicit one through deletion.
		 */
	}

	CANCEL_RESTORE(svc);

	return ret;
}

u_long sm_v(u_long smid)
{
	struct psos_sem *sem;
	struct service svc;
	int ret;

	sem = get_sem_from_id(smid, &ret);
	if (sem == NULL)
		return ret;

	CANCEL_DEFER(svc);

	ret = semobj_post(&sem->smobj);
	if (ret == -EIDRM)
		ret = ERR_OBJDEL;

	CANCEL_RESTORE(svc);

	return ret;
}

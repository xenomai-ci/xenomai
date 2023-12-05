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
#include <copperplate/cluster.h>
#include <psos/psos.h>

#include "internal.h"
#include "reference.h"
#include "queue.h"
#include "tm.h"

#define queue_magic	0x8181fdfd

struct cluster psos_queue_table;

static unsigned long anon_qids;

struct msgholder {
	int size;
	struct holder link;
	/* Payload data follows. */
};

static struct psos_queue *get_queue_from_id(u_long qid, int *err_r)
{
	struct psos_queue *q = mainheap_deref(qid, struct psos_queue);

	if (q == NULL || ((uintptr_t)q & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (q->magic == queue_magic)
		return q;

	if (q->magic == ~queue_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((q->magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static void queue_finalize(struct syncobj *sobj)
{
	struct psos_queue *q = container_of(sobj, struct psos_queue, sobj);
	xnfree(q);
}
fnref_register(libpsos, queue_finalize);

static u_long __q_create(const char *name, u_long count,
			 u_long flags, u_long maxlen, u_long *qid_r)
{
	struct psos_queue *q;
	struct service svc;
	int sobj_flags = 0;
	int ret = SUCCESS;
	char short_name[5];

	CANCEL_DEFER(svc);

	q = xnmalloc(sizeof(*q));
	if (q == NULL) {
		ret = ERR_NOQCB;
		goto out;
	}

	if (name == NULL || *name == '\0')
		sprintf(q->name, "q%lu", ++anon_qids);
	else {
		name = psos_trunc_name(short_name, name);
		namecpy(q->name, name);
	}

	if (flags & Q_PRIOR)
		sobj_flags = SYNCOBJ_PRIO;

	q->flags = flags;
	q->maxmsg = (flags & Q_LIMIT) ? count : 0;
	q->maxlen = maxlen;
	ret = syncobj_init(&q->sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libpsos, queue_finalize));
	if (ret) {
		ret = ERR_NOQCB;
		goto fail_syncinit;
	}

	list_init(&q->msg_list);
	q->msgcount = 0;
	q->magic = queue_magic;
	*qid_r = mainheap_ref(q, u_long);

	if (cluster_addobj_dup(&psos_queue_table, q->name, &q->cobj)) {
		warning("cannot register queue: %s", q->name);
		ret = ERR_OBJID;
		goto fail_register;
	}

	CANCEL_RESTORE(svc);

	return 0;

fail_register:
	syncobj_uninit(&q->sobj);
fail_syncinit:
	xnfree(q);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long q_create(const char *name,
		u_long count, u_long flags, u_long *qid_r)
{
	return __q_create(name, count, flags & ~Q_VARIABLE, sizeof(u_long[4]), qid_r);
}

u_long q_vcreate(const char *name, u_long flags,
		 u_long count, u_long maxlen, u_long *qid_r)
{
	return __q_create(name, count, flags | Q_VARIABLE, maxlen, qid_r);
}

static u_long __q_delete(u_long qid, u_long flags)
{
	struct syncstate syns;
	struct msgholder *msg;
	struct psos_queue *q;
	struct service svc;
	int ret, emptyq;

	q = get_queue_from_id(qid, &ret);
	if (q == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&q->sobj, &syns))
		return ERR_OBJDEL;

	if (((flags ^ q->flags) & Q_VARIABLE)) {
		syncobj_unlock(&q->sobj, &syns);
		CANCEL_RESTORE(svc);
		return (flags & Q_VARIABLE) ? ERR_NOTVARQ: ERR_VARQ;

	}

	emptyq = list_empty(&q->msg_list);
	if (!emptyq) {
		do {
			msg = list_pop_entry(&q->msg_list,
					     struct msgholder, link);
			xnfree(msg);
		} while (!list_empty(&q->msg_list));
	}

	cluster_delobj(&psos_queue_table, &q->cobj);
	q->magic = ~queue_magic; /* Prevent further reference. */
	ret = syncobj_destroy(&q->sobj, &syns);
	CANCEL_RESTORE(svc);
	if (ret)
		return ERR_TATQDEL;

	return emptyq ? SUCCESS : ERR_MATQDEL;
}

u_long q_delete(u_long qid)
{
	return __q_delete(qid, 0);
}

u_long q_vdelete(u_long qid)
{
	return __q_delete(qid, Q_VARIABLE);
}

static u_long __q_ident(const char *name,
			u_long flags, u_long node, u_long *qid_r)
{
	struct clusterobj *cobj;
	struct psos_queue *q;
	struct service svc;
	char short_name[5];

	if (node)
		return ERR_NODENO;

	name = psos_trunc_name(short_name, name);

	CANCEL_DEFER(svc);
	cobj = cluster_findobj(&psos_queue_table, name);
	CANCEL_RESTORE(svc);
	if (cobj == NULL)
		return ERR_OBJNF;

	q = container_of(cobj, struct psos_queue, cobj);
	if (((flags ^ q->flags) & Q_VARIABLE)) /* XXX: unsafe, but well... */
		return (flags & Q_VARIABLE) ? ERR_NOTVARQ: ERR_VARQ;

	*qid_r = mainheap_ref(q, u_long);

	return SUCCESS;
}

u_long q_ident(const char *name, u_long node, u_long *qid_r)
{
	return __q_ident(name, 0, node, qid_r);
}

u_long q_vident(const char *name, u_long node, u_long *qid_r)
{
	return __q_ident(name, Q_VARIABLE, node, qid_r);
}

static u_long __q_send_inner(struct psos_queue *q, unsigned long flags,
			     u_long *buffer, u_long bytes)
{
	struct psos_queue_wait *wait;
	struct threadobj *thobj;
	struct msgholder *msg;
	u_long maxbytes;

	thobj = syncobj_peek_grant(&q->sobj);
	if (thobj && threadobj_local_p(thobj)) {
		/* Fast path: direct copy to the receiver's buffer. */
		wait = threadobj_get_wait(thobj);
		maxbytes = wait->size;
		if (bytes > maxbytes)
			bytes = maxbytes;
		if (bytes > 0)
			memcpy(__mptr(wait->ptr), buffer, bytes);
		wait->size = bytes;
		goto done;
	}

	if ((q->flags & Q_LIMIT) && q->msgcount >= q->maxmsg)
		return ERR_QFULL;

	msg = xnmalloc(bytes + sizeof(*msg));
	if (msg == NULL)
		return ERR_NOMGB;

	q->msgcount++;
	msg->size = bytes;
	holder_init(&msg->link);

	if (bytes > 0)
		memcpy(msg + 1, buffer, bytes);

	if (flags & Q_JAMMED)
		list_prepend(&msg->link, &q->msg_list);
	else
		list_append(&msg->link, &q->msg_list);

	if (thobj) {
		/*
		 * We could not copy the message directly to the
		 * remote buffer, tell the thread to pull it from the
		 * pool.
		 */
		wait = threadobj_get_wait(thobj);
		wait->size = -1UL;
	}
done:
	if (thobj)
		syncobj_grant_to(&q->sobj, thobj);

	return SUCCESS;
}

static u_long __q_send(u_long qid, u_long flags, u_long *buffer, u_long bytes)
{
	struct syncstate syns;
	struct psos_queue *q;
	struct service svc;
	int ret;

	q = get_queue_from_id(qid, &ret);
	if (q == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&q->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if (((flags ^ q->flags) & Q_VARIABLE)) {
		ret = (flags & Q_VARIABLE) ? ERR_NOTVARQ: ERR_VARQ;
		goto fail;
	}

	if (bytes > q->maxlen) {
		ret = ERR_MSGSIZ;
		goto fail;
	}

	ret = __q_send_inner(q, flags, buffer, bytes);
fail:
	syncobj_unlock(&q->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long q_send(u_long qid, u_long msgbuf[4])
{
	return __q_send(qid, 0, msgbuf, sizeof(u_long[4]));
}

u_long q_vsend(u_long qid, void *msgbuf, u_long msglen)
{
	return __q_send(qid, Q_VARIABLE, msgbuf, msglen);
}

u_long q_urgent(u_long qid, u_long msgbuf[4])
{
	return __q_send(qid, Q_JAMMED, msgbuf, sizeof(u_long[4]));
}

u_long q_vurgent(u_long qid, void *msgbuf, u_long msglen)
{
	return __q_send(qid, Q_VARIABLE | Q_JAMMED, msgbuf, msglen);
}

static u_long __q_broadcast(u_long qid, u_long flags,
			    u_long *buffer, u_long bytes, u_long *count_r)
{
	struct syncstate syns;
	struct psos_queue *q;
	struct service svc;
	int ret = SUCCESS;

	q = get_queue_from_id(qid, &ret);
	if (q == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&q->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if (((flags ^ q->flags) & Q_VARIABLE)) {
		ret = (flags & Q_VARIABLE) ? ERR_NOTVARQ: ERR_VARQ;
		goto fail;
	}

	if (bytes > q->maxlen) {
		ret = ERR_MSGSIZ;
		goto fail;
	}

	/* Release all pending tasks atomically. */
	*count_r = 0;
	while (syncobj_grant_wait_p(&q->sobj)) {
		ret = __q_send_inner(q, flags, buffer, bytes);
		if (ret)
			break;
		(*count_r)++;
	}
fail:
	syncobj_unlock(&q->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long q_broadcast(u_long qid, u_long msgbuf[4], u_long *count_r)
{
	return __q_broadcast(qid, 0, msgbuf, sizeof(u_long[4]), count_r);
}

u_long q_vbroadcast(u_long qid, void *msgbuf, u_long msglen, u_long *count_r)
{
	return __q_broadcast(qid, Q_VARIABLE, msgbuf, msglen, count_r);
}

static u_long __q_receive(u_long qid, u_long flags, u_long timeout,
			  void *buffer, u_long msglen, u_long *msglen_r)
{
	struct psos_queue_wait *wait = NULL;
	struct timespec ts, *timespec;
	struct msgholder *msg = NULL;
	struct syncstate syns;
	unsigned long nbytes;
	struct psos_queue *q;
	struct service svc;
	int ret = SUCCESS;

	q = get_queue_from_id(qid, &ret);
	if (q == NULL)
		return ret;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&q->sobj, &syns)) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if (((flags ^ q->flags) & Q_VARIABLE)) {
		ret = (flags & Q_VARIABLE) ? ERR_NOTVARQ: ERR_VARQ;
		goto fail;
	}
retry:
	if (!list_empty(&q->msg_list)) {
		q->msgcount--;
		msg = list_pop_entry(&q->msg_list, struct msgholder, link);
		nbytes = msg->size;
		if (nbytes > msglen)
			nbytes = msglen;
		if (nbytes > 0)
			memcpy(buffer, msg + 1, nbytes);
		xnfree(msg);
		goto done;
	}

	if (flags & Q_NOWAIT) {
		ret = ERR_NOMSG;
		goto fail;
	}

	if (timeout != 0) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&psos_clock, timeout, timespec);
	} else
		timespec = NULL;

	wait = threadobj_prepare_wait(struct psos_queue_wait);
	wait->ptr = __moff(buffer);
	wait->size = msglen;

	ret = syncobj_wait_grant(&q->sobj, timespec, &syns);
	if (ret == -EIDRM) {
		ret = ERR_QKILLD;
		goto out;
	}

	if (ret == -ETIMEDOUT) {
		ret = ERR_TIMEOUT;
		goto fail;
	}
	nbytes = wait->size;
	if (nbytes == -1UL)	/* No direct copy? */
		goto retry;
done:
	if (msglen_r)
		*msglen_r = nbytes;
fail:
	syncobj_unlock(&q->sobj, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	CANCEL_RESTORE(svc);

	return ret;
}

u_long q_receive(u_long qid, u_long flags, u_long timeout, u_long msgbuf[4])
{
	return __q_receive(qid, flags & ~Q_VARIABLE,
			   timeout, msgbuf, sizeof(u_long[4]), NULL);
}

u_long q_vreceive(u_long qid, u_long flags, u_long timeout,
		  void *msgbuf, u_long msglen, u_long *msglen_r)
{
	return __q_receive(qid, flags | Q_VARIABLE,
			   timeout, msgbuf, msglen, msglen_r);
}

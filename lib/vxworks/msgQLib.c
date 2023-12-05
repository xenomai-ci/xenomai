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
#include <assert.h>
#include <memory.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include <vxworks/errnoLib.h>

#include "reference.h"
#include "taskLib.h"
#include "msgQLib.h"
#include "tickLib.h"

/*
 * XXX: In order to keep the following services callable from
 * non-VxWorks tasks (but still Xenomai ones, though), make sure
 * to never depend on the wind_task struct, but rather on the thread
 * base object directly.
 */

#define mq_magic	0x4a5b6c7d

struct msgholder {
	int size;
	struct holder link;
	/* Payload data follows. */
};

static struct wind_mq *find_mq_from_id(MSG_Q_ID qid)
{
	struct wind_mq *mq = mainheap_deref(qid, struct wind_mq);

	if (mq == NULL || ((uintptr_t)mq & (sizeof(uintptr_t)-1)) != 0 ||
	    mq->magic != mq_magic)
		return NULL;

	return mq;
}

static void mq_finalize(struct syncobj *sobj)
{
	struct wind_mq *mq = container_of(sobj, struct wind_mq, sobj);
	heapobj_destroy(&mq->pool);
	xnfree(mq);
}
fnref_register(libvxworks, mq_finalize);

MSG_Q_ID msgQCreate(int maxMsgs, int maxMsgLength, int options)
{
	int sobj_flags = 0, ret;
	struct wind_mq *mq;
	struct service svc;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return (MSG_Q_ID)0;
	}

	if ((options & ~MSG_Q_PRIORITY) || maxMsgs <= 0) {
		errno = S_msgQLib_INVALID_QUEUE_TYPE;
		return (MSG_Q_ID)0;
	}

	if (maxMsgLength < 0) {
		errno = S_msgQLib_INVALID_MSG_LENGTH;
		return (MSG_Q_ID)0;
	}

	CANCEL_DEFER(svc);

	mq = xnmalloc(sizeof(*mq));
	if (mq == NULL)
		goto fail_cballoc;

	/*
	 * The message pool must come from the main heap because of
	 * mq->msg_list (this queue head and messages from the pool
	 * must share the same allocation base). Create the heap
	 * object accordingly.
	 */
	if (heapobj_init_array(&mq->pool, NULL, maxMsgLength +
			       sizeof(struct msgholder), maxMsgs))
		goto fail_bufalloc;

	if (options & MSG_Q_PRIORITY)
		sobj_flags = SYNCOBJ_PRIO;

	ret = syncobj_init(&mq->sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libvxworks, mq_finalize));
	if (ret)
		goto fail_syncinit;
		
	mq->options = options;
	mq->maxmsg = maxMsgs;
	mq->msgsize = maxMsgLength;
	mq->msgcount = 0;
	list_init(&mq->msg_list);

	mq->magic = mq_magic;

	CANCEL_RESTORE(svc);

	return mainheap_ref(mq, MSG_Q_ID);

fail_syncinit:
	heapobj_destroy(&mq->pool);
fail_bufalloc:
	xnfree(mq);
fail_cballoc:
	errno = S_memLib_NOT_ENOUGH_MEMORY;

	CANCEL_RESTORE(svc);

	return (MSG_Q_ID)0;
}

STATUS msgQDelete(MSG_Q_ID msgQId)
{
	struct syncstate syns;
	struct wind_mq *mq;
	struct service svc;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	mq = find_mq_from_id(msgQId);
	if (mq == NULL)
		goto objid_error;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&mq->sobj, &syns)) {
		CANCEL_RESTORE(svc);
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	mq->magic = ~mq_magic; /* Prevent further reference. */
	syncobj_destroy(&mq->sobj, &syns);

	CANCEL_RESTORE(svc);

	return OK;
}

int msgQReceive(MSG_Q_ID msgQId, char *buffer, UINT maxNBytes, int timeout)
{
	struct wind_queue_wait *wait = NULL;
	struct timespec ts, *timespec;
	struct msgholder *msg = NULL;
	UINT nbytes = (UINT)ERROR;
	struct syncstate syns;
	struct wind_mq *mq;
	struct service svc;
	int ret;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	mq = find_mq_from_id(msgQId);
	if (mq == NULL)
		goto objid_error;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&mq->sobj, &syns)) {
		CANCEL_RESTORE(svc);
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

retry:
	if (!list_empty(&mq->msg_list)) {
		mq->msgcount--;
		msg = list_pop_entry(&mq->msg_list, struct msgholder, link);
		nbytes = msg->size;
		if (nbytes > maxNBytes)
			nbytes = maxNBytes;
		if (nbytes > 0)
			memcpy(buffer, msg + 1, nbytes);
		heapobj_free(&mq->pool, msg);
		syncobj_drain(&mq->sobj);
		goto done;
	}

	if (timeout == NO_WAIT) {
		errno = S_objLib_OBJ_UNAVAILABLE;
		goto done;
	}

	if (timeout != WAIT_FOREVER) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&wind_clock, timeout, timespec);
	} else
		timespec = NULL;

	wait = threadobj_prepare_wait(struct wind_queue_wait);
	wait->ptr = __moff(buffer);
	wait->size = maxNBytes;

	ret = syncobj_wait_grant(&mq->sobj, timespec, &syns);
	if (ret == -EIDRM) {
		errno = S_objLib_OBJ_DELETED;
		goto out;
	}
	if (ret == -ETIMEDOUT) {
		errno = S_objLib_OBJ_TIMEOUT;
		goto done;
	}
	nbytes = wait->size;
	if (nbytes == -1L)	/* No direct copy? */
		goto retry;
	syncobj_drain(&mq->sobj);
done:
	syncobj_unlock(&mq->sobj, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	CANCEL_RESTORE(svc);

	return nbytes;
}

STATUS msgQSend(MSG_Q_ID msgQId, const char *buffer, UINT bytes,
		int timeout, int prio)
{
	struct timespec ts, *timespec;
	struct wind_queue_wait *wait;
	struct threadobj *thobj;
	struct msgholder *msg;
	struct syncstate syns;
	struct wind_mq *mq;
	struct service svc;
	int ret = ERROR;
	UINT maxbytes;

	CANCEL_DEFER(svc);

	mq = find_mq_from_id(msgQId);
	if (mq == NULL)
		goto objid_error;

	if (syncobj_lock(&mq->sobj, &syns)) {
		CANCEL_RESTORE(svc);
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	if (bytes > mq->msgsize) {
		errno = S_msgQLib_INVALID_MSG_LENGTH;
		goto fail;
	}

	thobj = syncobj_peek_grant(&mq->sobj);
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

	if (mq->msgcount < mq->maxmsg)
		goto enqueue;

	if (timeout == NO_WAIT) {
		errno = S_objLib_OBJ_UNAVAILABLE;
		goto fail;
	}

	if (threadobj_irq_p()) {
		errno = S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL;
		goto fail;
	}

	if (timeout != WAIT_FOREVER) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&wind_clock, timeout, timespec);
	} else
		timespec = NULL;

	do {
		ret = syncobj_wait_drain(&mq->sobj, timespec, &syns);
		if (ret == -EIDRM) {
			errno = S_objLib_OBJ_DELETED;
			ret = ERROR;
			goto out;
		}
		if (ret == -ETIMEDOUT) {
			errno = S_objLib_OBJ_TIMEOUT;
			ret = ERROR;
			goto fail;
		}
	} while (mq->msgcount >= mq->maxmsg);

enqueue:
	msg = heapobj_alloc(&mq->pool, bytes + sizeof(*msg));
	if (msg == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		ret = ERROR;
		goto fail;
	}

	mq->msgcount++;
	assert(mq->msgcount <= mq->maxmsg); /* Paranoid. */
	msg->size = bytes;
	holder_init(&msg->link);

	if (bytes > 0)
		memcpy(msg + 1, buffer, bytes);

	if (prio == MSG_PRI_NORMAL)
		list_append(&msg->link, &mq->msg_list);
	else
		list_prepend(&msg->link, &mq->msg_list);

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
	if (thobj)	/* Wakeup waiter. */
		syncobj_grant_to(&mq->sobj, thobj);

	ret = OK;
fail:
	syncobj_unlock(&mq->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

int msgQNumMsgs(MSG_Q_ID msgQId)
{
	struct syncstate syns;
	struct wind_mq *mq;
	struct service svc;
	int msgcount;

	mq = find_mq_from_id(msgQId);
	if (mq == NULL)
		goto objid_error;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&mq->sobj, &syns)) {
		CANCEL_RESTORE(svc);
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	msgcount = mq->msgcount;
	syncobj_unlock(&mq->sobj, &syns);

	CANCEL_RESTORE(svc);

	return msgcount;
}

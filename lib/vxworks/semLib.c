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
#include <limits.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/heapobj.h>
#include <vxworks/errnoLib.h>

#include "reference.h"
#include "taskLib.h"
#include "semLib.h"
#include "tickLib.h"

/*
 * XXX: In order to keep the following services callable from
 * non-VxWorks tasks (but still Xenomai ones, though), make sure
 * to never depend on the wind_task struct, but rather on the basic
 * thread object directly.
 */

#define sem_magic	0x2a3b4c5d

static struct wind_sem *alloc_sem(int options, const struct wind_sem_ops *ops)
{
	struct wind_sem *sem;

	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return NULL;
	}

	sem->options = options;
	sem->semops = ops;
	sem->magic = sem_magic;

	return sem;
}

static STATUS xsem_take(struct wind_sem *sem, int timeout)
{
	struct timespec ts, *timespec;
	struct syncstate syns;
	struct service svc;
	STATUS ret = OK;

	if (threadobj_irq_p())
		return S_intLib_NOT_ISR_CALLABLE;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&sem->u.xsem.sobj, &syns)) {
		ret = S_objLib_OBJ_ID_ERROR;
		goto out;
	}

	if (--sem->u.xsem.value >= 0)
		goto done;

	if (timeout == NO_WAIT) {
		sem->u.xsem.value++;
		ret = S_objLib_OBJ_UNAVAILABLE;
		goto done;
	}

	if (timeout != WAIT_FOREVER) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&wind_clock, timeout, timespec);
	} else
		timespec = NULL;

	ret = syncobj_wait_grant(&sem->u.xsem.sobj, timespec, &syns);
	if (ret == -EIDRM) {
		ret = S_objLib_OBJ_DELETED;
		goto out;
	}
	if (ret) {
		sem->u.xsem.value++;
		if (ret == -ETIMEDOUT)
			ret = S_objLib_OBJ_TIMEOUT;
		else if (ret == -EINTR)
			ret = OK;	/* Flushed. */
	}
done:
	syncobj_unlock(&sem->u.xsem.sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

static STATUS xsem_give(struct wind_sem *sem)
{
	struct syncstate syns;
	struct service svc;
	STATUS ret = OK;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&sem->u.xsem.sobj, &syns)) {
		ret = S_objLib_OBJ_ID_ERROR;
		goto out;
	}

	if (sem->u.xsem.value >= sem->u.xsem.maxvalue) {
		if (sem->u.xsem.maxvalue == INT_MAX)
			/* No wrap around. */
			ret = S_semLib_INVALID_OPERATION;
	} else if (++sem->u.xsem.value <= 0)
		syncobj_grant_one(&sem->u.xsem.sobj);

	syncobj_unlock(&sem->u.xsem.sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

static STATUS xsem_flush(struct wind_sem *sem)
{
	struct syncstate syns;
	struct service svc;
	STATUS ret = OK;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&sem->u.xsem.sobj, &syns)) {
		ret = S_objLib_OBJ_ID_ERROR;
		goto out;
	}

	syncobj_flush(&sem->u.xsem.sobj);

	syncobj_unlock(&sem->u.xsem.sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

static void sem_finalize(struct syncobj *sobj)
{
	struct wind_sem *sem = container_of(sobj, struct wind_sem, u.xsem.sobj);
	xnfree(sem);
}
fnref_register(libvxworks, sem_finalize);

static STATUS xsem_delete(struct wind_sem *sem)
{
	struct syncstate syns;
	struct service svc;
	int ret = OK;

	if (threadobj_irq_p())
		return S_intLib_NOT_ISR_CALLABLE;

	CANCEL_DEFER(svc);

	if (syncobj_lock(&sem->u.xsem.sobj, &syns)) {
		ret = S_objLib_OBJ_ID_ERROR;
		goto out;
	}

	sem->magic = ~sem_magic; /* Prevent further reference. */
	syncobj_destroy(&sem->u.xsem.sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

static const struct wind_sem_ops xsem_ops = {
	.take = xsem_take,
	.give = xsem_give,
	.flush = xsem_flush,
	.delete = xsem_delete
};

static SEM_ID alloc_xsem(int options, int initval, int maxval)
{
	int sobj_flags = 0, ret;
	struct wind_sem *sem;

	if (options & ~SEM_Q_PRIORITY) {
		errno = S_semLib_INVALID_OPTION;
		return (SEM_ID)0;
	}

	sem = alloc_sem(options, &xsem_ops);
	if (sem == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return (SEM_ID)0;
	}

	if (options & SEM_Q_PRIORITY)
		sobj_flags = SYNCOBJ_PRIO;

	sem->u.xsem.value = initval;
	sem->u.xsem.maxvalue = maxval;
	ret = syncobj_init(&sem->u.xsem.sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libvxworks, sem_finalize));
	if (ret) {
		xnfree(sem);
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return (SEM_ID)0;
	}

	return mainheap_ref(sem, SEM_ID);
}

static STATUS msem_take(struct wind_sem *sem, int timeout)
{
	struct wind_task *current;
	struct timespec ts;
	int ret;

	if (threadobj_irq_p())
		return S_intLib_NOT_ISR_CALLABLE;

	/*
	 * We allow threads from other APIs to grab a VxWorks mutex
	 * ignoring the safe option in such a case.
	 */
	current = wind_task_current();
	if (current && (sem->options & SEM_DELETE_SAFE))
		__RT(pthread_mutex_lock(&current->safelock));

	if (timeout == NO_WAIT) {
		ret = __RT(pthread_mutex_trylock(&sem->u.msem.lock));
		goto check;
	}

	if  (timeout == WAIT_FOREVER) {
		ret = __RT(pthread_mutex_lock(&sem->u.msem.lock));
		goto check;
	}

	__clockobj_ticks_to_timeout(&wind_clock, CLOCK_REALTIME, timeout, &ts);
	ret = __RT(pthread_mutex_timedlock(&sem->u.msem.lock, &ts));
check:
	switch (ret) {
	case 0:
		return OK;
	case EINVAL:
		ret = S_objLib_OBJ_ID_ERROR;
		break;
	case EBUSY:
		ret = S_objLib_OBJ_UNAVAILABLE;
		break;
	case ETIMEDOUT:
		ret = S_objLib_OBJ_TIMEOUT;
		break;
	case EOWNERDEAD:
	case ENOTRECOVERABLE:
		warning("owner of mutex-type semaphore %p died", sem);
		ret = S_objLib_OBJ_UNAVAILABLE;
		break;
	}

	if (current != NULL && (sem->options & SEM_DELETE_SAFE))
		__RT(pthread_mutex_unlock(&current->safelock));

	return ret;
}

static STATUS msem_give(struct wind_sem *sem)
{
	struct wind_task *current;
	int ret;

	if (threadobj_irq_p())
		return S_intLib_NOT_ISR_CALLABLE;

	ret = __RT(pthread_mutex_unlock(&sem->u.msem.lock));
	if (ret == EINVAL)
		return S_objLib_OBJ_ID_ERROR;
	if (ret == EPERM)
		return S_semLib_INVALID_OPERATION;

	if (sem->options & SEM_DELETE_SAFE) {
		current = wind_task_current();
		if (current)
			__RT(pthread_mutex_unlock(&current->safelock));
	}

	return OK;
}

static STATUS msem_flush(struct wind_sem *sem)
{
	return S_semLib_INVALID_OPERATION;
}

static STATUS msem_delete(struct wind_sem *sem)
{
	int ret;

	if (threadobj_irq_p())
		return S_intLib_NOT_ISR_CALLABLE;

	ret = __RT(pthread_mutex_destroy(&sem->u.msem.lock));
	if (ret == EINVAL)
		return S_objLib_OBJ_ID_ERROR;
	/*
	 * XXX: We depart from the spec here since we can't flush, but
	 * we tell the caller about any pending task instead.
	 */
	if (ret == EBUSY)
		return S_semLib_INVALID_OPERATION;
	else
		xnfree(sem);

	return OK;
}

static const struct wind_sem_ops msem_ops = {
	.take = msem_take,
	.give = msem_give,
	.flush = msem_flush,
	.delete = msem_delete
};

SEM_ID semBCreate(int options, SEM_B_STATE state)
{
	struct service svc;
	SEM_ID sem_id;

	CANCEL_DEFER(svc);
	sem_id = alloc_xsem(options, state, 1);
	CANCEL_RESTORE(svc);

	return sem_id;
}

SEM_ID semCCreate(int options, int count)
{
	struct service svc;
	SEM_ID sem_id;

	CANCEL_DEFER(svc);
	sem_id = alloc_xsem(options, count, INT_MAX);
	CANCEL_RESTORE(svc);

	return sem_id;
}

SEM_ID semMCreate(int options)
{
	pthread_mutexattr_t mattr;
	struct wind_sem *sem;
	struct service svc;

	if (options & ~(SEM_Q_PRIORITY|SEM_DELETE_SAFE|SEM_INVERSION_SAFE)) {
		errno = S_semLib_INVALID_OPTION;
		return (SEM_ID)0;
	}

	if ((options & SEM_Q_PRIORITY) == 0) {
		if (options & SEM_INVERSION_SAFE) {
			errno = S_semLib_INVALID_QUEUE_TYPE; /* C'mon... */
			return (SEM_ID)0;
		}
	}

	CANCEL_DEFER(svc);

	sem = alloc_sem(options, &msem_ops);
	if (sem == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		CANCEL_RESTORE(svc);
		return (SEM_ID)0;
	}

	/*
	 * XXX: POSIX-wise, we have a few issues with emulating
	 * VxWorks semaphores of the mutex kind.
	 *
	 * VxWorks flushes any kind of semaphore upon deletion
	 * (however, explicit semFlush() is not allowed on the mutex
	 * kind though); but POSIX doesn't implement such mechanism on
	 * its mutex object. At the same time, we need priority
	 * inheritance when SEM_INVERSION_SAFE is passed, so we can't
	 * emulate VxWorks mutex semaphores using condvars. Since the
	 * only way to get priority inheritance is to use a POSIX
	 * mutex, we choose not to emulate flushing in semDelete(),
	 * but keep inversion-safe locking possible.
	 *
	 * The same way, we don't support FIFO ordering for mutexes,
	 * since this would require to handle them as recursive binary
	 * semaphores with ownership, for no obvious upside.
	 * Logically speaking, relying on recursion without any
	 * consideration for priority while serializing threads is
	 * just asking for troubles anyway.
	 */
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	/* pthread_mutexattr_setrobust() might not be implemented. */
	pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST_NP);
	if (options & SEM_INVERSION_SAFE)
		pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);

	__RT(pthread_mutex_init(&sem->u.msem.lock, &mattr));
	pthread_mutexattr_destroy(&mattr);

	CANCEL_RESTORE(svc);

	return mainheap_ref(sem, SEM_ID);
}

static struct wind_sem *find_sem_from_id(SEM_ID sem_id)
{
	struct wind_sem *sem = mainheap_deref(sem_id, struct wind_sem);

	if (sem == NULL || ((uintptr_t)sem & (sizeof(uintptr_t)-1)) != 0 ||
	    sem->magic != sem_magic)
		return NULL;

	return sem;
}

#define do_sem_op(sem_id, op, args...)			\
do {							\
	struct wind_sem *sem;				\
	struct service svc;				\
	STATUS ret;					\
							\
	sem = find_sem_from_id(sem_id);			\
	if (sem == NULL) {				\
		errno = S_objLib_OBJ_ID_ERROR;		\
		return ERROR;				\
	}						\
							\
	CANCEL_DEFER(svc);			\
	ret = sem->semops->op(sem , ##args);		\
	CANCEL_RESTORE(svc);			\
	if (ret) {					\
		errno = ret;				\
		ret = ERROR;				\
	}						\
							\
	return ret;					\
} while(0)

STATUS semDelete(SEM_ID sem_id)
{
	do_sem_op(sem_id, delete);
}

STATUS semGive(SEM_ID sem_id)
{
	do_sem_op(sem_id, give);
}

STATUS semTake(SEM_ID sem_id, int timeout)
{
	do_sem_op(sem_id, take, timeout);
}

STATUS semFlush(SEM_ID sem_id)
{
	do_sem_op(sem_id, flush);
}

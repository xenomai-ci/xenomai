/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <copperplate/threadobj.h>
#include <copperplate/registry-obstack.h>
#include "reference.h"
#include "internal.h"
#include "sem.h"
#include "timer.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_sem Semaphore services
 *
 * Counting semaphore IPC mechanism
 *
 * A counting semaphore is a synchronization object for controlling
 * the concurrency level allowed in accessing a resource from multiple
 * real-time tasks, based on the value of a count variable accessed
 * atomically.  The semaphore is used through the P ("Proberen", from
 * the Dutch "test and decrement") and V ("Verhogen", increment)
 * operations. The P operation decrements the semaphore count by one
 * if non-zero, or waits until a V operation is issued by another
 * task. Conversely, the V operation releases a resource by
 * incrementing the count by one, unblocking the heading task waiting
 * on the P operation if any. Waiting on a semaphore may cause
 * a priority inversion.
 *
 * If no more than a single resource is made available at any point in
 * time, the semaphore enforces mutual exclusion and thus can be used
 * to serialize access to a critical section. However, mutexes should
 * be used instead in order to prevent priority inversions, based on
 * the priority inheritance protocol.
 *
 * @{
 */

struct syncluster alchemy_sem_table;

static DEFINE_NAME_GENERATOR(sem_namegen, "sem",
			     struct alchemy_sem, name);

DEFINE_LOOKUP_PRIVATE(sem, RT_SEM);

#ifdef CONFIG_XENO_REGISTRY

static int sem_registry_open(struct fsobj *fsobj, void *priv)
{
	struct semobj_waitentry *waitlist, *p;
	struct fsobstack *o = priv;
	struct alchemy_sem *scb;
	size_t waitsz;
	int ret, val;

	scb = container_of(fsobj, struct alchemy_sem, fsobj);

	waitsz = sizeof(*p) * 256;
	waitlist = __STD(malloc(waitsz));
	if (waitlist == NULL)
		return -ENOMEM;

	ret = semobj_inquire(&scb->smobj, waitsz, waitlist, &val);
	if (ret < 0)
		goto out;

	fsobstack_init(o);

	if (val < 0)
		val = 0; /* Report depleted state as null. */

	fsobstack_grow_format(o, "=%d\n", val);

	if (ret) {
		fsobstack_grow_format(o, "--\n[WAITER]\n");
		p = waitlist;
		do {
			fsobstack_grow_format(o, "%s\n", p->name);
			p++;
		} while (--ret > 0);
	}

	fsobstack_finish(o);
out:
	__STD(free(waitlist));

	return ret;
}

static struct registry_operations registry_ops = {
	.open		= sem_registry_open,
	.release	= fsobj_obstack_release,
	.read		= fsobj_obstack_read
};

#else /* !CONFIG_XENO_REGISTRY */

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static void sem_finalize(struct semobj *smobj)
{
	struct alchemy_sem *scb = container_of(smobj, struct alchemy_sem, smobj);

	registry_destroy_file(&scb->fsobj);
	/* We should never fail here, so we backtrace. */
	__bt(syncluster_delobj(&alchemy_sem_table, &scb->cobj));
	scb->magic = ~sem_magic;
	xnfree(scb);
}
fnref_register(libalchemy, sem_finalize);

/**
 * @fn int rt_sem_create(RT_SEM *sem, const char *name, unsigned long icount, int mode)
 * @brief Create a counting semaphore.
 *
 * @param sem The address of a semaphore descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * semaphore. When non-NULL and non-empty, a copy of this string is
 * used for indexing the created semaphore into the object registry.
 *
 * @param icount The initial value of the counting semaphore.
 *
 * @param mode The semaphore creation mode. The following flags can be
 * OR'ed into this bitmask:
 *
 * - S_FIFO makes tasks pend in FIFO order on the semaphore.
 *
 * - S_PRIO makes tasks pend in priority order on the semaphore.
 *
 * - S_PULSE causes the semaphore to behave in "pulse" mode. In this
 * mode, the V (signal) operation attempts to release a single waiter
 * each time it is called, without incrementing the semaphore count,
 * even if no waiter is pending. For this reason, the semaphore count
 * in pulse mode remains zero.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a icount is non-zero and S_PULSE is set
 * in @a mode, or @a mode is otherwise invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the semaphore.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered semaphore.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 *
 * @note Semaphores can be shared by multiple processes which belong
 * to the same Xenomai session.
 */
int rt_sem_create(RT_SEM *sem, const char *name,
		  unsigned long icount, int mode)
{
	int smobj_flags = 0, ret;
	struct alchemy_sem *scb;
	struct service svc;

	if (threadobj_irq_p())
		return -EPERM;

	if (mode & ~(S_PRIO|S_PULSE))
		return -EINVAL;

	if (mode & S_PULSE) {
		if (icount > 0)
			return -EINVAL;
		smobj_flags |= SEMOBJ_PULSE;
	}

	CANCEL_DEFER(svc);

	scb = xnmalloc(sizeof(*scb));
	if (scb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if (mode & S_PRIO)
		smobj_flags |= SEMOBJ_PRIO;

	ret = semobj_init(&scb->smobj, smobj_flags, icount,
			  fnref_put(libalchemy, sem_finalize));
	if (ret) {
		xnfree(scb);
		goto out;
	}

	generate_name(scb->name, name, &sem_namegen);
	scb->magic = sem_magic;

	registry_init_file_obstack(&scb->fsobj, &registry_ops);
	ret = __bt(registry_add_file(&scb->fsobj, O_RDONLY,
				     "/alchemy/semaphores/%s", scb->name));
	if (ret) {
		warning("failed to export semaphore %s to registry, %s",
			scb->name, symerror(ret));
		ret = 0;
	}

	ret = syncluster_addobj(&alchemy_sem_table, scb->name, &scb->cobj);
	if (ret) {
		registry_destroy_file(&scb->fsobj);
		semobj_uninit(&scb->smobj);
		xnfree(scb);
	} else
		sem->handle = mainheap_ref(scb, uintptr_t);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_sem_delete(RT_SEM *sem)
 * @brief Delete a semaphore.
 *
 * This routine deletes a semaphore previously created by a call to
 * rt_sem_create().
 *
 * @param sem The semaphore descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a valid semaphore
 * descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{mode-unrestricted, switch-secondary}
 */
int rt_sem_delete(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	/*
	 * XXX: we rely on copperplate's semobj to check for semaphore
	 * existence, so we refrain from altering the object memory
	 * until we know it was valid. So the only safe place to
	 * negate the magic tag, deregister from the cluster and
	 * release the memory is in the finalizer routine, which is
	 * only called for valid objects.
	 */
	ret = semobj_destroy(&scb->smobj);
	if (ret > 0)
		ret = 0;
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_sem_p(RT_SEM *sem, RTIME timeout)
 * @brief Pend on a semaphore (with relative scalar timeout).
 *
 * This routine is a variant of rt_sem_p_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param sem The semaphore descriptor.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE causes the caller to block indefinitely until the
 * request is satisfied. Passing TM_NONBLOCK causes the service to
 * return without blocking in case the request cannot be satisfied
 * immediately.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn int rt_sem_p_until(RT_SEM *sem, RTIME abs_timeout)
 * @brief Pend on a semaphore (with absolute scalar timeout).
 *
 * This routine is a variant of rt_sem_p_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param sem The semaphore descriptor.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * the request is satisfied. Passing TM_NONBLOCK causes the service
 * to return without blocking in case the request cannot be satisfied
 * immediately.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn int rt_sem_p_timed(RT_SEM *sem, const struct timespec *abs_timeout)
 * @brief Pend on a semaphore.
 *
 * Test and decrement the semaphore count. If the semaphore value is
 * greater than zero, it is decremented by one and the service
 * immediately returns to the caller. Otherwise, the caller is blocked
 * until the semaphore is either signaled or destroyed, unless a
 * non-blocking operation was required.
 *
 * @param sem The semaphore descriptor.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for the
 * request to be satisfied. Passing NULL causes the caller to block
 * indefinitely until the request is satisfied. Passing { .tv_sec = 0,
 * .tv_nsec = 0 } causes the service to return without blocking in case
 * the request cannot be satisfied immediately.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a abs_timeout is reached before the
 * request is satisfied.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and the semaphore count is zero on entry.

 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before the request is satisfied.
 *
 * - -EINVAL is returned if @a sem is not a valid semaphore
 * descriptor.
 *
 * - -EIDRM is returned if @a sem is deleted while the caller was
 * sleeping on it. In such a case, @a sem is no more valid upon
 * return of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 */
int rt_sem_p_timed(RT_SEM *sem, const struct timespec *abs_timeout)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_wait(&scb->smobj, abs_timeout);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_sem_v(RT_SEM *sem)
 * @brief Signal a semaphore.
 *
 * If the semaphore is pended, the task heading the wait queue is
 * immediately unblocked. Otherwise, the semaphore count is
 * incremented by one, unless the semaphore is used in "pulse" mode
 * (see rt_sem_create()).
 *
 * @param sem The semaphore descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a valid semaphore
 * descriptor.
 *
 * @apitags{unrestricted}
 */
int rt_sem_v(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_post(&scb->smobj);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_sem_broadcast(RT_SEM *sem)
 * @brief Broadcast a semaphore.
 *
 * All tasks currently waiting on the semaphore are immediately
 * unblocked. The semaphore count is set to zero.
 *
 * @param sem The semaphore descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a valid semaphore
 * descriptor.
 *
 * @apitags{unrestricted}
 */
int rt_sem_broadcast(RT_SEM *sem)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_broadcast(&scb->smobj);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
 * @brief Query semaphore status.
 *
 * This routine returns the status information about the specified
 * semaphore.
 *
 * @param sem The semaphore descriptor.
 *
 * @param info A pointer to the @ref RT_SEM_INFO "return
 * buffer" to copy the information to.
 *
 * @return Zero is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a valid semaphore
 * descriptor.
 *
 * @apitags{unrestricted}
 */
int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
{
	struct alchemy_sem *scb;
	struct service svc;
	int ret = 0, sval;

	CANCEL_DEFER(svc);

	scb = find_alchemy_sem(sem, &ret);
	if (scb == NULL)
		goto out;

	ret = semobj_getvalue(&scb->smobj, &sval);
	if (ret)
		goto out;

	info->count = sval < 0 ? 0 : sval;
	info->nwaiters = sval < 0 ? -sval : 0;
	strcpy(info->name, scb->name); /* <= racy. */
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_sem_bind(RT_SEM *sem, const char *name, RTIME timeout)
 * @brief Bind to a semaphore.
 *
 * This routine creates a new descriptor to refer to an existing
 * semaphore identified by its symbolic name. If the object does not
 * exist on entry, the caller may block until a semaphore of the given
 * name is created.
 *
 * @param sem The address of a semaphore descriptor filled in by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param name A valid NULL-terminated name which identifies the
 * semaphore to bind to. This string should match the object name
 * argument passed to rt_sem_create().
 *
 * @param timeout The number of clock ticks to wait for the
 * registration to occur (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until the object is
 * registered. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if the object is not registered on
 * entry.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the searched object is not registered on entry.
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 *
 * @note The @a timeout value is interpreted as a multiple of the
 * Alchemy clock resolution (see --alchemy-clock-resolution option,
 * defaults to 1 nanosecond).
 */
int rt_sem_bind(RT_SEM *sem,
		const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_sem_table,
				   timeout,
				   offsetof(struct alchemy_sem, cobj),
				   &sem->handle);
}

/**
 * @fn int rt_sem_unbind(RT_SEM *sem)
 * @brief Unbind from a semaphore.
 *
 * @param sem The semaphore descriptor.
 *
 * This routine releases a previous binding to a semaphore. After this
 * call has returned, the descriptor is no more valid for referencing
 * this object.
 *
 * @apitags{thread-unrestricted}
 */
int rt_sem_unbind(RT_SEM *sem)
{
	sem->handle = 0;
	return 0;
}

/** @} */

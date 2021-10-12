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
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include "internal.h"
#include "mutex.h"
#include "timer.h"
#include "task.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_mutex Mutex services
 *
 * POSIXish mutual exclusion servicesl
 *
 * A mutex is a MUTual EXclusion object, and is useful for protecting
 * shared data structures from concurrent modifications, and
 * implementing critical sections and monitors.
 *
 * A mutex has two possible states: unlocked (not owned by any task),
 * and locked (owned by one task). A mutex can never be owned by two
 * different tasks simultaneously. A task attempting to lock a mutex
 * that is already locked by another task is blocked until the latter
 * unlocks the mutex first.
 *
 * Xenomai mutex services enforce a priority inheritance protocol in
 * order to solve priority inversions.
 *
 * @{
 */
struct syncluster alchemy_mutex_table;

static DEFINE_NAME_GENERATOR(mutex_namegen, "mutex",
			     struct alchemy_mutex, name);

DEFINE_LOOKUP(mutex, RT_MUTEX);

#ifdef CONFIG_XENO_REGISTRY

static ssize_t mutex_registry_read(struct fsobj *fsobj,
				   char *buf, size_t size, off_t offset,
				   void *priv)
{
	return 0;		/* FIXME */
}

static struct registry_operations registry_ops = {
	.read	= mutex_registry_read
};

#else /* !CONFIG_XENO_REGISTRY */

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

/**
 * @fn int rt_mutex_create(RT_MUTEX *mutex, const char *name)
 * @brief Create a mutex.
 *
 * Create a mutual exclusion object that allows multiple tasks to
 * synchronize access to a shared resource. A mutex is left in an
 * unlocked state after creation.
 *
 * @param mutex The address of a mutex descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * mutex. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created mutex into the object registry.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the mutex.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered mutex.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 *
 * @note Mutexes can be shared by multiple processes which belong to
 * the same Xenomai session.
 */
int rt_mutex_create(RT_MUTEX *mutex, const char *name)
{
	struct alchemy_mutex *mcb;
	pthread_mutexattr_t mattr;
	struct service svc;
	int ret;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	mcb = xnmalloc(sizeof(*mcb));
	if (mcb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * XXX: we can't have priority inheritance with syncobj, so we
	 * have to base this code directly over the POSIX layer.
	 */
	generate_name(mcb->name, name, &mutex_namegen);
	mcb->owner = NO_ALCHEMY_TASK;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	/* pthread_mutexattr_setrobust_np() might not be implemented. */
	pthread_mutexattr_setrobust_np(&mattr, PTHREAD_MUTEX_ROBUST_NP);
	ret = __RT(pthread_mutex_init(&mcb->lock, &mattr));
	if (ret) {
		xnfree(mcb);
		goto out;
	}
	
	pthread_mutexattr_destroy(&mattr);

	mcb->magic = mutex_magic;

	registry_init_file(&mcb->fsobj, &registry_ops, 0);
	ret = __bt(registry_add_file(&mcb->fsobj, O_RDONLY,
				     "/alchemy/mutexes/%s", mcb->name));
	if (ret) {
		warning("failed to export mutex %s to registry, %s",
			mcb->name, symerror(ret));
		ret = 0;
	}

	ret = syncluster_addobj(&alchemy_mutex_table, mcb->name, &mcb->cobj);
	if (ret) {
		registry_destroy_file(&mcb->fsobj);
		xnfree(mcb);
	} else
		mutex->handle = mainheap_ref(mcb, uintptr_t);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_mutex_delete(RT_MUTEX *mutex)
 * @brief Delete a mutex.
 *
 * This routine deletes a mutex object previously created by a call to
 * rt_mutex_create().
 *
 * @param mutex The mutex descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid mutex descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * - -EBUSY is returned upon an attempt to destroy the object
 * referenced by @a mutex while it is referenced (for example, while
 * being used in a rt_mutex_acquire(), rt_mutex_acquire_timed() or
 * rt_mutex_acquire_until() by another task).
 *
 * @apitags{mode-unrestricted, switch-secondary}
 */
int rt_mutex_delete(RT_MUTEX *mutex)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	ret = -__RT(pthread_mutex_destroy(&mcb->lock));
	if (ret)
		goto out;

	mcb->magic = ~mutex_magic;
	syncluster_delobj(&alchemy_mutex_table, &mcb->cobj);
	registry_destroy_file(&mcb->fsobj);
	xnfree(mcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_mutex_acquire(RT_MUTEX *mutex, RTIME timeout)
 * @brief Acquire/lock a mutex (with relative scalar timeout).
 *
 * This routine is a variant of rt_mutex_acquire_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param mutex The mutex descriptor.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE the caller to block indefinitely. Passing TM_NONBLOCK
 * causes the service to return immediately without blocking in case
 * @a mutex is already locked by another task.
 *
 * @apitags{xthread-only, switch-primary}
 */

/**
 * @fn int rt_mutex_acquire_until(RT_MUTEX *mutex, RTIME abs_timeout)
 * @brief Acquire/lock a mutex (with absolute scalar timeout).
 *
 * This routine is a variant of rt_mutex_acquire_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param mutex The mutex descriptor.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE the caller to block indefinitely. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * blocking in case @a mutex is already locked by another task.
 *
 * @apitags{xthread-only, switch-primary}
 */

/**
 * @fn int rt_mutex_acquire_timed(RT_MUTEX *mutex, const struct timespec *abs_timeout)
 * @brief Acquire/lock a mutex (with absolute timeout date).
 *
 * Attempt to lock a mutex. The calling task is blocked until the
 * mutex is available, in which case it is locked again before this
 * service returns. Xenomai mutexes are implicitely recursive and
 * implement the priority inheritance protocol.
 *
 * @param mutex The mutex descriptor.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for the
 * mutex to be available. Passing NULL the caller to block indefinitely.
 * Passing { .tv_sec = 0, .tv_nsec = 0 } causes the service to return
 * immediately without blocking in case @a mutex is already locked by
 * another task.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a abs_timeout is reached before the
 * mutex is available.
 *
 * - -EWOULDBLOCK is returned if @a timeout is { .tv_sec = 0, .tv_nsec
 * = 0 } and the mutex is not immediately available.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task.
 *
 * - -EINVAL is returned if @a mutex is not a valid mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is deleted while the caller was
 * waiting on it. In such event, @a mutex is no more valid upon return
 * of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-only, switch-primary}
 *
 * @sideeffect
 * Over the Cobalt core, an Alchemy task with priority zero keeps
 * running in primary mode until it releases the mutex, at which point
 * it is switched back to secondary mode automatically.
 */
int rt_mutex_acquire_timed(RT_MUTEX *mutex,
			   const struct timespec *abs_timeout)
{
	struct alchemy_task *current;
	struct alchemy_mutex *mcb;
	struct timespec ts;
	int ret = 0;

	/* This must be an alchemy task. */
	current = alchemy_task_current();
	if (current == NULL)
		return -EPERM;

	/*
	 * Try the fast path first. Note that we don't need any
	 * protected section here: the caller should have provided for
	 * it.
	 */
	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		return ret;

	/*
	 * We found the mutex, but locklessly: let the POSIX layer
	 * check for object existence.
	 */
	ret = -__RT(pthread_mutex_trylock(&mcb->lock));
	if (ret == 0 || ret != -EBUSY || alchemy_poll_mode(abs_timeout))
		goto done;

	/* Slow path. */
	if (abs_timeout == NULL) {
		ret = -__RT(pthread_mutex_lock(&mcb->lock));
		goto done;
	}

	/*
	 * What a mess: we want all our timings to be based on
	 * CLOCK_COPPERPLATE, but pthread_mutex_timedlock() is
	 * implicitly based on CLOCK_REALTIME, so we need to translate
	 * the user timeout into something POSIX understands.
	 */
	clockobj_convert_clocks(&alchemy_clock, abs_timeout, CLOCK_REALTIME, &ts);
	ret = -__RT(pthread_mutex_timedlock(&mcb->lock, &ts));
done:
	switch (ret) {
	case -ENOTRECOVERABLE:
		ret = -EOWNERDEAD;
	case -EOWNERDEAD:
		warning("owner of mutex 0x%x died", mutex->handle);
		break;
	case -EBUSY:
		/*
		 * Remap EBUSY -> EWOULDBLOCK: not very POSIXish, but
		 * consistent with similar cases in the Alchemy API.
		 */
		ret = -EWOULDBLOCK;
		break;
	case 0:
		mcb->owner.handle = mainheap_ref(current, uintptr_t);
	}

	return ret;
}

/**
 * @fn int rt_mutex_release(RT_MUTEX *mutex)
 * @brief Release/unlock a mutex.
 *
 * This routine releases a mutex object previously locked by a call to
 * rt_mutex_acquire() or rt_mutex_acquire_until().  If the mutex is
 * pended, the first waiting task (by priority order) is immediately
 * unblocked and transfered the ownership of the mutex; otherwise, the
 * mutex is left in an unlocked state.
 *
 * @param mutex The mutex descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a alarm is not a valid mutex descriptor.
 *
 * - -EPERM is returned if @a mutex is not owned by the current task,
 * or more generally if this service was called from a context which
 * cannot own any mutex (e.g. interrupt context).
 *
 * @apitags{xthread-only, switch-primary}
 */
int rt_mutex_release(RT_MUTEX *mutex)
{
	struct alchemy_mutex *mcb;
	int ret = 0;

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		return ret;

	/* Let the POSIX layer check for object existence. */
	return -__RT(pthread_mutex_unlock(&mcb->lock));
}

/**
 * @fn int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
 * @brief Query mutex status.
 *
 * This routine returns the status information about the specified
 * mutex.
 *
 * @param mutex The mutex descriptor.
 *
 * @param info A pointer to the @ref RT_MUTEX_INFO "return
 * buffer" to copy the information to.
 *
 * @return Zero is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a valid mutex descriptor.
 *
 * - -EPERM is returned if this service is called from an interrupt
 * context.
 *
 * @apitags{xthread-only, switch-primary}
 */
int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
{
	struct alchemy_mutex *mcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	mcb = find_alchemy_mutex(mutex, &ret);
	if (mcb == NULL)
		goto out;

	ret = -__RT(pthread_mutex_trylock(&mcb->lock));
	if (ret) {
		if (ret != -EBUSY)
			goto out;
		info->owner = mcb->owner;
		ret = 0;
	} else {
		__RT(pthread_mutex_unlock(&mcb->lock));
		info->owner = NO_ALCHEMY_TASK;
	}

	strcpy(info->name, mcb->name);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_mutex_bind(RT_MUTEX *mutex, const char *name, RTIME timeout)
 * @brief Bind to a mutex.
 *
 * This routine creates a new descriptor to refer to an existing mutex
 * identified by its symbolic name. If the object not exist on entry,
 * the caller may block until a mutex of the given name is created.
 *
 * @param mutex The address of a mutex descriptor filled in by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param name A valid NULL-terminated name which identifies the mutex
 * to bind to. This string should match the object name argument
 * passed to rt_mutex_create().
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
int rt_mutex_bind(RT_MUTEX *mutex,
		  const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_mutex_table,
				   timeout,
				   offsetof(struct alchemy_mutex, cobj),
				   &mutex->handle);
}

/**
 * @fn int rt_mutex_unbind(RT_MUTEX *mutex)
 * @brief Unbind from a mutex.
 *
 * @param mutex The mutex descriptor.
 *
 * This routine releases a previous binding to a mutex. After this
 * call has returned, the descriptor is no more valid for referencing
 * this object.
 */
int rt_mutex_unbind(RT_MUTEX *mutex)
{
	mutex->handle = 0;
	return 0;
}

/** @} */

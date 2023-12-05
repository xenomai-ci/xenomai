/*
 * Copyright (C) 2008-2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <assert.h>
#include <errno.h>
#include "boilerplate/lock.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"

/*
 * XXX: The POSIX spec states that "Synchronization primitives that
 * attempt to interfere with scheduling policy by specifying an
 * ordering rule are considered undesirable. Threads waiting on
 * mutexes and condition variables are selected to proceed in an order
 * dependent upon the scheduling policy rather than in some fixed
 * order (for example, FIFO or priority). Thus, the scheduling policy
 * determines which thread(s) are awakened and allowed to proceed.".
 * Linux enforces this by always queuing SCHED_FIFO waiters by
 * priority when sleeping on futex objects, which underlay mutexes and
 * condition variables.
 *
 * Unfortunately, most non-POSIX RTOS do allow specifying the queuing
 * order which applies to their synchronization objects at creation
 * time, and ignoring the FIFO queuing requirement may break the
 * application in case a fair attribution of the resource is
 * expected. Therefore, we must emulate FIFO ordering, and we do that
 * using an internal queue. We also use this queue to implement the
 * flush operation on synchronization objects which POSIX does not
 * provide either.
 *
 * The syncobj abstraction is based on a complex monitor object to
 * wait for resources, either implemented natively by Cobalt or
 * emulated via a mutex and two condition variables over Mercury (one
 * of which being hosted by the thread object implementation).
 *
 * NOTE: we don't do error backtracing in this file, since error
 * returns when locking, pending or deleting sync objects usually
 * express normal runtime conditions.
 */

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

static inline
int monitor_enter(struct syncobj *sobj)
{
	return cobalt_monitor_enter(&sobj->core.monitor);
}

static inline
void monitor_exit(struct syncobj *sobj)
{
	int ret;
	ret = cobalt_monitor_exit(&sobj->core.monitor);
	assert(ret == 0);
	(void)ret;
}

static inline
int monitor_wait_grant(struct syncobj *sobj,
		       struct threadobj *current,
		       const struct timespec *timeout)
{
	return cobalt_monitor_wait(&sobj->core.monitor,
				   COBALT_MONITOR_WAITGRANT,
				   timeout);
}

static inline
int monitor_wait_drain(struct syncobj *sobj,
		       struct threadobj *current,
		       const struct timespec *timeout)
{
	return cobalt_monitor_wait(&sobj->core.monitor,
				   COBALT_MONITOR_WAITDRAIN,
				   timeout);
}

static inline
void monitor_grant(struct syncobj *sobj, struct threadobj *thobj)
{
	cobalt_monitor_grant(&sobj->core.monitor,
			     threadobj_get_window(&thobj->core));
}

static inline
void monitor_drain_all(struct syncobj *sobj)
{
	cobalt_monitor_drain_all(&sobj->core.monitor);
}

static inline int syncobj_init_corespec(struct syncobj *sobj,
					clockid_t clk_id)
{
	int flags = monitor_scope_attribute;

	return __bt(cobalt_monitor_init(&sobj->core.monitor, clk_id, flags));
}

static inline void syncobj_cleanup_corespec(struct syncobj *sobj)
{
	/* We hold the gate lock while destroying. */
	int ret = cobalt_monitor_destroy(&sobj->core.monitor);
	/* Let earlier EPERM condition propagate, don't trap. */
	assert(ret == 0 || ret == -EPERM);
	(void)ret;
}

#else /* CONFIG_XENO_MERCURY */

static inline
int monitor_enter(struct syncobj *sobj)
{
	return -pthread_mutex_lock(&sobj->core.lock);
}

static inline
void monitor_exit(struct syncobj *sobj)
{
	int ret;
	ret = pthread_mutex_unlock(&sobj->core.lock);
	assert(ret == 0); (void)ret;
}

static inline
int monitor_wait_grant(struct syncobj *sobj,
		       struct threadobj *current,
		       const struct timespec *timeout)
{
	if (timeout)
		return -threadobj_cond_timedwait(&current->core.grant_sync,
						 &sobj->core.lock, timeout);

	return -threadobj_cond_wait(&current->core.grant_sync, &sobj->core.lock);
}

static inline
int monitor_wait_drain(struct syncobj *sobj,
		       struct threadobj *current,
		       const struct timespec *timeout)
{
	if (timeout)
		return -threadobj_cond_timedwait(&sobj->core.drain_sync,
						 &sobj->core.lock,
						 timeout);

	return -threadobj_cond_wait(&sobj->core.drain_sync, &sobj->core.lock);
}

static inline
void monitor_grant(struct syncobj *sobj, struct threadobj *thobj)
{
	threadobj_cond_signal(&thobj->core.grant_sync);
}

static inline
void monitor_drain_all(struct syncobj *sobj)
{
	threadobj_cond_broadcast(&sobj->core.drain_sync);
}

/*
 * Over Mercury, we implement a complex monitor via a mutex and a
 * couple of condvars, one in the syncobj and the other owned by the
 * thread object.
 */
static inline int syncobj_init_corespec(struct syncobj *sobj,
					clockid_t clk_id)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	int ret;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	ret = __bt(-pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute));
	if (ret) {
		pthread_mutexattr_destroy(&mattr);
		return ret;
	}

	ret = __bt(-pthread_mutex_init(&sobj->core.lock, &mattr));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	ret = __bt(pthread_condattr_setclock(&cattr, clk_id));
	if (ret)
		goto fail;

	ret = __bt(-pthread_cond_init(&sobj->core.drain_sync, &cattr));
	pthread_condattr_destroy(&cattr);
	if (ret) {
	fail:
		pthread_mutex_destroy(&sobj->core.lock);
		return ret;
	}

	return 0;
}

static inline void syncobj_cleanup_corespec(struct syncobj *sobj)
{
	monitor_exit(sobj);
	pthread_cond_destroy(&sobj->core.drain_sync);
	pthread_mutex_destroy(&sobj->core.lock);
}

#endif	/* CONFIG_XENO_MERCURY */

int syncobj_init(struct syncobj *sobj, clockid_t clk_id, int flags,
		 fnref_type(void (*)(struct syncobj *sobj)) finalizer)
{
	sobj->flags = flags;
	list_init(&sobj->grant_list);
	list_init(&sobj->drain_list);
	sobj->grant_count = 0;
	sobj->drain_count = 0;
	sobj->wait_count = 0;
	sobj->finalizer = finalizer;
	sobj->magic = SYNCOBJ_MAGIC;

	return __bt(syncobj_init_corespec(sobj, clk_id));
}

int syncobj_lock(struct syncobj *sobj, struct syncstate *syns)
{
	int ret, oldstate;

	/*
	 * This magic prevents concurrent locking while a deletion is
	 * in progress, waiting for the release count to drop to zero.
	 */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	ret = monitor_enter(sobj);
	if (ret)
		goto fail;

	/* Check for an ongoing deletion. */
	if (sobj->magic != SYNCOBJ_MAGIC) {
		monitor_exit(sobj);
		ret = -EINVAL;
		goto fail;
	}

	syns->state = oldstate;
	__syncobj_tag_locked(sobj);
	return 0;
fail:
	pthread_setcancelstate(oldstate, NULL);
	return ret;
}

void syncobj_unlock(struct syncobj *sobj, struct syncstate *syns)
{
	__syncobj_tag_unlocked(sobj);
	monitor_exit(sobj);
	pthread_setcancelstate(syns->state, NULL);
}

static void __syncobj_finalize(struct syncobj *sobj)
{
	void (*finalizer)(struct syncobj *sobj);

	/*
	 * Cancelability is still disabled or we are running over the
	 * thread finalizer, therefore we can't be wiped off in the
	 * middle of the finalization process.
	 */
	syncobj_cleanup_corespec(sobj);
	fnref_get(finalizer, sobj->finalizer);
	if (finalizer)
		finalizer(sobj);
}

int __syncobj_broadcast_grant(struct syncobj *sobj, int reason)
{
	struct threadobj *thobj;
	int ret;

	assert(!list_empty(&sobj->grant_list));

	do {
		thobj = list_pop_entry(&sobj->grant_list,
				       struct threadobj, wait_link);
		thobj->wait_status |= reason;
		thobj->wait_sobj = NULL;
		monitor_grant(sobj, thobj);
	} while (!list_empty(&sobj->grant_list));

	ret = sobj->grant_count;
	sobj->grant_count = 0;

	return ret;
}

int __syncobj_broadcast_drain(struct syncobj *sobj, int reason)
{
	struct threadobj *thobj;
	int ret;

	assert(!list_empty(&sobj->drain_list));

	do {
		thobj = list_pop_entry(&sobj->drain_list,
				       struct threadobj, wait_link);
		thobj->wait_sobj = NULL;
		thobj->wait_status |= reason;
	} while (!list_empty(&sobj->drain_list));

	monitor_drain_all(sobj);

	ret = sobj->drain_count;
	sobj->drain_count = 0;

	return ret;
}

static inline void enqueue_waiter(struct syncobj *sobj,
				  struct threadobj *thobj)
{
	struct threadobj *__thobj;

	thobj->wait_prio = thobj->global_priority;
	if (list_empty(&sobj->grant_list) || (sobj->flags & SYNCOBJ_PRIO) == 0) {
		list_append(&thobj->wait_link, &sobj->grant_list);
		return;
	}

	list_for_each_entry_reverse(__thobj, &sobj->grant_list, wait_link) {
		if (thobj->wait_prio <= __thobj->wait_prio)
			break;
	}
	ath(&__thobj->wait_link, &thobj->wait_link);
}

static inline void dequeue_waiter(struct syncobj *sobj,
				  struct threadobj *thobj)
{
	list_remove(&thobj->wait_link);
	if (thobj->wait_status & SYNCOBJ_DRAINWAIT)
		sobj->drain_count--;
	else
		sobj->grant_count--;

	assert(sobj->wait_count > 0);
}

/*
 * NOTE: we don't use POSIX cleanup handlers in syncobj_wait_grant() and
 * syncobj_wait() on purpose: these may have a significant impact on
 * latency due to I-cache misses on low-end hardware (e.g. ~6 us on
 * MPC5200), particularly when unwinding the cancel frame. So the
 * cleanup handler below is called by the threadobj finalizer instead
 * when appropriate, since we have enough internal information to
 * handle this situation.
 */
void __syncobj_cleanup_wait(struct syncobj *sobj, struct threadobj *thobj)
{
	/*
	 * We don't care about resetting the original cancel type
	 * saved in the syncstate struct since we are there precisely
	 * because the caller got cancelled while sleeping on the
	 * GRANT/DRAIN condition.
	 */
	dequeue_waiter(sobj, thobj);

	if (--sobj->wait_count == 0 && sobj->magic != SYNCOBJ_MAGIC) {
		__syncobj_finalize(sobj);
		return;
	}

	monitor_exit(sobj);
}

struct threadobj *syncobj_grant_one(struct syncobj *sobj)
{
	struct threadobj *thobj;

	__syncobj_check_locked(sobj);

	if (list_empty(&sobj->grant_list))
		return NULL;

	thobj = list_pop_entry(&sobj->grant_list, struct threadobj, wait_link);
	thobj->wait_status |= SYNCOBJ_SIGNALED;
	thobj->wait_sobj = NULL;
	sobj->grant_count--;
	monitor_grant(sobj, thobj);

	return thobj;
}

void syncobj_grant_to(struct syncobj *sobj, struct threadobj *thobj)
{
	__syncobj_check_locked(sobj);

	list_remove(&thobj->wait_link);
	thobj->wait_status |= SYNCOBJ_SIGNALED;
	thobj->wait_sobj = NULL;
	sobj->grant_count--;
	monitor_grant(sobj, thobj);
}

struct threadobj *syncobj_peek_grant(struct syncobj *sobj)
{
	struct threadobj *thobj;

	__syncobj_check_locked(sobj);

	if (list_empty(&sobj->grant_list))
		return NULL;

	thobj = list_first_entry(&sobj->grant_list, struct threadobj,
				 wait_link);
	return thobj;
}

struct threadobj *syncobj_peek_drain(struct syncobj *sobj)
{
	struct threadobj *thobj;

	__syncobj_check_locked(sobj);

	if (list_empty(&sobj->drain_list))
		return NULL;

	thobj = list_first_entry(&sobj->drain_list, struct threadobj,
				 wait_link);
	return thobj;
}

static int wait_epilogue(struct syncobj *sobj,
			 struct syncstate *syns,
			 struct threadobj *current,
			 int ret)
{
	current->run_state = __THREAD_S_RUNNING;

	/*
	 * Fixup a potential race upon return from grant/drain_wait
	 * operations, e.g. given two threads A and B:
	 *
	 * A:enqueue_waiter(self)
	 * A:monitor_wait
	 *    A:monitor_unlock
	 *    A:[timed] sleep
	 *    A:wakeup on timeout/interrupt
	 *       B:monitor_lock
	 *       B:look_for_queued_waiter
	 *          (found A, update A's state)
	 *       B:monitor_unlock
	 *    A:dequeue_waiter(self)
	 *    A:return -ETIMEDOUT/-EINTR
	 *
	 * The race may happen anytime between the timeout/interrupt
	 * event is received by A, and the moment it grabs back the
	 * monitor lock before unqueuing. When the race happens, B can
	 * squeeze in a signal before A unqueues after resumption on
	 * error.
	 *
	 * Problem: A's internal state has been updated (e.g. some
	 * data transferred to it), but it will receive
	 * -ETIMEDOUT/-EINTR, causing it to miss the update
	 * eventually.
	 *
	 * Solution: fixup the status code upon return from
	 * wait_grant/drain operations, so that -ETIMEDOUT/-EINTR is
	 * never returned to the caller if the syncobj was actually
	 * signaled. We still allow the SYNCOBJ_FLUSHED condition to
	 * override that success code though.
	 *
	 * Whether a condition should be deemed satisfied if it is
	 * signaled during the race window described above is
	 * debatable, but this is a simple and straightforward way to
	 * handle such grey area.
	 */

	if (current->wait_sobj) {
		dequeue_waiter(sobj, current);
		current->wait_sobj = NULL;
	} else if (ret == -ETIMEDOUT || ret == -EINTR)
		ret = 0;

	sobj->wait_count--;
	assert(sobj->wait_count >= 0);

	if (sobj->magic != SYNCOBJ_MAGIC) {
		if (sobj->wait_count == 0)
			__syncobj_finalize(sobj);
		else
			monitor_exit(sobj);
		pthread_setcancelstate(syns->state, NULL);
		return -EIDRM;
	}

	if (current->wait_status & SYNCOBJ_FLUSHED)
		return -EINTR;

	return ret;
}

int syncobj_wait_grant(struct syncobj *sobj, const struct timespec *timeout,
		       struct syncstate *syns)
{
	struct threadobj *current = threadobj_current();
	int ret, state;

	__syncobj_check_locked(sobj);

	assert(current != NULL);

	current->run_state = timeout ? __THREAD_S_TIMEDWAIT : __THREAD_S_WAIT;
	threadobj_save_timeout(&current->core, timeout);
	current->wait_status = 0;
	enqueue_waiter(sobj, current);
	current->wait_sobj = sobj;
	sobj->grant_count++;
	sobj->wait_count++;

	/*
	 * NOTE: we are guaranteed to be in deferred cancel mode, with
	 * cancelability disabled (in syncobj_lock); re-enable it
	 * before pending on the condvar.
	 */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
	assert(state == PTHREAD_CANCEL_DISABLE);

	do {
		__syncobj_tag_unlocked(sobj);
		ret = monitor_wait_grant(sobj, current, timeout);
		__syncobj_tag_locked(sobj);
		/* Check for spurious wake up. */
	} while (ret == 0 && current->wait_sobj);

	pthread_setcancelstate(state, NULL);

	return wait_epilogue(sobj, syns, current, ret);
}

int syncobj_wait_drain(struct syncobj *sobj, const struct timespec *timeout,
		       struct syncstate *syns)
{
	struct threadobj *current = threadobj_current();
	int ret, state;

	__syncobj_check_locked(sobj);

	assert(current != NULL);

	current->run_state = timeout ? __THREAD_S_TIMEDWAIT : __THREAD_S_WAIT;
	threadobj_save_timeout(&current->core, timeout);
	current->wait_status = SYNCOBJ_DRAINWAIT;
	list_append(&current->wait_link, &sobj->drain_list);
	current->wait_sobj = sobj;
	sobj->drain_count++;
	sobj->wait_count++;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
	assert(state == PTHREAD_CANCEL_DISABLE);

	/*
	 * NOTE: Since the DRAINED signal is broadcast to all waiters,
	 * a race may exist for acting upon it among those
	 * threads. Therefore the caller must check that the drain
	 * condition is still true before proceeding.
	 */
	do {
		__syncobj_tag_unlocked(sobj);
		ret = monitor_wait_drain(sobj, current, timeout);
		__syncobj_tag_locked(sobj);
	} while (ret == 0 && current->wait_sobj);

	pthread_setcancelstate(state, NULL);

	return wait_epilogue(sobj, syns, current, ret);
}

int syncobj_destroy(struct syncobj *sobj, struct syncstate *syns)
{
	int ret;

	__syncobj_check_locked(sobj);

	sobj->magic = ~SYNCOBJ_MAGIC;
	ret = syncobj_flush(sobj);
	if (ret) {
		syncobj_unlock(sobj, syns);
		return ret;
	}

	/* No thread awaken - we may dispose immediately. */
	__syncobj_finalize(sobj);
	pthread_setcancelstate(syns->state, NULL);

	return 0;
}

void syncobj_uninit(struct syncobj *sobj)
{
	monitor_enter(sobj);
	assert(sobj->wait_count == 0);
	syncobj_cleanup_corespec(sobj);
}

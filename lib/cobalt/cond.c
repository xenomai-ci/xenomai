/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_cond Condition variables
 *
 * Cobalt/POSIX condition variable services
 *
 * A condition variable is a synchronization object that allows threads to
 * suspend execution until some predicate on shared data is satisfied. The basic
 * operations on conditions are: signal the condition (when the predicate
 * becomes true), and wait for the condition, suspending the thread execution
 * until another thread signals the condition.
 *
 * A condition variable must always be associated with a mutex, to avoid the
 * race condition where a thread prepares to wait on a condition variable and
 * another thread signals the condition just before the first thread actually
 * waits on it.
 *
 * Before it can be used, a condition variable has to be initialized with
 * pthread_cond_init(). An attribute object, which reference may be passed to
 * this service, allows to select the features of the created condition
 * variable, namely the @a clock used by the pthread_cond_timedwait() service
 * (@a CLOCK_REALTIME is used by default), and whether it may be shared between
 * several processes (it may not be shared by default, see
 * pthread_condattr_setpshared()).
 *
 * Note that pthread_cond_init() should be used to initialize a condition
 * variable, using the static initializer @a PTHREAD_COND_INITIALIZER will
 * delay the initialization to the first method called on the condition
 * variable and will most likely introduce switches to secondary mode.
 * The documentation (and specifically api-tags) of the
 * condition variable services assumes the condition variable was explicitly
 * initialised with pthread_cond_init().
 *
 *@{
 */

static pthread_condattr_t cobalt_default_condattr;

static inline struct cobalt_cond_state *
get_cond_state(struct cobalt_cond_shadow *shadow)
{
	if (xnsynch_is_shared(shadow->handle))
		return cobalt_umm_shared + shadow->state_offset;

	return cobalt_umm_private + shadow->state_offset;
}

static inline struct cobalt_mutex_state *
get_mutex_state(struct cobalt_cond_shadow *shadow)
{
	struct cobalt_cond_state *cond_state = get_cond_state(shadow);

	if (cond_state->mutex_state_offset == ~0U)
		return NULL;

	if (xnsynch_is_shared(shadow->handle))
		return cobalt_umm_shared + cond_state->mutex_state_offset;

	return cobalt_umm_private + cond_state->mutex_state_offset;
}

void cobalt_default_condattr_init(void)
{
	pthread_condattr_init(&cobalt_default_condattr);
}

/**
 * @fn int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
 * @brief Initialize a condition variable
 *
 * This service initializes the condition variable @a cond, using the
 * condition variable attributes object @a attr. If @a attr is @a
 * NULL, default attributes are used (see pthread_condattr_init()).
 *
 * @param cond the condition variable to be initialized;
 *
 * @param attr the condition variable attributes object.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable attributes object @a attr is invalid or
 *   uninitialized;
 * - EBUSY, the condition variable @a cond was already initialized;
 * - ENOMEM, insufficient memory available from the system heap to initialize the
 *   condition variable, increase CONFIG_XENO_OPT_SYS_HEAPSZ.
 * - EAGAIN, no registry slot available, check/raise CONFIG_XENO_OPT_REGISTRY_NRSLOTS.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_init.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, pthread_cond_init, (pthread_cond_t *cond,
				     const pthread_condattr_t * attr))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_cond_state *cond_state;
	struct cobalt_condattr kcattr;
	int err, tmp;

	if (attr == NULL)
		attr = &cobalt_default_condattr;

	err = pthread_condattr_getpshared(attr, &tmp);
	if (err)
		return err;
	kcattr.pshared = tmp;

	err = pthread_condattr_getclock(attr, &tmp);
	if (err)
		return err;
	kcattr.clock = tmp;

	err = -XENOMAI_SYSCALL2( sc_cobalt_cond_init, _cnd, &kcattr);
	if (err)
		return err;

	cond_state = get_cond_state(_cnd);
	cobalt_commit_memory(cond_state);

	return 0;
}

static int __attribute__((cold))
	cobalt_cond_autoinit_type(const pthread_cond_t *cond)
{
	static const pthread_cond_t cond_initializer =
		PTHREAD_COND_INITIALIZER;

	return memcmp(cond, &cond_initializer, sizeof(cond_initializer)) == 0 ?
		0 : -1;
}

static int __attribute__((cold))
	cobalt_cond_doautoinit(union cobalt_cond_union *ucond)
{
	if (cobalt_cond_autoinit_type(&ucond->native_cond) < 0)
		return EINVAL;

	return __COBALT(pthread_cond_init(&ucond->native_cond, NULL));
}

static inline int cobalt_cond_autoinit(union cobalt_cond_union *ucond)
{
	if (ucond->shadow_cond.magic != COBALT_COND_MAGIC)
		return cobalt_cond_doautoinit(ucond);
	return 0;
}

/**
 * @fn int pthread_cond_destroy(pthread_cond_t *cond)
 * @brief Destroy a condition variable
 *
 * This service destroys the condition variable @a cond, if no thread is
 * currently blocked on it. The condition variable becomes invalid for all
 * condition variable services (they all return the EINVAL error) except
 * pthread_cond_init().
 *
 * @param cond the condition variable to be destroyed.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable @a cond is invalid;
 * - EPERM, the condition variable is not process-shared and does not belong to
 *   the current process;
 * - EBUSY, some thread is currently using the condition variable.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_destroy.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, pthread_cond_destroy, (pthread_cond_t *cond))
{
	struct cobalt_cond_shadow *_cond =
		&((union cobalt_cond_union *)cond)->shadow_cond;

	if (_cond->magic != COBALT_COND_MAGIC)
		return (cobalt_cond_autoinit_type(cond) < 0) ? EINVAL : 0;

	return -XENOMAI_SYSCALL1( sc_cobalt_cond_destroy, _cond);
}

struct cobalt_cond_cleanup_t {
	struct cobalt_cond_shadow *cond;
	struct cobalt_mutex_shadow *mutex;
	unsigned count;
	int err;
};

static void __pthread_cond_cleanup(void *data)
{
	struct cobalt_cond_cleanup_t *c = (struct cobalt_cond_cleanup_t *)data;
	xnhandle_t cur = cobalt_get_current();
	int err;

	/* if we still own the mutex, cond_wait_prologue wasn't called yet */
	if (xnsynch_fast_owner_check(mutex_get_ownerp(c->mutex), cur) == 0)
		return;

	do {
		err = XENOMAI_SYSCALL2(sc_cobalt_cond_wait_epilogue,
				       c->cond, c->mutex);
	} while (err == -EINTR);

	c->mutex->lockcnt = c->count;
}

static inline int do_sc_cond_wait_prologue(struct cobalt_cond_shadow *cnd,
					   struct cobalt_mutex_shadow *mx,
					   int *err, int timed,
					   const struct timespec *abstime)
{
#ifdef __USE_TIME_BITS64
	long sc_nr = sc_cobalt_cond_wait_prologue64;
#else
	long sc_nr = sc_cobalt_cond_wait_prologue;
#endif

	return XENOMAI_SYSCALL5(sc_nr, cnd, mx, err, timed, abstime);
}

/**
 * Wait on a condition variable.
 *
 * This service atomically unlocks the mutex @a mx, and block the calling thread
 * until the condition variable @a cnd is signalled using pthread_cond_signal()
 * or pthread_cond_broadcast(). When the condition is signaled, this service
 * re-acquire the mutex before returning.
 *
 * Spurious wakeups occur if a signal is delivered to the blocked thread, so, an
 * application should not assume that the condition changed upon successful
 * return from this service.
 *
 * Even if the mutex @a mx is recursive and its recursion count is greater than
 * one on entry, it is unlocked before blocking the caller, and the recursion
 * count is restored once the mutex is re-acquired by this service before
 * returning.
 *
 * Once a thread is blocked on a condition variable, a dynamic binding is formed
 * between the condition vairable @a cnd and the mutex @a mx; if another thread
 * calls this service specifying @a cnd as a condition variable but another
 * mutex than @a mx, this service returns immediately with the EINVAL status.
 *
 * This service is a cancellation point for Cobalt threads (created
 * with the pthread_create() service). When such a thread is cancelled
 * while blocked in a call to this service, the mutex @a mx is
 * re-acquired before the cancellation cleanup handlers are called.
 *
 * @param cond the condition variable to wait for;
 *
 * @param mutex the mutex associated with @a cnd.
 *
 * @return 0 on success,
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the specified condition variable or mutex is invalid;
 * - EPERM, the specified condition variable is not process-shared and does not
 *   belong to the current process;
 * - EINVAL, another thread is currently blocked on @a cnd using another mutex
 *   than @a mx;
 * - EPERM, the specified mutex is not owned by the caller.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_wait.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(int, pthread_cond_wait, (pthread_cond_t *cond, pthread_mutex_t *mutex))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_mutex_shadow *_mx =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	struct cobalt_cond_cleanup_t c = {
		.cond = _cnd,
		.mutex = _mx,
		.err = 0,
	};
	int err, oldtype;

	if (_mx->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	err = cobalt_cond_autoinit((union cobalt_cond_union *)cond);
	if (err)
		return err;

	if (_mx->attr.type == PTHREAD_MUTEX_ERRORCHECK) {
		xnhandle_t cur = cobalt_get_current();

		if (cur == XN_NO_HANDLE)
			return EPERM;

		if (xnsynch_fast_owner_check(mutex_get_ownerp(_mx), cur))
			return EPERM;
	}

	c.count = _mx->lockcnt;

	pthread_cleanup_push(&__pthread_cond_cleanup, &c);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = do_sc_cond_wait_prologue(_cnd, _mx, &c.err, 0, NULL);

	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR) {
		err = XENOMAI_SYSCALL2(sc_cobalt_cond_wait_epilogue, _cnd, _mx);
		pthread_testcancel();
	}

	_mx->lockcnt = c.count;

	return -err ?: -c.err;
}

/**
 * Wait a bounded time on a condition variable.
 *
 * This service is equivalent to pthread_cond_wait(), except that the calling
 * thread remains blocked on the condition variable @a cnd only until the
 * timeout specified by @a abstime expires.
 *
 * The timeout @a abstime is expressed as an absolute value of the @a clock
 * attribute passed to pthread_cond_init(). By default, @a CLOCK_REALTIME is
 * used.
 *
 * @param cond the condition variable to wait for;
 *
 * @param mutex the mutex associated with @a cnd;
 *
 * @param abstime the timeout, expressed as an absolute value of the clock
 * attribute passed to pthread_cond_init().
 *
 * @return 0 on success,
 * @return an error number if:
 * - EPERM, the caller context is invalid;
 * - EPERM, the specified condition variable is not process-shared and does not
 *   belong to the current process;
 * - EINVAL, the specified condition variable, mutex or timeout is invalid;
 * - EINVAL, another thread is currently blocked on @a cnd using another mutex
 *   than @a mx;
 * - EPERM, the specified mutex is not owned by the caller;
 * - ETIMEDOUT, the specified timeout expired.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_timedwait.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL_TIME64(int, pthread_cond_timedwait, __pthread_cond_timedwait64,
		   (pthread_cond_t *cond, pthread_mutex_t *mutex,
		    const struct timespec *abstime))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_mutex_shadow *_mx =
		&((union cobalt_mutex_union *)mutex)->shadow_mutex;
	struct cobalt_cond_cleanup_t c = {
		.cond = _cnd,
		.mutex = _mx,
	};
	int err, oldtype;

	if (_mx->magic != COBALT_MUTEX_MAGIC)
		return EINVAL;

	err = cobalt_cond_autoinit((union cobalt_cond_union *)cond);
	if (err)
		return err;

	if (_mx->attr.type == PTHREAD_MUTEX_ERRORCHECK) {
		xnhandle_t cur = cobalt_get_current();

		if (cur == XN_NO_HANDLE)
			return EPERM;

		if (xnsynch_fast_owner_check(mutex_get_ownerp(_mx), cur))
			return EPERM;
	}

	c.count = _mx->lockcnt;

	pthread_cleanup_push(&__pthread_cond_cleanup, &c);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = do_sc_cond_wait_prologue(_cnd, _mx, &c.err, 1, abstime);

	pthread_setcanceltype(oldtype, NULL);

	pthread_cleanup_pop(0);

	while (err == -EINTR) {
		err = XENOMAI_SYSCALL2(sc_cobalt_cond_wait_epilogue, _cnd, _mx);
		pthread_testcancel();
	}

	_mx->lockcnt = c.count;

	return -err ?: -c.err;
}

/**
 * Signal a condition variable.
 *
 * This service unblocks one thread blocked on the condition variable @a cnd.
 *
 * If more than one thread is blocked on the specified condition variable, the
 * highest priority thread is unblocked.
 *
 * @param cond the condition variable to be signalled.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable is invalid;
 * - EPERM, the condition variable is not process-shared and does not belong to
 *   the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_signal.html.">
 * Specification.</a>
 *
  * @apitags{xthread-only}
*/
COBALT_IMPL(int, pthread_cond_signal, (pthread_cond_t *cond))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_mutex_state *mutex_state;
	struct cobalt_cond_state *cond_state;
	__u32 pending_signals;
	xnhandle_t cur;
	__u32 flags;
	int err;

	err = cobalt_cond_autoinit((union cobalt_cond_union *)cond);
	if (err)
		return err;

	mutex_state = get_mutex_state(_cnd);
	if (mutex_state == NULL)
		return 0;	/* Fast path, no waiter. */

	flags = mutex_state->flags;
	if (flags & COBALT_MUTEX_ERRORCHECK) {
		cur = cobalt_get_current();
		if (cur == XN_NO_HANDLE)
			return EPERM;
		if (xnsynch_fast_owner_check(&mutex_state->owner, cur) < 0)
			return EPERM;
	}

	mutex_state->flags = flags | COBALT_MUTEX_COND_SIGNAL;
	cond_state = get_cond_state(_cnd);
	pending_signals = cond_state->pending_signals;
	if (pending_signals != ~0U)
		cond_state->pending_signals = pending_signals + 1;

	return 0;
}

/**
 * Broadcast a condition variable.
 *
 * This service unblocks all threads blocked on the condition variable @a cnd.
 *
 * @param cond the condition variable to be signalled.
 *
 * @return 0 on succes,
 * @return an error number if:
 * - EINVAL, the condition variable is invalid;
 * - EPERM, the condition variable is not process-shared and does not belong to
 *   the current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_cond_broadcast.html">
 * Specification.</a>
 *
  * @apitags{xthread-only}
 */
COBALT_IMPL(int, pthread_cond_broadcast, (pthread_cond_t *cond))
{
	struct cobalt_cond_shadow *_cnd = &((union cobalt_cond_union *)cond)->shadow_cond;
	struct cobalt_mutex_state *mutex_state;
	struct cobalt_cond_state *cond_state;
	xnhandle_t cur;
	__u32 flags;
	int err;

	err = cobalt_cond_autoinit((union cobalt_cond_union *)cond);
	if (err)
		return err;

	mutex_state = get_mutex_state(_cnd);
	if (mutex_state == NULL)
		return 0;

	flags = mutex_state->flags;
	if (flags & COBALT_MUTEX_ERRORCHECK) {
		cur = cobalt_get_current();
		if (cur == XN_NO_HANDLE)
			return EPERM;
		if (xnsynch_fast_owner_check(&mutex_state->owner, cur) < 0)
			return EPERM;
	}

	mutex_state->flags = flags | COBALT_MUTEX_COND_SIGNAL;
	cond_state = get_cond_state(_cnd);
	cond_state->pending_signals = ~0U;

	return 0;
}

/**
 * Initialize a condition variable attributes object.
 *
 * This services initializes the condition variable attributes object @a attr
 * with default values for all attributes. Default value are:
 * - for the @a clock attribute, @a CLOCK_REALTIME;
 * - for the @a pshared attribute @a PTHREAD_PROCESS_PRIVATE.
 *
 * If this service is called specifying a condition variable attributes object
 * that was already initialized, the attributes object is reinitialized.
 *
 * @param attr the condition variable attributes object to be initialized.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ENOMEM, the condition variable attribute object pointer @a attr is @a
 *   NULL.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_condattr_init.html">
 * Specification.</a>
 *
  * @apitags{thread-unrestricted}
 */
int pthread_condattr_init(pthread_condattr_t * attr);

/**
 * Destroy a condition variable attributes object.
 *
 * This service destroys the condition variable attributes object @a attr. The
 * object becomes invalid for all condition variable services (they all return
 * EINVAL) except pthread_condattr_init().
 *
 * @param attr the initialized mutex attributes object to be destroyed.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, the mutex attributes object @a attr is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_condattr_destroy.html">
 * Specification.</a>
 *
  * @apitags{thread-unrestricted}
 */
int pthread_condattr_destroy(pthread_condattr_t * attr);

/**
 * Get the clock selection attribute from a condition variable attributes
 * object.
 *
 * This service stores, at the address @a clk_id, the value of the @a clock
 * attribute in the condition variable attributes object @a attr.
 *
 * See pthread_cond_timedwait() for a description of the effect of
 * this attribute on a condition variable. The clock ID returned is @a
 * CLOCK_REALTIME or @a CLOCK_MONOTONIC.
 *
 * @param attr an initialized condition variable attributes object,
 *
 * @param clk_id address where the @a clock attribute value will be stored on
 * success.
 *
 * @return 0 on success,
 * @return an error number if:
 * - EINVAL, the attribute object @a attr is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_condattr_getclock.html">
 * Specification.</a>
 *
  * @apitags{thread-unrestricted}
 */
int pthread_condattr_getclock(const pthread_condattr_t * attr,
			clockid_t * clk_id);

/**
 * Set the clock selection attribute of a condition variable attributes object.
 *
 * This service set the @a clock attribute of the condition variable attributes
 * object @a attr.
 *
 * See pthread_cond_timedwait() for a description of the effect of
 * this attribute on a condition variable.
 *
 * @param attr an initialized condition variable attributes object,
 *
 * @param clk_id value of the @a clock attribute, may be @a CLOCK_REALTIME or @a
 * CLOCK_MONOTONIC.
 *
 * @return 0 on success,
 * @return an error number if:
 * - EINVAL, the condition variable attributes object @a attr is invalid;
 * - EINVAL, the value of @a clk_id is invalid for the @a clock attribute.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_condattr_setclock.html">
 * Specification.</a>
 *
  * @apitags{thread-unrestricted}
 */
int pthread_condattr_setclock(pthread_condattr_t * attr, clockid_t clk_id);

/**
 * Get the process-shared attribute from a condition variable attributes
 * object.
 *
 * This service stores, at the address @a pshared, the value of the @a pshared
 * attribute in the condition variable attributes object @a attr.
 *
 * The @a pshared attribute may only be one of @a PTHREAD_PROCESS_PRIVATE or @a
 * PTHREAD_PROCESS_SHARED. See pthread_condattr_setpshared() for the meaning of
 * these two constants.
 *
 * @param attr an initialized condition variable attributes object.
 *
 * @param pshared address where the value of the @a pshared attribute will be
 * stored on success.
 *
 * @return 0 on success,
 * @return an error number if:
 * - EINVAL, the @a pshared address is invalid;
 * - EINVAL, the condition variable attributes object @a attr is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_condattr_getpshared.html">
 * Specification.</a>
 *
  * @apitags{thread-unrestricted}
 */
int pthread_condattr_getpshared(const pthread_condattr_t *attr, int *pshared);

/**
 * Set the process-shared attribute of a condition variable attributes object.
 *
 * This service set the @a pshared attribute of the condition variable
 * attributes object @a attr.
 *
 * @param attr an initialized condition variable attributes object.
 *
 * @param pshared value of the @a pshared attribute, may be one of:
 * - PTHREAD_PROCESS_PRIVATE, meaning that a condition variable created with the
 *   attributes object @a attr will only be accessible by threads within the
 *   same process as the thread that initialized the condition variable;
 * - PTHREAD_PROCESS_SHARED, meaning that a condition variable created with the
 *   attributes object @a attr will be accessible by any thread that has access
 *   to the memory where the condition variable is allocated.
 *
 * @return 0 on success,
 * @return an error status if:
 * - EINVAL, the condition variable attributes object @a attr is invalid;
 * - EINVAL, the value of @a pshared is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_condattr_setpshared.html">
 * Specification.</a>
 *
  * @apitags{thread-unrestricted}
 */
int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared);

/** @}*/

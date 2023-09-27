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

#include <stdlib.h>		/* For malloc & free. */
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>		/* For O_CREAT. */
#include <pthread.h>		/* For pthread_setcanceltype. */
#include <semaphore.h>
#include <asm/xenomai/syscall.h>
#include <cobalt/uapi/sem.h>
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_sem Semaphores
 *
 * Cobalt/POSIX semaphore services
 *
 * Semaphores are counters for resources shared between threads. The basic
 * operations on semaphores are: increment the counter atomically, and wait
 * until the counter is non-null and decrement it atomically.
 *
 * Semaphores have a maximum value past which they cannot be incremented.  The
 * macro @a SEM_VALUE_MAX is defined to be this maximum value.
 *
 *@{
 */

static inline
struct cobalt_sem_state *sem_get_state(struct cobalt_sem_shadow *shadow)
{
	unsigned int pshared = shadow->state_offset < 0;

	if (pshared)
		return cobalt_umm_shared - shadow->state_offset;

	return cobalt_umm_private + shadow->state_offset;
}

/**
 * Initialize an unnamed semaphore.
 *
 * This service initializes the semaphore @a sm, with the value @a value.
 *
 * This service fails if @a sm is already initialized or is a named semaphore.
 *
 * @param sem the semaphore to be initialized;
 *
 * @param pshared if zero, means that the new semaphore may only be used by
 * threads in the same process as the thread calling sem_init(); if non zero,
 * means that the new semaphore may be used by any thread that has access to the
 * memory where the semaphore is allocated.
 *
 * @param value the semaphore initial value.
 *
 * @retval 0 on success,
 * @retval -1 with @a errno set if:
 * - EBUSY, the semaphore @a sm was already initialized;
 * - EAGAIN, insufficient memory available to initialize the
 *   semaphore, increase CONFIG_XENO_OPT_SHARED_HEAPSZ for a process-shared
 *   semaphore, or CONFIG_XENO_OPT_PRIVATE_HEAPSZ for a process-private semaphore.
 * - EAGAIN, no registry slot available, check/raise CONFIG_XENO_OPT_REGISTRY_NRSLOTS.
 * - EINVAL, the @a value argument exceeds @a SEM_VALUE_MAX.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_init.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, sem_init, (sem_t *sem, int pshared, unsigned int value))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	int ret;

	ret = XENOMAI_SYSCALL3(sc_cobalt_sem_init,
			       _sem, pshared ? SEM_PSHARED : 0, value);
	if (ret) {
		errno = -ret;
		return -1;
	}

	cobalt_commit_memory(sem_get_state(_sem));

	return 0;
}

/**
 * @fn int sem_destroy(sem_t *sem)
 * @brief Destroy an unnamed semaphore
 *
 * This service destroys the semaphore @a sem. Threads currently
 * blocked on @a sem are unblocked and the service they called return
 * -1 with @a errno set to EINVAL. The semaphore is then considered
 * invalid by all semaphore services (they all fail with @a errno set
 * to EINVAL) except sem_init().
 *
 * This service fails if @a sem is a named semaphore.
 *
 * @param sem the semaphore to be destroyed.
 *
 * @retval always 0 on success.  If SEM_WARNDEL was mentioned in
 * sem_init_np(), the semaphore is deleted as requested and a strictly
 * positive value is returned to warn the caller if threads were
 * pending on it, otherwise zero is returned. If SEM_NOBUSYDEL was
 * mentioned in sem_init_np(), sem_destroy() may succeed only if no
 * thread is waiting on the semaphore to delete, otherwise -EBUSY is
 * returned.
 *
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore @a sem is invalid or a named semaphore;
 * - EPERM, the semaphore @a sem is not process-shared and does not belong to the
 *   current process.
 * - EBUSY, a thread is currently waiting on the semaphore @a sem with
 * SEM_NOBUSYDEL set.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_destroy.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, sem_destroy, (sem_t *sem))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	int ret;

	if (_sem->magic != COBALT_SEM_MAGIC
	    && _sem->magic != COBALT_NAMED_SEM_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	ret = XENOMAI_SYSCALL1(sc_cobalt_sem_destroy, _sem);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}

/**
 * @fn int sem_post(sem_t *sem)
 * @brief Post a semaphore
 *
 * This service posts the semaphore @a sem.
 *
 * If no thread is currently blocked on this semaphore, its count is
 * incremented unless "pulse" mode is enabled for it (see
 * sem_init_np(), SEM_PULSE). If a thread is blocked on the semaphore,
 * the thread heading the wait queue is unblocked.
 *
 * @param sem the semaphore to be signaled.
 *
 * @retval 0 on success;
 * @retval -1 with errno set if:
 * - EINVAL, the specified semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process;
 * - EAGAIN, the semaphore count is @a SEM_VALUE_MAX.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_post.html">
 * Specification.</a>
 *
 * @apitags{unrestricted}
 */
COBALT_IMPL(int, sem_post, (sem_t *sem))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	struct cobalt_sem_state *state;
	int value, ret, old, new;

	if (_sem->magic != COBALT_SEM_MAGIC
	    && _sem->magic != COBALT_NAMED_SEM_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	state = sem_get_state(_sem);
	smp_mb();
	value = atomic_read(&state->value);
	if (value >= 0) {
		if (state->flags & SEM_PULSE)
			return 0;
		do {
			old = value;
			new = value + 1;
			value = atomic_cmpxchg(&state->value, old, new);
			if (value < 0)
				goto do_syscall;
		} while (value != old);

		return 0;
	}

  do_syscall:
	ret = XENOMAI_SYSCALL1(sc_cobalt_sem_post, _sem);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

/**
 * @fn int sem_trywait(sem_t *sem)
 * @brief Attempt to decrement a semaphore
 *
 * This service is equivalent to sem_wait(), except that it returns
 * immediately if the semaphore @a sem is currently depleted, and that
 * it is not a cancellation point.
 *
 * @param sem the semaphore to be decremented.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the specified semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sem is not process-shared and does not belong to the
 *   current process;
 * - EAGAIN, the specified semaphore is currently fully depleted.
 *
 * * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_trywait.html">
 * Specification.</a>
 *
 * @apitags{xthread-only}
 */
COBALT_IMPL(int, sem_trywait, (sem_t *sem))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	struct cobalt_sem_state *state;
	int value, old, new;

	if (_sem->magic != COBALT_SEM_MAGIC
	    && _sem->magic != COBALT_NAMED_SEM_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	state = sem_get_state(_sem);
	smp_mb();
	value = atomic_read(&state->value);
	if (value > 0) {
		do {
			old = value;
			new = value - 1;
			value = atomic_cmpxchg(&state->value, old, new);
			if (value <= 0)
				goto eagain;
		} while (value != old);

		return 0;
	}
eagain:
	errno = EAGAIN;

	return -1;
}

/**
 * @fn int sem_wait(sem_t *sem)
 * @brief Decrement a semaphore
 *
 * This service decrements the semaphore @a sem if it is currently if
 * its value is greater than 0. If the semaphore's value is currently
 * zero, the calling thread is suspended until the semaphore is
 * posted, or a signal is delivered to the calling thread.
 *
 * This service is a cancellation point for Cobalt threads (created
 * with the pthread_create() service). When such a thread is cancelled
 * while blocked in a call to this service, the semaphore state is
 * left unchanged before the cancellation cleanup handlers are called.
 *
 * @param sem the semaphore to be decremented.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sem is not process-shared and does not belong to the
 *   current process;
 * - EINTR, the caller was interrupted by a signal while blocked in this
 *   service.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_wait.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(int, sem_wait, (sem_t *sem))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	int ret, oldtype;

	ret = __RT(sem_trywait(sem));
	if (ret != -1 || errno != EAGAIN)
		return ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL1(sc_cobalt_sem_wait, _sem);

	pthread_setcanceltype(oldtype, NULL);

	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

/**
 * @fn int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
 * @brief Attempt to decrement a semaphore with a time limit
 *
 * This service is equivalent to sem_wait(), except that the caller is only
 * blocked until the timeout @a abs_timeout expires.
 *
 * @param sem the semaphore to be decremented;
 *
 * @param abs_timeout the timeout, expressed as an absolute value of
 * the relevant clock for the semaphore, either CLOCK_MONOTONIC if
 * SEM_RAWCLOCK was mentioned via sem_init_np(), or CLOCK_REALTIME
 * otherwise.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - EINVAL, the semaphore is invalid or uninitialized;
 * - EINVAL, the specified timeout is invalid;
 * - EPERM, the semaphore @a sm is not process-shared and does not belong to the
 *   current process;
 * - EINTR, the caller was interrupted by a signal while blocked in this
 *   service;
 * - ETIMEDOUT, the semaphore could not be decremented and the
 *   specified timeout expired.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_timedwait.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL_TIME64(int, sem_timedwait, __sem_timedwait64,
		   (sem_t *sem, const struct timespec *abs_timeout))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	int ret, oldtype;

	ret = __RT(sem_trywait(sem));
	if (ret != -1 || errno != EAGAIN)
		return ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

#ifdef __USE_TIME_BITS64
	ret = XENOMAI_SYSCALL2(sc_cobalt_sem_timedwait64, _sem,
			       abs_timeout);
#else
	ret = XENOMAI_SYSCALL2(sc_cobalt_sem_timedwait, _sem, abs_timeout);
#endif

	pthread_setcanceltype(oldtype, NULL);

	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

/**
 * @fn int sem_getvalue(sem_t sem, int *sval_r)
 * @brief Get the value of a semaphore.
 *
 * This service stores at the address @a value, the current count of
 * the semaphore @a sem. The state of the semaphore is unchanged.
 *
 * If the semaphore is currently fully depleted, the value stored is
 * zero, unless SEM_REPORT was mentioned for a non-standard semaphore
 * (see sem_init_np()), in which case the current number of waiters is
 * returned as the semaphore's negative value (e.g. -2 would mean the
 * semaphore is fully depleted AND two threads are currently pending
 * on it).
 *
 * @param sem a semaphore;
 *
 * @param sval_r address where the semaphore count will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore is invalid or uninitialized;
 * - EPERM, the semaphore @a sem is not process-shared and does not belong to the
 *   current process.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_getvalue.html">
 * Specification.</a>
 *
 * @apitags{unrestricted}
 */
COBALT_IMPL(int, sem_getvalue, (sem_t *sem, int *sval))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	struct cobalt_sem_state *state;
	int value;

	if (_sem->magic != COBALT_SEM_MAGIC
	    && _sem->magic != COBALT_NAMED_SEM_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	state = sem_get_state(_sem);
	smp_mb();
	value = atomic_read(&state->value);
	if (value < 0 && (state->flags & SEM_REPORT) == 0)
		value = 0;

	*sval = value;

	return 0;
}

/**
 * @fn int sem_open(const char *name, int oflags, mode_t mode, unsigned int value)
 * @brief Open a named semaphore
 *
 * This opens the semaphore named @a name.
 *
 * If no semaphore named @a name exists and @a oflags has the @a O_CREAT bit
 * set, the semaphore is created by this function, using two more arguments:
 * - a @a mode argument, of type @b mode_t, currently ignored;
 * - a @a value argument, of type @b unsigned, specifying the initial value of
 *   the created semaphore.
 *
 * If @a oflags has the two bits @a O_CREAT and @a O_EXCL set and the semaphore
 * already exists, this service fails.
 *
 * @a name may be any arbitrary string, in which slashes have no particular
 * meaning. However, for portability, using a name which starts with a slash and
 * contains no other slash is recommended.
 *
 * If sem_open() is called from the same process several times for the
 * same @a name, the same address is returned.
 *
 * @param name the name of the semaphore to be created;
 *
 * @param oflags flags.
 *
 * @return the address of the named semaphore on success;
 * @return SEM_FAILED with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - EEXIST, the bits @a O_CREAT and @a O_EXCL were set in @a oflags and the
 *   named semaphore already exists;
 * - ENOENT, the bit @a O_CREAT is not set in @a oflags and the named semaphore
 *   does not exist;
 * - ENOMEM, not enough memory to create the semaphore. A usual
 *   suspect is a shortage from system heap memory, which may be
 *   fixed by increasing CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EINVAL, the @a value argument exceeds @a SEM_VALUE_MAX.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_open.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(sem_t *, sem_open, (const char *name, int oflags, ...))
{
	union cobalt_sem_union *sem, *rsem;
	unsigned value = 0;
	mode_t mode = 0;
	va_list ap;
	int err;

	if (oflags & O_CREAT) {
		va_start(ap, oflags);
		mode = va_arg(ap, int);
		value = va_arg(ap, unsigned);
		va_end(ap);
	}

	rsem = sem = malloc(sizeof(*sem));
	if (rsem == NULL) {
		err = -ENOMEM;
		goto error;
	}

	err = XENOMAI_SYSCALL5(sc_cobalt_sem_open,
			       &rsem, name, oflags, mode, value);
	if (err == 0) {
		if (rsem != sem)
			free(sem);
		return &rsem->native_sem;
	}

	free(sem);
error:
	errno = -err;

	return SEM_FAILED;
}

/**
 * @fn int sem_close(sem_t *sem)
 * @brief Close a named semaphore
 *
 * This service closes the semaphore @a sem. The semaphore is
 * destroyed only when unlinked with a call to the sem_unlink()
 * service and when each call to sem_open() matches a call to this
 * service.
 *
 * When a semaphore is destroyed, the memory it used is returned to the system
 * heap, so that further references to this semaphore are not guaranteed to
 * fail, as is the case for unnamed semaphores.
 *
 * This service fails if @a sem is an unnamed semaphore.
 *
 * @param sem the semaphore to be closed.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, the semaphore @a sem is invalid or is an unnamed semaphore.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_close.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(int, sem_close, (sem_t *sem))
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	int ret;

	if (_sem->magic != COBALT_NAMED_SEM_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	ret = XENOMAI_SYSCALL1(sc_cobalt_sem_close, _sem);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	if (ret)
		free(sem);

	return 0;
}

/**
 * @fn int sem_unlink(const char *name)
 * @brief Unlink a named semaphore
 *
 * This service unlinks the semaphore named @a name. This semaphore is not
 * destroyed until all references obtained with sem_open() are closed by calling
 * sem_close(). However, the unlinked semaphore may no longer be reached with
 * the sem_open() service.
 *
 * When a semaphore is destroyed, the memory it used is returned to the system
 * heap, so that further references to this semaphore are not guaranteed to
 * fail, as is the case for unnamed semaphores.
 *
 * @param name the name of the semaphore to be unlinked.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - ENOENT, the named semaphore does not exist.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sem_unlink.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(int, sem_unlink, (const char *name))
{
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sem_unlink, name);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

int sem_init_np(sem_t *sem, int flags, unsigned int value)
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	int ret;

	ret = XENOMAI_SYSCALL3(sc_cobalt_sem_init, _sem, flags, value);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

int sem_broadcast_np(sem_t *sem)
{
	struct cobalt_sem_shadow *_sem = &((union cobalt_sem_union *)sem)->shadow_sem;
	struct cobalt_sem_state *state;
	int value, ret;

	if (_sem->magic != COBALT_SEM_MAGIC
	    && _sem->magic != COBALT_NAMED_SEM_MAGIC) {
		errno = EINVAL;
		return -1;
	}

	state = sem_get_state(_sem);
	smp_mb();
	value = atomic_read(&state->value);
	if (value >= 0)
		return 0;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sem_broadcast_np, _sem);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

/** @} */

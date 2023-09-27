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

#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <semaphore.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_thread Thread management
 *
 * Cobalt (POSIX) thread management services
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_09.html#tag_02_09">
 * Specification.</a>
 *
 *@{
 */

static pthread_attr_ex_t default_attr_ex;

static int linuxthreads;

struct pthread_iargs {
	struct sched_param_ex param_ex;
	int policy;
	int personality;
	void *(*start)(void *);
	void *arg;
	int parent_prio;
	sem_t sync;
	int ret;
};

static void *cobalt_thread_trampoline(void *p)
{
	/*
	 * Volatile is to prevent (too) smart gcc releases from
	 * trashing the syscall registers (see later comment).
	 */
	int personality, parent_prio, policy, std_policy;
	volatile pthread_t ptid = pthread_self();
	void *(*start)(void *), *arg, *retval;
	struct pthread_iargs *iargs = p;
	struct sched_param_ex param_ex;
	struct sched_param std_param;
	__u32 u_winoff;
	long ret;

	cobalt_sigshadow_install_once();

	personality = iargs->personality;
	param_ex = iargs->param_ex;
	policy = iargs->policy;
	parent_prio = iargs->parent_prio;
	start = iargs->start;
	arg = iargs->arg;

	/*
	 * We don't have any Xenomai extension yet, set our base
	 * scheduling parameters for the host kernel first.
	 */
	std_policy = cobalt_xlate_schedparam(policy, &param_ex, &std_param);
	ret = __STD(pthread_setschedparam(ptid, std_policy, &std_param));
	if (ret)
		goto sync_with_creator;

	/*
	 * Do _not_ inline the call to pthread_self() in the syscall
	 * macro: this trashes the syscall regs on some archs.
	 */
	ret = -XENOMAI_SYSCALL5(sc_cobalt_thread_create, ptid,
				policy, &param_ex, personality, &u_winoff);
	if (ret == 0)
		cobalt_set_tsd(u_winoff);

	/*
	 * We must access anything we'll need from *iargs before
	 * posting the sync semaphore, since our released parent could
	 * unwind the stack space onto which the iargs struct is laid
	 * on before we actually get the CPU back.
	 */
sync_with_creator:
	iargs->ret = ret;
	__STD(sem_post(&iargs->sync));
	if (ret)
		return (void *)ret;

	/*
	 * If the parent thread runs with the same priority as we do,
	 * then we should yield the CPU to it, to preserve the
	 * scheduling order.
	 */
	if (param_ex.sched_priority == parent_prio)
		__STD(sched_yield());

	cobalt_thread_harden();

	retval = start(arg);

	pthread_setmode_np(PTHREAD_WARNSW, 0, NULL);

	return retval;
}

int pthread_create_ex(pthread_t *ptid_r,
		      const pthread_attr_ex_t *attr_ex,
		      void *(*start) (void *), void *arg)
{
	int inherit, detachstate, ret;
	struct pthread_iargs iargs;
	struct sched_param param;
	struct timespec timeout;
	pthread_attr_t attr;
	pthread_t lptid;

	if (attr_ex == NULL)
		attr_ex = &default_attr_ex;

	pthread_getschedparam_ex(pthread_self(), &iargs.policy, &iargs.param_ex);
	iargs.parent_prio = iargs.param_ex.sched_priority;
	memcpy(&attr, &attr_ex->std, sizeof(attr));

	pthread_attr_getinheritsched(&attr, &inherit);
	if (inherit == PTHREAD_EXPLICIT_SCHED) {
		pthread_attr_getschedpolicy_ex(attr_ex, &iargs.policy);
		pthread_attr_getschedparam_ex(attr_ex, &iargs.param_ex);
	}

	if (linuxthreads && geteuid()) {
		/*
		 * Work around linuxthreads shortcoming: it doesn't
		 * believe that it could have RT power as non-root and
		 * fails the thread creation overeagerly.
		 */
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		param.sched_priority = 0;
		pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
		pthread_attr_setschedparam(&attr, &param);
	} else
		/*
		 * Get the created thread to temporarily inherit the
		 * caller priority (we mean linux/libc priority here,
		 * as we use a libc call to create the thread).
		 */
		pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);

	pthread_attr_getdetachstate(&attr, &detachstate);
	pthread_attr_getpersonality_ex(attr_ex, &iargs.personality);

	/*
	 * First start a regular POSIX thread, then mate a Cobalt
	 * thread to it.
	 */
	iargs.start = start;
	iargs.arg = arg;
	iargs.ret = EAGAIN;
	__STD(sem_init(&iargs.sync, 0, 0));

	ret = __STD(pthread_create(&lptid, &attr, cobalt_thread_trampoline, &iargs));
	if (ret) {
		__STD(sem_destroy(&iargs.sync));
		return ret;
	}

	__STD(clock_gettime(CLOCK_REALTIME, &timeout));
	timeout.tv_sec += 5;
	timeout.tv_nsec = 0;

	for (;;) {
		ret = __STD(sem_timedwait(&iargs.sync, &timeout));
		if (ret && errno == EINTR)
			continue;
		if (ret == 0) {
			ret = iargs.ret;
			if (ret == 0)
				*ptid_r = lptid;
			break;
		} else if (errno == ETIMEDOUT) {
			ret = EAGAIN;
			break;
		}
		ret = -errno;
		panic("regular sem_wait() failed with %s", symerror(ret));
	}

	__STD(sem_destroy(&iargs.sync));

	cobalt_thread_harden(); /* May fail if regular thread. */

	return ret;
}

/**
 * @fn int pthread_create(pthread_t *ptid_r, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
 * @brief Create a new thread
 *
 * This service creates a thread managed by the Cobalt core in a dual
 * kernel configuration.
 *
 * Attributes of the new thread depend on the @a attr argument. If @a
 * attr is NULL, default values for these attributes are used.
 *
 * Returning from the @a start routine has the same effect as calling
 * pthread_exit() with the return value.
 *
 * @param ptid_r address where the identifier of the new thread will be stored on
 * success;
 *
 * @param attr thread attributes;
 *
 * @param start thread start routine;
 *
 * @param arg opaque user-supplied argument passed to @a start;
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a attr is invalid;
 * - EAGAIN, insufficient memory available from the system heap to create a new
 *   thread, increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EINVAL, thread attribute @a inheritsched is set to PTHREAD_INHERIT_SCHED
 *   and the calling thread does not belong to the Cobalt interface;
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_create.html">
 * Specification.</a>
 *
 * @note
 *
 * When creating a Cobalt thread for the first time, libcobalt
 * installs an internal handler for the SIGSHADOW signal. If you had
 * previously installed a handler for such signal before that point,
 * such handler will be exclusively called for any SIGSHADOW
 * occurrence Xenomai did not send.
 *
 * If, however, an application-defined handler for SIGSHADOW is
 * installed afterwards, overriding the libcobalt handler, the new
 * handler is required to call cobalt_sigshadow_handler() on
 * entry. This routine returns a non-zero value for every occurrence
 * of SIGSHADOW issued by the Cobalt core. If zero instead, the
 * application-defined handler should process the signal.
 *
 * <b>int cobalt_sigshadow_handler(int sig, siginfo_t *si, void *ctxt);</b>
 *
 * You should register your handler with sigaction(2), setting the
 * SA_SIGINFO flag.
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(int, pthread_create, (pthread_t *ptid_r,
				  const pthread_attr_t *attr,
				  void *(*start) (void *), void *arg))
{
	pthread_attr_ex_t attr_ex;
	struct sched_param param;
	int policy;

	if (attr == NULL)
		attr = &default_attr_ex.std;

	memcpy(&attr_ex.std, attr, sizeof(*attr));
	pthread_attr_getschedpolicy(attr, &policy);
	attr_ex.nonstd.sched_policy = policy;
	pthread_attr_getschedparam(attr, &param);
	attr_ex.nonstd.sched_param.sched_priority = param.sched_priority;
	attr_ex.nonstd.personality = 0; /* Default: use Cobalt. */

	return pthread_create_ex(ptid_r, &attr_ex, start, arg);
}

/**
 * Set the mode of the current thread.
 *
 * This service sets the mode of the calling thread, which affects its
 * behavior under particular circumstances. @a clrmask and @a setmask
 * are two masks of mode bits which are respectively cleared and set
 * by pthread_setmode_np():
 *
 * - PTHREAD_LOCK_SCHED, when set, locks the scheduler, which prevents
 *   the current thread from being switched out until the scheduler is
 *   unlocked. Unless PTHREAD_DISABLE_LOCKBREAK is also set, the
 *   thread may still block, dropping the lock temporarily, in which
 *   case, the lock will be reacquired automatically when the thread
 *   resumes execution. When PTHREAD_LOCK_SCHED is cleared, the
 *   current thread drops the scheduler lock, and the rescheduling
 *   procedure is initiated.
 *
 * - When set, PTHREAD_WARNSW enables debugging notifications for the
 *   current thread.  A SIGDEBUG (Linux-originated) signal is sent when
 *   the following atypical or abnormal behavior is detected:
 *
 *   - the current thread switches to secondary mode. Such
 *     notification comes in handy for detecting spurious relaxes,
 *     with one of the following reason codes:
 *
 *     - SIGDEBUG_MIGRATE_SYSCALL, if the thread issued a regular
 *       Linux system call.
 *
 *     - SIGDEBUG_MIGRATE_SIGNAL, if the thread had to leave real-time
 *       mode for handling a Linux signal.
 *
 *     - SIGDEBUG_MIGRATE_FAULT, if the thread had to leave real-time
 *       mode for handling a processor fault/exception.
 *
 *   - the current thread is sleeping on a Cobalt mutex currently
 *     owned by a thread running in secondary mode, which reveals a
 *     priority inversion. In such an event, the reason code passed to
 *     the signal handler will be SIGDEBUG_MIGRATE_PRIOINV.
 *
 *   - the current thread is about to sleep while holding a Cobalt
 *     mutex, and CONFIG_XENO_OPT_DEBUG_MUTEX_SLEEP is enabled in the
 *     kernel configuration.  In such an event, the reason code passed
 *     to the signal handler will be SIGDEBUG_MUTEX_SLEEP.  Blocking
 *     for acquiring a mutex does not trigger such signal though.
 *
 *   - the current thread has enabled PTHREAD_DISABLE_LOCKBREAK and
 *     PTHREAD_LOCK_SCHED, then attempts to block on a Cobalt service,
 *     which would cause a lock break. In such an event, the reason
 *     code passed to the signal handler will be SIGDEBUG_LOCK_BREAK.
 *
 * - PTHREAD_DISABLE_LOCKBREAK disallows breaking the scheduler
 *   lock. Normally, the scheduler lock is dropped implicitly when the
 *   current owner blocks, then reacquired automatically when the
 *   owner resumes execution. If PTHREAD_DISABLE_LOCKBREAK is set, the
 *   scheduler lock owner would return with EINTR immediately from any
 *   blocking call instead (see PTHREAD_WARNSW notifications).
 *
 * - PTHREAD_CONFORMING can be passed in @a setmask to switch the
 *   current Cobalt thread to its preferred runtime mode. The only
 *   meaningful use of this switch is to force a real-time thread back
 *   to primary mode eagerly. Other usages have no effect.
 *
 * This service is a non-portable extension of the Cobalt interface.
 *
 * @param clrmask set of bits to be cleared.
 *
 * @param setmask set of bits to be set.
 *
 * @param mode_r If non-NULL, @a mode_r must be a pointer to a memory
 * location which will be written upon success with the previous set
 * of active mode bits. If NULL, the previous set of active mode bits
 * will not be returned.
 *
 * @return 0 on success, otherwise:
 *
 * - EINVAL, some bit in @a clrmask or @a setmask is invalid.
 *
 * @note Setting @a clrmask and @a setmask to zero leads to a nop,
 * only returning the previous mode if @a mode_r is a valid address.
 *
 * @attention Issuing PTHREAD_CONFORMING is most likely useless or even
 * introduces pure overhead in regular applications, since the Cobalt
 * core performs the necessary mode switches, only when required.
 *
 * @apitags{xthread-only, switch-primary}
 */
int pthread_setmode_np(int clrmask, int setmask, int *mode_r)
{
	return -XENOMAI_SYSCALL3(sc_cobalt_thread_setmode,
				 clrmask, setmask, mode_r);
}

/**
 * Set a thread name.
 *
 * This service set to @a name, the name of @a thread. This name is used for
 * displaying information in /proc/xenomai/sched.
 *
 * This service is a non-portable extension of the Cobalt interface.
 *
 * @param thread target thread;
 *
 * @param name name of the thread.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 *
 * @apitags{xthread-only}
 */
COBALT_IMPL(int, pthread_setname_np, (pthread_t thread, const char *name))
{
	return -XENOMAI_SYSCALL2(sc_cobalt_thread_setname, thread, name);
}

/**
 * Send a signal to a thread.
 *
 * This service send the signal @a sig to the Cobalt thread @a thread
 * (created with pthread_create()). If @a sig is zero, this service
 * check for existence of the thread @a thread, but no signal is sent.
 *
 * @param thread thread identifier;
 *
 * @param sig signal number.
 *
 * @return 0 on success;
 * @return an error number if:
 * - EINVAL, @a sig is an invalid signal number;
 * - EAGAIN, the maximum number of pending signals has been exceeded;
 * - ESRCH, @a thread is an invalid thread identifier.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_kill.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-primary}
 */
COBALT_IMPL(int, pthread_kill, (pthread_t thread, int sig))
{
	int ret;

	ret = -XENOMAI_SYSCALL2(sc_cobalt_thread_kill, thread, sig);
	if (ret == ESRCH)
		return __STD(pthread_kill(thread, sig));

	return ret;
}

/**
 * Wait for termination of a specified thread.
 *
 * If @a thread is running and joinable, this service blocks the
 * caller until @a thread terminates or detaches.  When @a thread
 * terminates, the caller is unblocked and its return value is stored
 * at the address @a value_ptr.
 *
 * On the other hand, if @a thread has already finished execution, its
 * return value collected earlier is stored at the address @a
 * value_ptr and this service returns immediately.
 *
 * This service is a cancelation point for Cobalt threads: if the
 * calling thread is canceled while blocked in a call to this service,
 * the cancelation request is honored and @a thread remains joinable.
 *
 * Multiple simultaneous calls to pthread_join() specifying the same running
 * target thread block all the callers until the target thread terminates.
 *
 * @param thread identifier of the thread to wait for;
 *
 * @param retval address where the target thread return value will be stored
 * on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - EDEADLK, attempting to join the calling thread;
 * - EINVAL, @a thread is detached;
 * - EPERM, the caller context is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_join.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-secondary, switch-primary}
 */
COBALT_IMPL(int, pthread_join, (pthread_t thread, void **retval))
{
	int ret;

	ret = __STD(pthread_join(thread, retval));
	if (ret)
		return ret;

	ret = cobalt_thread_join(thread);

	return ret == -EBUSY ? EINVAL : 0;
}

/** @} */

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_sched Scheduling management
 *
 * Cobalt scheduling management services
 * @{
 */

/**
 * Set the scheduling policy and parameters of the specified thread.
 *
 * This service set the scheduling policy of the Cobalt thread
 * identified by @a pid to the value @a policy, and its scheduling
 * parameters (i.e. its priority) to the value pointed to by @a param.
 *
 * If pthread_self() is passed, this service turns the current thread
 * into a Cobalt thread. If @a thread is not the identifier of a
 * Cobalt thread, this service falls back to the regular
 * pthread_setschedparam() service.
 *
 * @param thread target Cobalt thread;
 *
 * @param policy scheduling policy, one of SCHED_FIFO, SCHED_RR, or
 * SCHED_OTHER;
 *
 * @param param address of scheduling parameters.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a pid is invalid;
 * - EINVAL, @a policy or @a param->sched_priority is invalid;
 * - EAGAIN, insufficient memory available from the system heap,
 *   increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EFAULT, @a param is an invalid address;
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_setschedparam.html">
 * Specification.</a>
 *
 * @note
 *
 * See pthread_create(), pthread_setschedparam_ex().
 *
 * @apitags{thread-unrestricted, switch-secondary, switch-primary}
 */
COBALT_IMPL(int, pthread_setschedparam, (pthread_t thread,
					 int policy, const struct sched_param *param))
{
	/*
	 * XXX: We currently assume that all available policies
	 * supported by the host kernel define a single scheduling
	 * parameter only, i.e. a priority level.
	 */
	struct sched_param_ex param_ex = {
		.sched_priority = param->sched_priority,
	};

	return pthread_setschedparam_ex(thread, policy, &param_ex);
}

/**
 * Set extended scheduling policy of thread
 *
 * This service is an extended version of the regular
 * pthread_setschedparam() service, which supports Cobalt-specific
 * scheduling policies, not available with the host Linux environment.
 *
 * This service set the scheduling policy of the Cobalt thread @a
 * thread to the value @a policy, and its scheduling parameters
 * (e.g. its priority) to the value pointed to by @a param_ex.
 *
 * If @a thread does not match the identifier of a Cobalt thread, this
 * action falls back to the regular pthread_setschedparam() service.
 *
 * @param thread target Cobalt thread;
 *
 * @param policy scheduling policy, one of SCHED_WEAK, SCHED_FIFO,
 * SCHED_COBALT, SCHED_RR, SCHED_SPORADIC, SCHED_TP, SCHED_QUOTA or
 * SCHED_NORMAL;
 *
 * @param param_ex scheduling parameters address. As a special
 * exception, a negative sched_priority value is interpreted as if
 * SCHED_WEAK was given in @a policy, using the absolute value of this
 * parameter as the weak priority level.
 *
 * When CONFIG_XENO_OPT_SCHED_WEAK is enabled, SCHED_WEAK exhibits
 * priority levels in the [0..99] range (inclusive). Otherwise,
 * sched_priority must be zero for the SCHED_WEAK policy.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid;
 * - EINVAL, @a policy or @a param_ex->sched_priority is invalid;
 * - EAGAIN, insufficient memory available from the system heap,
 *   increase CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EFAULT, @a param_ex is an invalid address;
 * - EPERM, the calling process does not have superuser
 *   permissions.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_setschedparam.html">
 * Specification.</a>
 *
 * @note
 *
 * See pthread_create(), pthread_setschedparam().
 *
 * @apitags{thread-unrestricted, switch-secondary, switch-primary}
 */
int pthread_setschedparam_ex(pthread_t thread,
			     int policy, const struct sched_param_ex *param_ex)
{
	int ret, promoted, std_policy;
	struct sched_param std_param;
	__u32 u_winoff, *u_winoff_ptr;

	/*
	 * If we enter this call over a relaxed context, take the
	 * opportunity to tell the host kernel via the regular libc
	 * about the new schedparams right now, then tell Xenomai
	 * next.  Otherwise, send the request to Xenomai only, which
	 * will propagate the change to the host kernel asap,
	 * i.e. almost immediately if the target thread is already
	 * relaxed, or when it relaxes otherwise.
	 *
	 * CAUTION: when lazy propagation has to take place
	 * (i.e. calling pthread_setschedparam_ex() from primary
	 * mode), glibc's cached idea of the current schedparams of
	 * the target thread will be out of sync. This is part of the
	 * trade-off to keep the caller running in primary mode
	 * throughout this service.
	 *
	 * Application which are sensitive to this issue with Xenomai
	 * threads should refrain from mixing APIs for managing
	 * scheduling parameters, and only rely on libcobalt for this.
	 */
	if (cobalt_eager_setsched()) {
		/* True if disabled or shadow not mapped yet. */
		std_policy = cobalt_xlate_schedparam(policy, param_ex, &std_param);
		ret = __STD(pthread_setschedparam(thread, std_policy, &std_param));
		if (ret)
			return ret;
	}

	/* only request promotion when this targets the current thread */
	u_winoff_ptr = thread == pthread_self() ? &u_winoff : NULL;

	ret = -XENOMAI_SYSCALL5(sc_cobalt_thread_setschedparam_ex,
				thread, policy, param_ex,
				u_winoff_ptr, &promoted);

	/*
	 * If the kernel has no reference to the target thread. let glibc
	 * handle the call.
	 */
	if (ret == ESRCH) {
		std_policy = cobalt_xlate_schedparam(policy, param_ex,
						     &std_param);
		return __STD(pthread_setschedparam(thread, std_policy,
						   &std_param));
	}

	if (ret == 0 && promoted) {
		cobalt_sigshadow_install_once();
		cobalt_set_tsd(u_winoff);
		cobalt_thread_harden();
	}

	return ret;
}

COBALT_IMPL(int, pthread_setschedprio, (pthread_t thread, int prio))
{
	__u32 u_winoff, *u_winoff_ptr;
	int ret, promoted;

	/* See pthread_setschedparam_ex. */
	if (cobalt_eager_setsched()) {
		ret = __STD(pthread_setschedprio(thread, prio));
		if (ret)
			return ret;
	}

	/* only request promotion when this targets the current thread */
	u_winoff_ptr = thread == pthread_self() ? &u_winoff : NULL;

	ret = -XENOMAI_SYSCALL4(sc_cobalt_thread_setschedprio,
				thread, prio, u_winoff_ptr, &promoted);

	/*
	 * If the kernel has no reference to the target thread. let glibc
	 * handle the call.
	 */
	if (ret == ESRCH)
		return __STD(pthread_setschedprio(thread, prio));

	if (ret == 0 && promoted) {
		cobalt_sigshadow_install_once();
		cobalt_set_tsd(u_winoff);
		cobalt_thread_harden();
	}

	return ret;
}

/**
 * Get the scheduling policy and parameters of the specified thread.
 *
 * This service returns, at the addresses @a policy and @a par, the
 * current scheduling policy and scheduling parameters (i.e. priority)
 * of the Cobalt thread @a tid. If @a thread is not the identifier of
 * a Cobalt thread, this service fallback to the regular POSIX
 * pthread_getschedparam() service.
 *
 * @param thread target thread;
 *
 * @param policy address where the scheduling policy of @a tid is stored on
 * success;
 *
 * @param param address where the scheduling parameters of @a tid is stored on
 * success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a tid is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, pthread_getschedparam, (pthread_t thread,
					 int *__restrict__ policy,
					 struct sched_param *__restrict__ param))
{
	struct sched_param_ex param_ex;
	int ret;

	ret = pthread_getschedparam_ex(thread, policy, &param_ex);
	if (ret)
		return ret;

	param->sched_priority = param_ex.sched_priority;

	return 0;
}

/**
 * Get extended scheduling policy of thread
 *
 * This service is an extended version of the regular
 * pthread_getschedparam() service, which also supports
 * Cobalt-specific policies, not available with the host Linux
 * environment.
 *
 * @param thread target thread;
 *
 * @param policy_r address where the scheduling policy of @a thread is stored on
 * success;
 *
 * @param param_ex address where the scheduling parameters of @a thread are
 * stored on success.
 *
 * @return 0 on success;
 * @return an error number if:
 * - ESRCH, @a thread is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/pthread_getschedparam.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
int pthread_getschedparam_ex(pthread_t thread,
			     int *__restrict__ policy_r,
			     struct sched_param_ex *__restrict__ param_ex)
{
	struct sched_param short_param;
	int ret;

	ret = -XENOMAI_SYSCALL3(sc_cobalt_thread_getschedparam_ex,
				thread, policy_r, param_ex);
	if (ret == ESRCH) {
		ret = __STD(pthread_getschedparam(thread, policy_r, &short_param));
		if (ret == 0)
			param_ex->sched_priority = short_param.sched_priority;
	}

	return ret;
}

/**
 * Yield the processor.
 *
 * This function move the current thread at the end of its priority group.
 *
 * @retval 0
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/sched_yield.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-primary}
 */
COBALT_IMPL(int, pthread_yield, (void))
{
	return __WRAP(sched_yield());
}

/** @} */

void cobalt_thread_init(void)
{
#ifdef _CS_GNU_LIBPTHREAD_VERSION
	char vers[128];
	linuxthreads =
		!confstr(_CS_GNU_LIBPTHREAD_VERSION, vers, sizeof(vers))
		|| strstr(vers, "linuxthreads");
#else /* !_CS_GNU_LIBPTHREAD_VERSION */
	linuxthreads = 1;
#endif /* !_CS_GNU_LIBPTHREAD_VERSION */
	pthread_attr_init_ex(&default_attr_ex);
}

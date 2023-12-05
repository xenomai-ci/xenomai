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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <sys/prctl.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

#include <boilerplate/ancillaries.h>
#include <copperplate/clockobj.h>
#include <copperplate/threadobj.h>
#include "internal.h"

static int thread_spawn_prologue(struct corethread_attributes *cta);

static int thread_spawn_epilogue(struct corethread_attributes *cta);

static void *thread_trampoline(void *arg);

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *ptid_r)
{
	pthread_attr_ex_t attr_ex;
	size_t stacksize;
	int ret;

	ret = thread_spawn_prologue(cta);
	if (ret)
		return __bt(ret);

	stacksize = cta->stacksize;
	if (stacksize < PTHREAD_STACK_DEFAULT)
		stacksize = PTHREAD_STACK_DEFAULT;

	pthread_attr_init_ex(&attr_ex);
	pthread_attr_setinheritsched_ex(&attr_ex, PTHREAD_INHERIT_SCHED);
	pthread_attr_setstacksize_ex(&attr_ex, stacksize);
	pthread_attr_setdetachstate_ex(&attr_ex, cta->detachstate);
	ret = -pthread_create_ex(ptid_r, &attr_ex, thread_trampoline, cta);
	pthread_attr_destroy_ex(&attr_ex);
	if (ret)
		return __bt(ret);

	return __bt(thread_spawn_epilogue(cta));
}

int copperplate_renice_local_thread(pthread_t ptid, int policy,
				    const struct sched_param_ex *param_ex)
{
	return -pthread_setschedparam_ex(ptid, policy, param_ex);
}

static inline void prepare_wait_corespec(void)
{
	/*
	 * Switch back to primary mode eagerly, so that both the
	 * parent and the child threads compete on the same priority
	 * scale when handshaking. In addition, this ensures the child
	 * thread enters the run() handler over the Xenomai domain,
	 * which is a basic assumption for all clients.
	 */
	cobalt_thread_harden();
}

int copperplate_kill_tid(pid_t tid, int sig)
{
	return __RT(kill(tid, sig)) ? -errno : 0;
}

int copperplate_probe_tid(pid_t tid)
{
	return cobalt_thread_probe(tid);
}

void copperplate_set_current_name(const char *name)
{
	__RT(pthread_setname_np(pthread_self(), name));
}

#else /* CONFIG_XENO_MERCURY */

int copperplate_kill_tid(pid_t tid, int sig)
{
	return syscall(__NR_tkill, tid, sig) ? -errno : 0;
}

int copperplate_probe_tid(pid_t tid)
{
	return copperplate_kill_tid(tid, 0) && errno != EPERM ? -errno : 0;
}

void copperplate_set_current_name(const char *name)
{
	prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
}

int copperplate_create_thread(struct corethread_attributes *cta,
			      pthread_t *ptid_r)
{
	pthread_attr_t attr;
	size_t stacksize;
	int ret;

	ret = thread_spawn_prologue(cta);
	if (ret)
		return __bt(ret);

	stacksize = cta->stacksize;
	if (stacksize < PTHREAD_STACK_DEFAULT)
		stacksize = PTHREAD_STACK_DEFAULT;

	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
	pthread_attr_setstacksize(&attr, stacksize);
	pthread_attr_setdetachstate(&attr, cta->detachstate);
	ret = -pthread_create(ptid_r, &attr, thread_trampoline, cta);
	pthread_attr_destroy(&attr);

	if (ret)
		return __bt(ret);

	return __bt(thread_spawn_epilogue(cta));
}

int copperplate_renice_local_thread(pthread_t ptid, int policy,
				    const struct sched_param_ex *param_ex)
{
	struct sched_param param = {
		.sched_priority = param_ex->sched_priority,
	};

	return -__RT(pthread_setschedparam(ptid, policy, &param));
}

static inline void prepare_wait_corespec(void)
{
	/* empty */
}

#endif  /* CONFIG_XENO_MERCURY */

int copperplate_get_current_name(char *name, size_t maxlen)
{
	if (maxlen < 16)
		return -ENOSPC;

	return prctl(PR_GET_NAME, (unsigned long)name, 0, 0, 0);
}

static int thread_spawn_prologue(struct corethread_attributes *cta)
{
	int ret;

	ret = __RT(sem_init(&cta->__reserved.warm, 0, 0));
	if (ret)
		return __bt(-errno);

	cta->__reserved.status = -ENOSYS;

	return 0;
}

static void thread_spawn_wait(sem_t *sem)
{
	int ret;

	for (;;) {
		ret = __RT(sem_wait(sem));
		if (ret && errno == EINTR)
			continue;
		if (ret == 0)
			return;
		ret = -errno;
		panic("sem_wait() failed with %s", symerror(ret));
	}
}

static void *thread_trampoline(void *arg)
{
	struct corethread_attributes *cta = arg, _cta;
	sem_t released;
	int ret;

	/*
	 * cta may be on the parent's stack, so it may be dandling
	 * soon after the parent is posted: copy this argument
	 * structure early on.
	 */
	_cta = *cta;

	ret = __RT(sem_init(&released, 0, 0));
	if (ret) {
		ret = __bt(-errno);
		cta->__reserved.status = ret;
		warning("lack of resources for core thread, %s", symerror(ret));
		goto fail;
	}

	cta->__reserved.released = &released;
	ret = cta->prologue(cta->arg);
	cta->__reserved.status = ret;
	if (ret) {
		__RT(sem_destroy(&released));
		backtrace_check();
		goto fail;
	}

	/*
	 * CAUTION: Once the prologue handler has run successfully,
	 * the client code may assume that we can't fail spawning the
	 * child thread anymore, which guarantees that
	 * copperplate_create_thread() will return a success code
	 * after this point. This is important so that any thread
	 * finalizer installed by the prologue handler won't conflict
	 * with the cleanup code the client may run whenever
	 * copperplate_create_thread() fails.
	 */
	ret = __bt(copperplate_renice_local_thread(pthread_self(),
			   _cta.policy, &_cta.param_ex));
	if (ret)
		warning("cannot renice core thread, %s", symerror(ret));
	prepare_wait_corespec();
	__RT(sem_post(&cta->__reserved.warm));
	thread_spawn_wait(&released);
	__RT(sem_destroy(&released));

	return _cta.run(_cta.arg);
fail:
	__RT(sem_post(&cta->__reserved.warm));

	return (void *)(long)ret;
}

static int thread_spawn_epilogue(struct corethread_attributes *cta)
{
	prepare_wait_corespec();
	thread_spawn_wait(&cta->__reserved.warm);

	if (cta->__reserved.status == 0)
		__RT(sem_post(cta->__reserved.released));

	__RT(sem_destroy(&cta->__reserved.warm));

	return __bt(cta->__reserved.status);
}

void __panic(const char *fn, const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	___panic(fn, thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
}

void warning(const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	__warning(thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
	va_end(ap);
}

void notice(const char *fmt, ...)
{
	struct threadobj *thobj = threadobj_current();
	va_list ap;

	va_start(ap, fmt);
	__notice(thobj ? threadobj_get_name(thobj) : NULL, fmt, ap);
	va_end(ap);
}

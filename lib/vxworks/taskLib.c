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
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sched.h>
#include "taskLib.h"
#include "tickLib.h"
#include "msgQLib.h"
#include "taskHookLib.h"
#include "boilerplate/namegen.h"
#include "copperplate/heapobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/cluster.h"
#include "copperplate/internal.h"
#include "copperplate/registry-obstack.h"
#include "vxworks/errnoLib.h"

union wind_wait_union {
	struct wind_queue_wait queue_wait;
};

struct cluster wind_task_table;

DEFINE_PRIVATE_LIST(wind_task_list);

pthread_mutex_t wind_task_lock;

int wind_time_slice = 0;

static DEFINE_NAME_GENERATOR(task_namegen, "task",
			     struct wind_task, name);

static struct wind_task *find_wind_task(TASK_ID tid)
{
	struct WIND_TCB *tcb = mainheap_deref(tid, struct WIND_TCB);
	struct wind_task *task;

	/*
	 * Best-effort to validate a TCB pointer the cheap way,
	 * without relying on any syscall.
	 */
	if (tcb == NULL || ((uintptr_t)tcb & (sizeof(uintptr_t)-1)) != 0)
		return NULL;

	task = tcb->opaque;
	if (task == NULL || ((uintptr_t)task & (sizeof(uintptr_t)-1)) != 0)
		return NULL;

	if (threadobj_get_magic(&task->thobj) != task_magic)
		return NULL;

	return task;
}

static struct wind_task *find_wind_task_or_self(TASK_ID tid)
{
	if (tid)
		return find_wind_task(tid);

	return wind_task_current();
}

struct wind_task *get_wind_task(TASK_ID tid)
{
	struct wind_task *task = find_wind_task(tid);

	/*
	 * Grab the task lock, assuming that the task might have been
	 * deleted, and/or maybe we have been lucky, and some random
	 * opaque pointer might lead us to something which is laid in
	 * valid memory but certainly not to a task object. Last
	 * chance is pthread_mutex_lock() in threadobj_lock()
	 * detecting a wrong mutex kind and bailing out.
	 *
	 * NOTE: threadobj_lock() disables cancellability for the
	 * caller upon success, until the lock is dropped in
	 * threadobj_unlock(), so there is no way it may vanish while
	 * holding the lock. Therefore we need no cleanup handler
	 * here.
	 */
	if (task == NULL || threadobj_lock(&task->thobj) == -EINVAL)
		return NULL;

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&task->thobj) != task_magic) {
		threadobj_unlock(&task->thobj);
		return NULL;
	}

	return task;
}

struct wind_task *get_wind_task_or_self(TASK_ID tid)
{
	struct wind_task *current;

	if (tid)
		return get_wind_task(tid);

	current = wind_task_current();
	if (current == NULL)
		return NULL;

	/* This one might block but can't fail, it is ours. */
	threadobj_lock(&current->thobj);

	return current;
}

void put_wind_task(struct wind_task *task)
{
	threadobj_unlock(&task->thobj);
}

int get_task_status(struct wind_task *task)
{
	int status = threadobj_get_status(&task->thobj), ret = WIND_READY;

	if (status & __THREAD_S_SUSPENDED)
		ret |= WIND_SUSPEND;

	if (status & (__THREAD_S_WAIT|__THREAD_S_TIMEDWAIT))
		ret |= WIND_PEND;
	else if (status & __THREAD_S_DELAYED)
		ret |= WIND_DELAY;

	return ret;
}

static void task_finalizer(struct threadobj *thobj)
{
	struct wind_task *task = container_of(thobj, struct wind_task, thobj);

	if (pvholder_linked(&task->next)) {
		write_lock_nocancel(&wind_task_lock);
		pvlist_remove(&task->next);
		write_unlock(&wind_task_lock);
		wind_run_hooks(&wind_delete_hooks, task);
	}

	task->tcb->status |= WIND_DEAD;
	cluster_delobj(&wind_task_table, &task->cobj);
	registry_destroy_file(&task->fsobj);
	__RT(pthread_mutex_destroy(&task->safelock));
}

#ifdef CONFIG_XENO_REGISTRY

static void task_decode_status(struct fsobstack *o, struct wind_task *task)
{
	int status = threadobj_get_status(&task->thobj);

	if (threadobj_get_lockdepth(&task->thobj) > 0)
		fsobstack_grow_string(o, " LOCK");

	if (threadobj_get_policy(&task->thobj) == SCHED_RR)
		fsobstack_grow_string(o, " RR");

	if (status & __THREAD_S_SUSPENDED)
		fsobstack_grow_string(o, " SUSPEND");

	if (status & (__THREAD_S_WAIT|__THREAD_S_TIMEDWAIT))
		fsobstack_grow_string(o, " PEND");
	else if (status & __THREAD_S_DELAYED)
		fsobstack_grow_string(o, " DELAY");
	else
		fsobstack_grow_string(o, " READY");
}

static int task_registry_open(struct fsobj *fsobj, void *priv)
{
	struct fsobstack *o = priv;
	struct wind_task *task;
	int ret;

	task = container_of(fsobj, struct wind_task, fsobj);
	ret = threadobj_lock(&task->thobj);
	if (ret)
		return -EIO;

	fsobstack_init(o);

	fsobstack_grow_format(o, "errno      = %d\n",
			      threadobj_get_errno(&task->thobj));
	fsobstack_grow_string(o, "status     =");
	task_decode_status(o, task);
	fsobstack_grow_format(o, "\npriority   = %d\n",
			      wind_task_get_priority(task));
	fsobstack_grow_format(o, "lock_depth = %d\n",
			      threadobj_get_lockdepth(&task->thobj));

	threadobj_unlock(&task->thobj);

	fsobstack_finish(o);

	return 0;
}

static struct registry_operations registry_ops = {
	.open		= task_registry_open,
	.release	= fsobj_obstack_release,
	.read		= fsobj_obstack_read
};

#else

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static int task_prologue(void *arg)
{
	struct wind_task *task = arg;

	return __bt(threadobj_prologue(&task->thobj, task->name));
}

static void *task_trampoline(void *arg)
{
	struct wind_task *task = arg;
	struct wind_task_args *args = &task->args;
	struct sched_param_ex param_ex;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	write_lock_nocancel(&wind_task_lock);
	pvlist_append(&task->next, &wind_task_list);
	write_unlock(&wind_task_lock);

	ret = __bt(registry_add_file(&task->fsobj, O_RDONLY,
				     "/vxworks/tasks/%s", task->name));
	if (ret)
		warning("failed to export task %s to registry, %s",
			task->name, symerror(ret));

	wind_run_hooks(&wind_create_hooks, task);
	
	/* Wait for someone to run taskActivate() upon us. */
	threadobj_wait_start();

	/* Turn on time slicing if RR globally enabled. */
	if (wind_time_slice) {
		struct timespec ts;
		clockobj_ticks_to_timespec(&wind_clock, wind_time_slice, &ts);
		param_ex.sched_rr_quantum.tv_sec = ts.tv_sec;
		param_ex.sched_rr_quantum.tv_nsec = ts.tv_nsec;
		threadobj_lock(&task->thobj);
		param_ex.sched_priority = threadobj_get_priority(&task->thobj);
		threadobj_set_schedparam(&task->thobj, SCHED_RR, &param_ex);
		threadobj_unlock(&task->thobj);
	}

	threadobj_notify_entry();

	CANCEL_RESTORE(svc);

	args->entry(args->arg0, args->arg1, args->arg2, args->arg3,
		    args->arg4, args->arg5, args->arg6, args->arg7,
		    args->arg8, args->arg9);

	return NULL;
}

/*
 * By default, WIND kernel priorities are reversely mapped to
 * SCHED_FIFO levels. The available priority range is [1..256] over
 * Cobalt when running in primary mode, and [1..99] over the regular
 * kernel with the POSIX interface.
 *
 * NOTE: over Cobalt, a thread transitioning to secondary mode has its
 * priority ceiled to 99 in the regular POSIX SCHED_FIFO class.
 *
 * The application code may override the routine doing the priority
 * mapping from VxWorks to SCHED_FIFO (normalize). Normalized
 * priorities returned by this routine must be in the range [ 1
 * .. threadobj_high_prio ] inclusive.
 */
__weak int wind_task_normalize_priority(int wind_prio)
{
	/*
	 * SCHED_FIFO priorities are always 1-based regardless of the
	 * underlying real-time core. We remap the lowest VxWorks
	 * priority to the lowest available level in the SCHED_FIFO
	 * policy.
	 */
	if (wind_prio > threadobj_high_prio - 1)
		panic("current implementation restricts VxWorks "
		      "priority levels to range [%d..0]",
		      threadobj_high_prio - 1);

	/* Map a VxWorks priority level to a SCHED_FIFO one. */
	return threadobj_high_prio - wind_prio - 1;
}

__weak int wind_task_denormalize_priority(int core_prio)
{
	/* Map a SCHED_FIFO priority level to a VxWorks one. */
	return threadobj_high_prio - core_prio - 1;
}

static int check_task_priority(u_long wind_prio, int *core_prio)
{
	if (wind_prio < 0 || wind_prio > 255) /* In theory. */
		return S_taskLib_ILLEGAL_PRIORITY;

	*core_prio = wind_task_normalize_priority(wind_prio);

	return OK;
}

static STATUS __taskInit(struct wind_task *task,
			 struct WIND_TCB *tcb, const char *name,
			 int prio, int flags, FUNCPTR entry, int stacksize)
{
	struct corethread_attributes cta;
	struct threadobj_init_data idata;
	pthread_mutexattr_t mattr;
	int ret, cprio;

	ret = check_task_priority(prio, &cprio);
	if (ret) {
		errno = ret;
		return ERROR;
	}

	task->tcb = tcb;
	initpvh(&task->next);
	tcb->opaque = task;
	tcb->status = WIND_SUSPEND;
	tcb->safeCnt = 0;
	tcb->flags = flags;
	tcb->entry = entry;

	generate_name(task->name, name, &task_namegen);

	idata.magic = task_magic;
	idata.finalizer = task_finalizer;
	idata.policy = cprio ? SCHED_FIFO : SCHED_OTHER;
	idata.param_ex.sched_priority = cprio;
	ret = threadobj_init(&task->thobj, &idata);
	if (ret) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		return ERROR;
	}

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	__RT(pthread_mutex_init(&task->safelock, &mattr));
	pthread_mutexattr_destroy(&mattr);

	ret = __bt(cluster_addobj(&wind_task_table, task->name, &task->cobj));
	if (ret) {
		warning("duplicate task name: %s", task->name);
		threadobj_uninit(&task->thobj);
		__RT(pthread_mutex_destroy(&task->safelock));
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	registry_init_file_obstack(&task->fsobj, &registry_ops);

	cta.policy = idata.policy;
	cta.param_ex.sched_priority = cprio;
	cta.prologue = task_prologue;
	cta.run = task_trampoline;
	cta.arg = task;
	cta.stacksize = stacksize;
	cta.detachstate = PTHREAD_CREATE_DETACHED;
	ret = __bt(copperplate_create_thread(&cta, &task->thobj.ptid));
	if (ret) {
		registry_destroy_file(&task->fsobj);
		cluster_delobj(&wind_task_table, &task->cobj);
		threadobj_uninit(&task->thobj);
		__RT(pthread_mutex_destroy(&task->safelock));
		errno = ret == -EAGAIN ? S_memLib_NOT_ENOUGH_MEMORY : -ret;
		return ERROR;
	}

	return OK;
}

static inline struct wind_task *alloc_task(void)
{
	return threadobj_alloc(struct wind_task,
			       thobj, union wind_wait_union);
}

STATUS taskInit(WIND_TCB *pTcb,
		const char *name,
		int prio,
		int flags,
		char *stack __attribute__ ((unused)),
		int stacksize,
		FUNCPTR entry,
		long arg0, long arg1, long arg2, long arg3, long arg4,
		long arg5, long arg6, long arg7, long arg8, long arg9)
{
	struct wind_task_args *args;
	struct wind_task *task;
	struct service svc;
	STATUS ret = ERROR;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	CANCEL_DEFER(svc);

	task = alloc_task();
	if (task == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		goto out;
	}

	args = &task->args;
	args->entry = entry;
	args->arg0 = arg0;
	args->arg1 = arg1;
	args->arg2 = arg2;
	args->arg3 = arg3;
	args->arg4 = arg4;
	args->arg5 = arg5;
	args->arg6 = arg6;
	args->arg7 = arg7;
	args->arg8 = arg8;
	args->arg9 = arg9;
	ret = __taskInit(task, pTcb, name, prio, flags, entry, stacksize);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

STATUS taskActivate(TASK_ID tid)
{
	struct wind_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_wind_task(tid);
	if (task == NULL) {
		ret = ERROR;
		goto out;
	}

	task->tcb->status &= ~WIND_SUSPEND;
	ret = threadobj_start(&task->thobj);
	switch (ret) {
	case -EIDRM:
		ret = OK;
		break;
	default:
		ret = ERROR;
	case 0:	/* == OK */
		put_wind_task(task);
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

TASK_ID taskSpawn(const char *name,
		  int prio,
		  int flags,
		  int stacksize,
		  FUNCPTR entry,
		  long arg0, long arg1, long arg2, long arg3, long arg4,
		  long arg5, long arg6, long arg7, long arg8, long arg9)
{
	struct wind_task_args *args;
	struct wind_task *task;
	struct service svc;
	TASK_ID tid;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	CANCEL_DEFER(svc);

	task = alloc_task();
	if (task == NULL) {
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		CANCEL_RESTORE(svc);
		return ERROR;
	}

	args = &task->args;
	args->entry = entry;
	args->arg0 = arg0;
	args->arg1 = arg1;
	args->arg2 = arg2;
	args->arg3 = arg3;
	args->arg4 = arg4;
	args->arg5 = arg5;
	args->arg6 = arg6;
	args->arg7 = arg7;
	args->arg8 = arg8;
	args->arg9 = arg9;

	if (__taskInit(task, &task->priv_tcb, name,
		       prio, flags, entry, stacksize) == ERROR) {
		CANCEL_RESTORE(svc);
		return ERROR;
	}

	CANCEL_RESTORE(svc);

	tid = mainheap_ref(&task->priv_tcb, TASK_ID);

	return taskActivate(tid) == ERROR ? ERROR : tid;
}

static STATUS __taskDelete(TASK_ID tid, int force)
{
	struct wind_task *task;
	struct service svc;
	int ret;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	task = find_wind_task_or_self(tid);
	if (task == NULL) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	CANCEL_DEFER(svc);

	/*
	 * We always attempt to grab the thread safe lock first, then
	 * make sure nobody (including the target task itself) will be
	 * able to alter the internal state of that task anymore. In
	 * forced mode, we are allowed to bypass lock contention, but
	 * then we might create dangerous situations leading to
	 * invalid memory references; that's just part of the deal.
	 *
	 * NOTE: Locking order is always safelock first, internal
	 * object lock afterwards, therefore, _never_ call
	 * __taskDelete() directly or indirectly while holding the
	 * thread object lock. You have been warned.
	 */
	if (force)	/* Best effort only. */
		force = __RT(pthread_mutex_trylock(&task->safelock));
	else
		__RT(pthread_mutex_lock(&task->safelock));

	ret = threadobj_lock(&task->thobj);

	if (!force)	/* I.e. do we own the safe lock? */
		__RT(pthread_mutex_unlock(&task->safelock));

	if (ret == 0)
		ret = threadobj_cancel(&task->thobj);

	CANCEL_RESTORE(svc);

	if (ret)
		goto objid_error;

	return OK;
}

STATUS taskDelete(TASK_ID tid)
{
	return __taskDelete(tid, 0);
}

STATUS taskDeleteForce(TASK_ID tid)
{
	return __taskDelete(tid, 1);
}

TASK_ID taskIdSelf(void)
{
	struct wind_task *current;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();
	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	return (TASK_ID)current->tcb;
}

struct WIND_TCB *taskTcb(TASK_ID tid)
{
	struct wind_task *task;

	task = find_wind_task(tid);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return NULL;
	}

	return task->tcb;
}

STATUS taskSuspend(TASK_ID tid)
{
	struct wind_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_wind_task(tid);
	if (task == NULL)
		goto objid_error;

	ret = threadobj_suspend(&task->thobj);
	put_wind_task(task);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		ret = ERROR;
	}

	CANCEL_RESTORE(svc);

	return ret;
}

STATUS taskResume(TASK_ID tid)
{
	struct wind_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_wind_task(tid);
	if (task == NULL)
		goto objid_error;

	ret = threadobj_resume(&task->thobj);
	put_wind_task(task);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		ret = ERROR;
	}

	CANCEL_RESTORE(svc);

	return ret;
}

STATUS taskSafe(void)
{
	struct wind_task *current;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();
	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	/*
	 * Grabbing the safelock will lock out cancellation requests,
	 * so we don't have to issue CANCEL_DEFER().
	 */
	__RT(pthread_mutex_lock(&current->safelock));
	current->tcb->safeCnt++;

	return OK;
}

STATUS taskUnsafe(void)
{
	struct wind_task *current;
	int ret;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();
	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	ret = __RT(pthread_mutex_unlock(&current->safelock));
	if (ret == 0)
		current->tcb->safeCnt--;

	return OK;
}

STATUS taskIdVerify(TASK_ID tid)
{
	struct wind_task *task;

	task = find_wind_task(tid);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	return OK;
}

void taskExit(int code)
{
	pthread_exit((void *)(long)code);
}

STATUS taskPrioritySet(TASK_ID tid, int prio)
{
	struct sched_param_ex param_ex;
	struct wind_task *task;
	int ret, policy, cprio;
	struct service svc;

	CANCEL_DEFER(svc);

	task = get_wind_task(tid);
	if (task == NULL)
		goto objid_error;

	ret = check_task_priority(prio, &cprio);
	if (ret) {
		put_wind_task(task);
		errno = ret;
		ret = ERROR;
		goto out;
	}

	policy = cprio ? SCHED_FIFO : SCHED_OTHER;
	param_ex.sched_priority = cprio;
	ret = threadobj_set_schedparam(&task->thobj, policy, &param_ex);
	if (ret != -EIDRM)
		put_wind_task(task);

	if (ret) {
	objid_error:
		errno = S_objLib_OBJ_ID_ERROR;
		ret = ERROR;
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

int wind_task_get_priority(struct wind_task *task)
{
	/* Can't fail if we hold the task lock as we should. */
	int prio = threadobj_get_priority(&task->thobj);
	return wind_task_denormalize_priority(prio);
}

STATUS taskPriorityGet(TASK_ID tid, int *priop)
{
	struct wind_task *task;
	struct service svc;
	int ret = OK;

	CANCEL_DEFER(svc);

	task = get_wind_task(tid);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		ret = ERROR;
		goto out;
	}

	*priop = wind_task_get_priority(task);
	put_wind_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

STATUS taskLock(void)
{
	struct wind_task *task;
	struct service svc;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	task = find_wind_task_or_self(0);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	CANCEL_DEFER(svc);
	threadobj_lock_sched();
	CANCEL_RESTORE(svc);

	return OK;
}

STATUS taskUnlock(void)
{
	struct wind_task *task;
	struct service svc;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	task = find_wind_task_or_self(0);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	CANCEL_DEFER(svc);
	threadobj_unlock_sched();
	CANCEL_RESTORE(svc);

	return OK;
}

STATUS taskDelay(int ticks)
{
	struct wind_task *current;
	struct timespec rqt;
	struct service svc;
	int ret;

	if (threadobj_irq_p()) {
		errno = S_intLib_NOT_ISR_CALLABLE;
		return ERROR;
	}

	current = wind_task_current();
	if (current == NULL) {
		errno = S_objLib_OBJ_NO_METHOD;
		return ERROR;
	}

	if (ticks == 0) {
		sched_yield();	/* Manual round-robin. */
		return OK;
	}

	CANCEL_DEFER(svc);

	clockobj_ticks_to_timeout(&wind_clock, ticks, &rqt);
	ret = threadobj_sleep(&rqt);
	if (ret) {
		errno = -ret;
		ret = ERROR;
	}

	CANCEL_RESTORE(svc);

	return ret;
}

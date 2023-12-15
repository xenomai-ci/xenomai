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
#include <sched.h>
#include "boilerplate/namegen.h"
#include "copperplate/heapobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/cluster.h"
#include "copperplate/internal.h"
#include "psos/psos.h"
#include "internal.h"
#include "task.h"
#include "tm.h"
#include "queue.h"
#include "rn.h"

union psos_wait_union {
	struct psos_queue_wait queue_wait;
	struct psos_rn_wait rn_wait;
};

struct cluster psos_task_table;

static DEFINE_NAME_GENERATOR(task_namegen, "task",
			     struct psos_task, name);

static struct psos_task *find_psos_task(u_long tid, int *err_r)
{
	struct psos_task *task = mainheap_deref(tid, struct psos_task);
	unsigned int magic;

	/*
	 * Best-effort to validate a TCB pointer the cheap way,
	 * without relying on any syscall.
	 */
	if (task == NULL || ((uintptr_t)task & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	magic = threadobj_get_magic(&task->thobj);

	if (magic == task_magic) {
		*err_r = SUCCESS;
		return task;
	}

	if (magic == ~task_magic) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	if ((magic >> 16) == 0x8181) {
		*err_r = ERR_OBJTYPE;
		return NULL;
	}

objid_error:
	*err_r = ERR_OBJID;

	return NULL;
}

static struct psos_task *find_psos_task_or_self(u_long tid, int *err_r)
{
	struct psos_task *current;

	if (tid)
		return find_psos_task(tid, err_r);

	current = psos_task_current();
	if (current == NULL) {
		*err_r = ERR_SSFN;
		return NULL;
	}

	return current;
}

struct psos_task *get_psos_task(u_long tid, int *err_r)
{
	struct psos_task *task = find_psos_task(tid, err_r);

	/*
	 * Grab the task lock, assuming that the task might have been
	 * deleted, and/or maybe we have been lucky, and some random
	 * opaque pointer might lead us to something which is laid in
	 * valid memory but certainly not to a task object. Last
	 * chance is pthread_mutex_lock() detecting a wrong mutex kind
	 * and bailing out.
	 *
	 * XXX: threadobj_lock() disables cancellability for the
	 * caller upon success, until the lock is dropped in
	 * threadobj_unlock(), so there is no way it may vanish while
	 * holding the lock. Therefore we need no cleanup handler
	 * here.
	 */
	if (task == NULL || threadobj_lock(&task->thobj) == -EINVAL) {
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&task->thobj) != task_magic) {
		threadobj_unlock(&task->thobj);
		*err_r = ERR_OBJDEL;
		return NULL;
	}

	return task;
}

struct psos_task *get_psos_task_or_self(u_long tid, int *err_r)
{
	struct psos_task *current;

	if (tid)
		return get_psos_task(tid, err_r);

	current = psos_task_current();
	if (current == NULL) {
		*err_r = ERR_SSFN;
		return NULL;
	}

	*err_r = SUCCESS;

	/* This one might block but can't fail, it is ours. */
	threadobj_lock(&current->thobj);

	return current;
}

void put_psos_task(struct psos_task *task)
{
	threadobj_unlock(&task->thobj);
}

static void task_finalizer(struct threadobj *thobj)
{
	struct psos_task *task = container_of(thobj, struct psos_task, thobj);
	struct psos_tm *tm, *tmp;
	struct syncstate syns;
	int ret;

	cluster_delobj(&psos_task_table, &task->cobj);

	if (!pvlist_empty(&task->timer_list)) {
		pvlist_for_each_entry_safe(tm, tmp, &task->timer_list, link)
			tm_cancel((u_long)tm);
	}

	/* We have to hold a lock on a syncobj to destroy it. */
	ret = __bt(syncobj_lock(&task->sobj, &syns));
	if (ret == 0)
		syncobj_destroy(&task->sobj, &syns);
}

static int task_prologue(void *arg)
{
	struct psos_task *task = arg;

	return __bt(threadobj_prologue(&task->thobj, task->name));
}

static void *task_trampoline(void *arg)
{
	struct psos_task *task = arg;
	struct psos_task_args *args = &task->args;
	struct sched_param_ex param_ex;
	struct service svc;

	CANCEL_DEFER(svc);
	threadobj_wait_start();
	threadobj_lock(&task->thobj);

	if (task->mode & T_TSLICE) {
		param_ex.sched_priority = threadobj_get_priority(&task->thobj);
		param_ex.sched_rr_quantum.tv_sec = psos_rrperiod.tv_sec;
		param_ex.sched_rr_quantum.tv_nsec = psos_rrperiod.tv_nsec;
		threadobj_set_schedparam(&task->thobj, SCHED_RR, &param_ex);
	}

	if (task->mode & T_NOPREEMPT)
		__threadobj_lock_sched(&task->thobj);

	threadobj_unlock(&task->thobj);
	threadobj_notify_entry();
	CANCEL_RESTORE(svc);

	args->entry(args->arg0, args->arg1, args->arg2, args->arg3);

	return NULL;
}

/*
 * By default, pSOS priorities are mapped 1:1 to SCHED_FIFO
 * levels. The available priority range is [1..256] over Cobalt when
 * running in primary mode, and [1..99] over the regular kernel with
 * the POSIX interface.
 *
 * NOTE: over Cobalt, a thread transitioning to secondary mode has its
 * priority ceiled to 99 in the regular POSIX SCHED_FIFO class.
 *
 * The application code may override the routine doing the priority
 * mapping from pSOS to SCHED_FIFO (normalize). Normalized priorities
 * returned by this routine must be in the range [ 1
 * .. threadobj_high_prio] inclusive.
 */
__weak int psos_task_normalize_priority(unsigned long psos_prio)
{
	if (psos_prio > threadobj_high_prio)
		panic("current implementation restricts pSOS "
		      "priority levels to range [1..%d]",
		      threadobj_high_prio);

	/* Map a pSOS priority level to a SCHED_FIFO one. */
	return psos_prio;
}

/*
 * Although default pSOS priorities are mapped 1:1 to SCHED_FIFO, we
 * do still have to use a denormalize function because these calls are
 * weak and application code may be override the call and implement
 * the mapping differently.
 */
__weak unsigned long psos_task_denormalize_priority(int core_prio)
{
	/* Map a SCHED_FIFO priority level to a pSOS one. */
	return core_prio;
}

static int check_task_priority(u_long psos_prio, int *core_prio)
{
	if (psos_prio < 1 || psos_prio > 255) /* In theory. */
		return ERR_PRIOR;

	*core_prio = psos_task_normalize_priority(psos_prio);

	return SUCCESS;
}

static int psos_task_get_priority(struct psos_task *task)
{
	int prio = threadobj_get_priority(&task->thobj);
	return psos_task_denormalize_priority(prio);
}

u_long t_create(const char *name, u_long prio,
		u_long sstack, u_long ustack, u_long flags, u_long *tid_r)
{
	struct corethread_attributes cta;
	struct threadobj_init_data idata;
	struct psos_task *task;
	struct service svc;
	int ret, cprio = 1;
	char short_name[5];

	ret = check_task_priority(prio, &cprio);
	if (ret)
		return ret;

	CANCEL_DEFER(svc);

	task = threadobj_alloc(struct psos_task,
			       thobj, union psos_wait_union);
	if (task == NULL) {
		ret = ERR_NOTCB;
		goto out;
	}

	ustack += sstack;

	/*
	 * Make sure we are granted a minimal amount of stack space
	 * for common usage of the Glibc. If zero, we will pick a
	 * value based on the implementation default for such minimum.
	 */
	if (ustack > 0 && ustack < 8192) {
		threadobj_free(&task->thobj);
		ret = ERR_TINYSTK;
		goto out;
	}

	if (name == NULL || *name == '\0')
		generate_name(task->name, name, &task_namegen);
	else {
		name = psos_trunc_name(short_name, name);
		namecpy(task->name, name);
	}

	task->flags = flags;	/* We don't do much with those. */
	task->mode = 0;	/* Not yet known. */
	task->events = 0;
	ret = syncobj_init(&task->sobj, CLOCK_COPPERPLATE, 0, fnref_null);
	if (ret)
		goto fail_syncinit;

	memset(task->notepad, 0, sizeof(task->notepad));
	pvlist_init(&task->timer_list);
	*tid_r = mainheap_ref(task, u_long);

	idata.magic = task_magic;
	idata.finalizer = task_finalizer;
	idata.policy = cprio ? SCHED_FIFO : SCHED_OTHER;
	idata.param_ex.sched_priority = cprio;
	ret = threadobj_init(&task->thobj, &idata);
	if (ret)
		goto fail_threadinit;

	ret = __bt(cluster_addobj_dup(&psos_task_table, task->name, &task->cobj));
	if (ret) {
		warning("cannot register task: %s", task->name);
		goto fail_register;
	}

	cta.policy = idata.policy;
	cta.param_ex.sched_priority = cprio;
	cta.prologue = task_prologue;
	cta.run = task_trampoline;
	cta.arg = task;
	cta.stacksize = ustack;
	cta.detachstate = PTHREAD_CREATE_DETACHED;

	ret = __bt(copperplate_create_thread(&cta, &task->thobj.ptid));
	if (ret) {
		cluster_delobj(&psos_task_table, &task->cobj);
	fail_register:
		threadobj_uninit(&task->thobj);
	fail_threadinit:
		syncobj_uninit(&task->sobj);
	fail_syncinit:
		ret = ERR_NOTCB;
		threadobj_free(&task->thobj);
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_start(u_long tid,
	       u_long mode,
	       void (*entry)(u_long, u_long, u_long, u_long),
	       u_long args[])
{
	struct psos_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_psos_task(tid, &ret);
	if (task == NULL)
		goto out;

	task->args.entry = entry;
	if (args) {
		task->args.arg0 = args[0];
		task->args.arg1 = args[1];
		task->args.arg2 = args[2];
		task->args.arg3 = args[3];
	} else {
		task->args.arg0 = 0;
		task->args.arg1 = 0;
		task->args.arg2 = 0;
		task->args.arg3 = 0;
	}
	task->mode = mode;
	ret = threadobj_start(&task->thobj);
	switch (ret) {
	case -EIDRM:
		ret = SUCCESS;
		break;
	default:
		ret = ERR_OBJDEL;
	case 0:	/* == SUCCESS */
		put_psos_task(task);
	}
 out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_suspend(u_long tid)
{
	struct psos_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		goto out;

	ret = threadobj_suspend(&task->thobj);
	if (ret)
		ret = ERR_OBJDEL;

	put_psos_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_resume(u_long tid)
{
	struct psos_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_psos_task(tid, &ret);
	if (task == NULL)
		goto out;

	ret = threadobj_resume(&task->thobj);
	if (ret)
		ret = ERR_OBJDEL;

	put_psos_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_setpri(u_long tid, u_long newprio, u_long *oldprio_r)
{
	struct sched_param_ex param_ex;
	int policy, ret, cprio = 1;
	struct psos_task *task;
	struct service svc;

	CANCEL_DEFER(svc);

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		goto out;

	*oldprio_r = psos_task_get_priority(task);

	if (newprio == 0) /* Only inquires for the task priority. */
		goto done;

	ret = check_task_priority(newprio, &cprio);
	if (ret) {
		ret = ERR_SETPRI;
		goto done;
	}

	policy = cprio ? SCHED_FIFO : SCHED_OTHER;
	param_ex.sched_priority = cprio;
	ret = threadobj_set_schedparam(&task->thobj, policy, &param_ex);
	switch (ret) {
	case -EIDRM:
		ret = SUCCESS;
		goto out;
	default:
		ret = ERR_OBJDEL;
	case 0:	/* == SUCCESS */
		break;
	}
done:
	put_psos_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_delete(u_long tid)
{
	struct psos_task *task;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		goto out;

	ret = threadobj_cancel(&task->thobj);
	if (ret)
		ret = ERR_OBJDEL;
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_ident(const char *name, u_long node, u_long *tid_r)
{
	struct clusterobj *cobj;
	struct psos_task *task;
	struct service svc;
	char short_name[5];
	int ret = SUCCESS;

	if (node)
		return ERR_NODENO;

	CANCEL_DEFER(svc);

	if (name == NULL) {
		task = find_psos_task_or_self(0, &ret);
		if (task == NULL)
			goto out;
	} else {
		name = psos_trunc_name(short_name, name);
		cobj = cluster_findobj(&psos_task_table, name);
		if (cobj == NULL) {
			ret = ERR_OBJNF;
			goto out;
		}
		task = container_of(cobj, struct psos_task, cobj);
		/*
		 * Last attempt to check whether the task is valid, in
		 * case it is pending deletion.
		 */
		if (threadobj_get_magic(&task->thobj) != task_magic) {
			ret = ERR_OBJNF;
			goto out;
		}
	}

	*tid_r = mainheap_ref(task, u_long);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_getreg(u_long tid, u_long regnum, u_long *regvalue_r)
{
	struct psos_task *task;
	struct service svc;
	int ret;

	if (regnum >= PSOSTASK_NR_REGS)
		return ERR_REGNUM;

	CANCEL_DEFER(svc);

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		goto out;

	*regvalue_r = task->notepad[regnum];
	put_psos_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_setreg(u_long tid, u_long regnum, u_long regvalue)
{
	struct psos_task *task;
	struct service svc;
	int ret;

	if (regnum >= PSOSTASK_NR_REGS)
		return ERR_REGNUM;

	CANCEL_DEFER(svc);

	task = get_psos_task_or_self(tid, &ret);
	if (task == NULL)
		goto out;

	task->notepad[regnum] = regvalue;
	put_psos_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

u_long t_mode(u_long mask, u_long newmask, u_long *oldmode_r)
{
	struct sched_param_ex param_ex;
	struct psos_task *task;
	struct service svc;
	int policy, ret;

	CANCEL_DEFER(svc);

	task = get_psos_task_or_self(0, &ret);
	if (task == NULL)
		goto out;

	*oldmode_r = task->mode;

	if (mask == 0)
		goto done;

	task->mode &= ~mask;
	task->mode |= (newmask & mask);

	if (task->mode & T_NOPREEMPT)
		__threadobj_lock_sched_once(&task->thobj);
	else if (*oldmode_r & T_NOPREEMPT)
		__threadobj_unlock_sched(&task->thobj);

	param_ex.sched_priority = threadobj_get_priority(&task->thobj);

	if (((task->mode ^ *oldmode_r) & T_TSLICE) == 0)
		goto done;	/* rr status not changed. */

	if (task->mode & T_TSLICE) {
		policy = SCHED_RR;
		param_ex.sched_rr_quantum.tv_sec = psos_rrperiod.tv_sec;
		param_ex.sched_rr_quantum.tv_nsec = psos_rrperiod.tv_nsec;
	} else {
		policy = param_ex.sched_priority ? SCHED_FIFO : SCHED_OTHER;
	}

	/* Working on self, so -EIDRM can't happen. */
	threadobj_set_schedparam(&task->thobj, policy, &param_ex);
done:
	put_psos_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

static int collect_events(struct psos_task *task,
			  u_long flags, u_long events, u_long *events_r)
{
	if (((flags & EV_ANY) && (events & task->events) != 0) ||
	    (!(flags & EV_ANY) && ((events & task->events) == events))) {
		/*
		 * The condition is satisfied; update the return value
		 * with the set of matched events, and clear the
		 * collected events from the task's mask.
		 */
		*events_r = (task->events & events);
		task->events &= ~events;
		return 1;
	}

	return 0;
}

u_long ev_receive(u_long events, u_long flags,
		  u_long timeout, u_long *events_r)
{
	struct timespec ts, *timespec;
	struct psos_task *current;
	struct syncstate syns;
	struct service svc;
	int ret;

	current = find_psos_task_or_self(0, &ret);
	if (current == NULL)
		return ret;

	CANCEL_DEFER(svc);

	ret = syncobj_lock(&current->sobj, &syns);
	if (ret) {
		ret = ERR_OBJDEL;
		goto out;
	}

	if (events == 0) {
		*events_r = current->events; /* Only polling events. */
		goto done;
	}

	if (collect_events(current, flags, events, events_r))
		goto done;

	if (flags & EV_NOWAIT) {
		ret = ERR_NOEVS;
		goto done;
	}

	if (timeout != 0) {
		timespec = &ts;
		clockobj_ticks_to_timeout(&psos_clock, timeout, timespec);
	} else
		timespec = NULL;

	for (;;) {
		ret = syncobj_wait_grant(&current->sobj, timespec, &syns);
		if (ret == -ETIMEDOUT) {
			ret = ERR_TIMEOUT;
			break;
		}
		if (collect_events(current, flags, events, events_r))
			break;
	}
done:
	syncobj_unlock(&current->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

int __ev_send(struct psos_task *task, u_long events)
{
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&task->sobj, &syns);
	if (ret)
		return ERR_OBJDEL;

	task->events |= events;
	/*
	 * If the task is pending in ev_receive(), it's likely that we
	 * are posting events the task is waiting for, so we can wake
	 * it up immediately and let it confirm whether the condition
	 * is now satisfied.
	 */
	syncobj_grant_one(&task->sobj);

	syncobj_unlock(&task->sobj, &syns);

	return 0;
}

u_long ev_send(u_long tid, u_long events)
{
	struct psos_task *task;
	struct service svc;
	int ret = SUCCESS;

	task = find_psos_task_or_self(tid, &ret);
	if (task == NULL)
		return ret;

	CANCEL_DEFER(svc);
	ret = __ev_send(task, events);
	CANCEL_RESTORE(svc);

	return ret;
}

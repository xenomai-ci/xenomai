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

#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include "copperplate/heapobj.h"
#include "copperplate/internal.h"
#include "internal.h"
#include "task.h"
#include "buffer.h"
#include "queue.h"
#include "timer.h"
#include "heap.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_task Task management services
 *
 * Services dealing with preemptive multi-tasking
 *
 * Each Alchemy task is an independent portion of the overall
 * application code embodied in a C procedure, which executes on its
 * own stack context.
 *
 * @{
 */

union alchemy_wait_union {
	struct alchemy_task_wait task_wait;
	struct alchemy_buffer_wait buffer_wait;
	struct alchemy_queue_wait queue_wait;
	struct alchemy_heap_wait heap_wait;
};

struct syncluster alchemy_task_table;

static DEFINE_NAME_GENERATOR(task_namegen, "task",
			     struct alchemy_task, name);

#ifdef CONFIG_XENO_REGISTRY

static int task_registry_open(struct fsobj *fsobj, void *priv)
{
	struct fsobstack *o = priv;
	struct threadobj_stat buf;
	struct alchemy_task *tcb;
	int ret;

	tcb = container_of(fsobj, struct alchemy_task, fsobj);
	ret = threadobj_lock(&tcb->thobj);
	if (ret)
		return -EIO;

	ret = threadobj_stat(&tcb->thobj, &buf);
	threadobj_unlock(&tcb->thobj);
	if (ret)
		return ret;

	fsobstack_init(o);

	fsobstack_finish(o);

	return 0;
}

static struct registry_operations registry_ops = {
	.open		= task_registry_open,
	.release	= fsobj_obstack_release,
	.read		= fsobj_obstack_read
};

#else /* !CONFIG_XENO_REGISTRY */

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static struct alchemy_task *find_alchemy_task(RT_TASK *task, int *err_r)
{
	struct alchemy_task *tcb;

	if (bad_pointer(task))
		goto bad_handle;

	tcb = mainheap_deref(task->handle, struct alchemy_task);
	if (bad_pointer(tcb))
		goto bad_handle;

	if (threadobj_get_magic(&tcb->thobj) == task_magic)
		return tcb;
bad_handle:
	*err_r = -EINVAL;

	return NULL;
}

static struct alchemy_task *find_alchemy_task_or_self(RT_TASK *task, int *err_r)
{
	struct alchemy_task *current;

	if (task)
		return find_alchemy_task(task, err_r);

	current = alchemy_task_current();
	if (current == NULL) {
		*err_r = -EPERM;
		return NULL;
	}

	return current;
}

struct alchemy_task *get_alchemy_task(RT_TASK *task, int *err_r)
{
	struct alchemy_task *tcb = find_alchemy_task(task, err_r);

	/*
	 * Grab the task lock, assuming that the task might have been
	 * deleted, and/or maybe we have been lucky, and some random
	 * opaque pointer might lead us to something which is laid in
	 * valid memory but certainly not to a task object. Last
	 * chance is pthread_mutex_lock() detecting a wrong mutex kind
	 * and bailing out.
	 */
	if (tcb == NULL || threadobj_lock(&tcb->thobj) == -EINVAL) {
		*err_r = -EINVAL;
		return NULL;
	}

	/* Check the magic word again, while we hold the lock. */
	if (threadobj_get_magic(&tcb->thobj) != task_magic) {
		threadobj_unlock(&tcb->thobj);
		*err_r = -EINVAL;
		return NULL;
	}

	return tcb;
}

struct alchemy_task *get_alchemy_task_or_self(RT_TASK *task, int *err_r)
{
	struct alchemy_task *current;

	if (task)
		return get_alchemy_task(task, err_r);

	current = alchemy_task_current();
	if (current == NULL) {
		*err_r = -EPERM;
		return NULL;
	}

	/* This one might block but can't fail, it is ours. */
	threadobj_lock(&current->thobj);

	return current;
}

void put_alchemy_task(struct alchemy_task *tcb)
{
	threadobj_unlock(&tcb->thobj);
}

static void task_finalizer(struct threadobj *thobj)
{
	struct alchemy_task *tcb;
	struct syncstate syns;
	int ret;

	tcb = container_of(thobj, struct alchemy_task, thobj);
	registry_destroy_file(&tcb->fsobj);
	syncluster_delobj(&alchemy_task_table, &tcb->cobj);
	/*
	 * The msg sync may be pended by other threads, so we do have
	 * to use syncobj_destroy() on it (i.e. NOT syncobj_uninit()).
	 */
	ret = __bt(syncobj_lock(&tcb->sobj_msg, &syns));
	if (ret == 0)
		syncobj_destroy(&tcb->sobj_msg, &syns);
}

static int task_prologue_1(void *arg)
{
	struct alchemy_task *tcb = arg;

	return __bt(threadobj_prologue(&tcb->thobj, tcb->name));
}

static int task_prologue_2(struct alchemy_task *tcb)
{
	int ret;

	threadobj_wait_start();
	threadobj_lock(&tcb->thobj);
	ret = threadobj_set_mode(0, tcb->mode, NULL);
	threadobj_unlock(&tcb->thobj);

	return ret;
}

static void *task_entry(void *arg)
{
	struct alchemy_task *tcb = arg;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	ret = __bt(task_prologue_2(tcb));
	if (ret) {
		CANCEL_RESTORE(svc);
		return (void *)(long)ret;
	}

	threadobj_notify_entry();

	CANCEL_RESTORE(svc);

	tcb->entry(tcb->arg);

	return NULL;
}

static void delete_tcb(struct alchemy_task *tcb)
{
	syncobj_uninit(&tcb->sobj_msg);
	threadobj_uninit(&tcb->thobj);
	threadobj_free(&tcb->thobj);
}

static int create_tcb(struct alchemy_task **tcbp, RT_TASK *task,
		      const char *name, int prio, int mode)
{
	struct threadobj_init_data idata;
	struct alchemy_task *tcb;
	int ret;

	if (threadobj_irq_p())
		return -EPERM;

	ret = check_task_priority(prio);
	if (ret)
		return ret;

	tcb = threadobj_alloc(struct alchemy_task, thobj,
			      union alchemy_wait_union);
	if (tcb == NULL)
		return -ENOMEM;

	generate_name(tcb->name, name, &task_namegen);

	tcb->mode = mode;
	tcb->entry = NULL;	/* Not yet known. */
	tcb->arg = NULL;

	CPU_ZERO(&tcb->affinity);

	ret = syncobj_init(&tcb->sobj_msg, CLOCK_COPPERPLATE,
			   SYNCOBJ_PRIO, fnref_null);
	if (ret)
		goto fail_syncinit;

	tcb->suspends = 0;
	tcb->flowgen = 0;

	idata.magic = task_magic;
	idata.finalizer = task_finalizer;
	idata.policy = prio ? SCHED_FIFO : SCHED_OTHER;
	idata.param_ex.sched_priority = prio;
	ret = threadobj_init(&tcb->thobj, &idata);
	if (ret)
		goto fail_threadinit;

	*tcbp = tcb;

	/*
	 * CAUTION: The task control block must be fully built before
	 * we publish it through syncluster_addobj(), at which point
	 * it could be referred to immediately from another task as we
	 * got preempted. In addition, the task descriptor must be
	 * updated prior to starting the task.
	 */
	tcb->self.handle = mainheap_ref(tcb, uintptr_t);

	registry_init_file_obstack(&tcb->fsobj, &registry_ops);
	ret = __bt(registry_add_file(&tcb->fsobj, O_RDONLY,
				     "/alchemy/tasks/%s", tcb->name));
	if (ret)
		warning("failed to export task %s to registry, %s",
			tcb->name, symerror(ret));

	ret = syncluster_addobj(&alchemy_task_table, tcb->name, &tcb->cobj);
	if (ret)
		goto fail_register;

	if (task)
		task->handle = tcb->self.handle;

	return 0;

fail_register:
	registry_destroy_file(&tcb->fsobj);
	threadobj_uninit(&tcb->thobj);
fail_threadinit:
	syncobj_uninit(&tcb->sobj_msg);
fail_syncinit:
	threadobj_free(&tcb->thobj);

	return ret;
}

/**
 * @fn int rt_task_create(RT_TASK *task, const char *name, int stksize, int prio, int mode)
 * @brief Create a task with Alchemy personality.
 *
 * This service creates a task with access to the full set of Alchemy
 * services. If @a prio is non-zero, the new task belongs to Xenomai's
 * real-time FIFO scheduling class, aka SCHED_FIFO. If @a prio is
 * zero, the task belongs to the regular SCHED_OTHER class.
 *
 * Creating tasks with zero priority is useful for running non
 * real-time processes which may invoke blocking real-time services,
 * such as pending on a semaphore, reading from a message queue or a
 * buffer, and so on.
 *
 * Once created, the task is left dormant until it is actually started
 * by rt_task_start().
 *
 * @param task The address of a task descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, a copy of this string is
 * used for indexing the created task into the object registry.
 *
 * @param stksize The size of the stack (in bytes) for the new
 * task. If zero is passed, a system-dependent default size will be
 * substituted.
 *
 * @param prio The base priority of the new task. This value must be
 * in the [0 .. 99] range, where 0 is the lowest effective priority. 
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask:
 *
 * - T_JOINABLE allows another task to wait on the termination of the
 * new task. rt_task_join() shall be called for this task to clean up
 * any resources after its termination.
 *
 * - T_LOCK causes the new task to lock the scheduler prior to
 * entering the user routine specified by rt_task_start(). A call to
 * rt_task_set_mode() from the new task is required to drop this lock.
 *
 * - When running over the Cobalt core, T_WARNSW causes the SIGDEBUG
 * signal to be sent to the current task whenever it switches to the
 * secondary mode. This feature is useful to detect unwanted
 * migrations to the Linux domain. This flag has no effect over the
 * Mercury core.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if either @a prio, @a mode or @a stksize are
 * invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the task.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered task.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 *
 * @sideeffect
 * - When running over the Cobalt core:
 *
 *   - calling rt_task_create() causes SCHED_FIFO tasks to switch to
 * secondary mode.
 *
 *   - members of Xenomai's SCHED_FIFO class running in the primary
 * domain have utmost priority over all Linux activities in the
 * system, including Linux interrupt handlers.
 *
 * - When running over the Mercury core, the new task belongs to the
 * regular POSIX SCHED_FIFO class.
 *
 * @note Tasks can be referred to from multiple processes which all
 * belong to the same Xenomai session.
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_task_create, (RT_TASK *task, const char *name,
				   int stksize, int prio, int mode))
#else
int rt_task_create(RT_TASK *task, const char *name,
		   int stksize, int prio, int mode)
#endif
{
	struct corethread_attributes cta;
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	if (mode & ~(T_LOCK | T_WARNSW | T_JOINABLE))
		return -EINVAL;

	CANCEL_DEFER(svc);

	ret = create_tcb(&tcb, task, name, prio, mode);
	if (ret)
		goto out;

	/* We want this to be set prior to spawning the thread. */
	tcb->self = *task;

	cta.detachstate = mode & T_JOINABLE ?
		PTHREAD_CREATE_JOINABLE : PTHREAD_CREATE_DETACHED;
	cta.policy = threadobj_get_policy(&tcb->thobj);
	threadobj_copy_schedparam(&cta.param_ex, &tcb->thobj);
	cta.prologue = task_prologue_1;
	cta.run = task_entry;
	cta.arg = tcb;
	cta.stacksize = stksize;

	ret = __bt(copperplate_create_thread(&cta, &tcb->thobj.ptid));
	if (ret) {
		delete_tcb(tcb);
	} else {
		tcb->self.thread = tcb->thobj.ptid;
		task->thread = tcb->thobj.ptid;
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_delete(RT_TASK *task)
 * @brief Delete a real-time task.
 *
 * This call terminates a task previously created by
 * rt_task_create().
 *
 * Tasks created with the T_JOINABLE flag shall be joined by a
 * subsequent call to rt_task_join() once successfully deleted, to
 * reclaim all resources.
 *
 * @param task The task descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * - -EPERM is returned if @a task is NULL and this service was called
 * from an invalid context. In addition, this error is always raised
 * when this service is called from asynchronous context, such as a
 * timer/alarm handler.
 *
 * @apitags{mode-unrestricted, switch-secondary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 */
int rt_task_delete(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	if (threadobj_irq_p())
		return -EPERM;

	tcb = find_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		return ret;

	CANCEL_DEFER(svc);
	threadobj_lock(&tcb->thobj);
	/* Self-deletion is handled by threadobj_cancel(). */
	threadobj_cancel(&tcb->thobj);
	CANCEL_RESTORE(svc);

	return 0;
}

/**
 * @fn int rt_task_join(RT_TASK *task)
 * @brief Wait on the termination of a real-time task.
 *
 * This service blocks the caller in non-real-time context until @a
 * task has terminated. All resources are released after successful
 * completion of this service.
 *
 * The specified task must have been created by the same process that
 * wants to join it, and the T_JOINABLE mode flag must have been set
 * on creation to rt_task_create().
 *
 * Tasks created with the T_JOINABLE flag shall be joined by a
 * subsequent call to rt_task_join() once successfully deleted, to
 * reclaim all resources.
 *
 * @param task The task descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * - -EINVAL is returned if the task was not created with T_JOINABLE
 * set or some other task is already waiting on the termination.
 *
 * - -EDEADLK is returned if @a task refers to the caller.
 *
 * - -ESRCH is returned if @a task no longer exists or refers to task
 * created by a different process.
 *
 * @apitags{mode-unrestricted, switch-primary}
 *
 * @note After successful completion of this service, it is neither
 * required nor valid to additionally invoke rt_task_delete() on the
 * same task.
 */
int rt_task_join(RT_TASK *task)
{
	if (bad_pointer(task))
		return -EINVAL;

	return -__RT(pthread_join(task->thread, NULL));
}

/**
 * @fn int rt_task_set_affinity(RT_TASK *task, const cpu_set_t *cpus)
 * @brief Set CPU affinity of real-time task.
 *
 * This calls makes @a task affine to the set of CPUs defined by @a
 * cpus.
 *
 * @param task The task descriptor.  If @a task is NULL, the CPU
 * affinity of the current task is changed.
 *
 * @param cpus The set of CPUs @a task should be affine to.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is NULL but the caller is not a
 * Xenomai task, or if @a task is non-NULL but not a valid task
 * descriptor.
 *
 * - -EINVAL is returned if @a cpus contains no processors that are
 * currently physically on the system and permitted to the process
 * according to any restrictions that may be imposed by the "cpuset"
 * mechanism described in cpuset(7).
 *
 * @apitags{mode-unrestricted, switch-secondary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 */
int rt_task_set_affinity(RT_TASK *task, const cpu_set_t *cpus)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	tcb->affinity = *cpus;

	ret = sched_setaffinity(threadobj_get_pid(&tcb->thobj),
				sizeof(tcb->affinity), &tcb->affinity);
	if (ret)
		ret = -errno;

	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_start(RT_TASK *task, void (*entry)(void *arg), void *arg)
 * @brief Start a real-time task.
 *
 * This call starts execution of a task previously created by
 * rt_task_create(). This service causes the started task to leave the
 * initial dormant state.
 *
 * @param task The task descriptor.
 *
 * @param entry The address of the task entry point.
 *
 * @param arg A user-defined opaque argument @a entry will receive.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * @apitags{mode-unrestricted, switch-primary}
 *
 * @note Starting an already started task leads to a nop, returning a
 * success status.
 */
int rt_task_start(RT_TASK *task,
		  void (*entry)(void *arg),
		  void *arg)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		goto out;

	tcb->entry = entry;
	tcb->arg = arg;
	ret = threadobj_start(&tcb->thobj);
	if (ret == -EIDRM)
		/*
		 * The started thread has run then exited, tcb->thobj
		 * is stale: don't touch it anymore.
		 */
		ret = 0;
	else
		put_alchemy_task(tcb);
out:
	CANCEL_DEFER(svc);

	return ret;
}

/**
 * @fn int rt_task_shadow(RT_TASK *task, const char *name, int prio, int mode)
 * @brief Turn caller into a real-time task.
 *
 * Set the calling thread personality to the Alchemy API, enabling the
 * full set of Alchemy services. Upon success, the caller is no more a
 * regular POSIX thread, but a Xenomai-extended thread.
 *
 * If @a prio is non-zero, the new task moves to Xenomai's real-time
 * FIFO scheduling class, aka SCHED_FIFO. If @a prio is zero, the task
 * moves to the regular SCHED_OTHER class.
 *
 * Running Xenomai tasks with zero priority is useful for running non
 * real-time processes which may invoke blocking real-time services,
 * such as pending on a semaphore, reading from a message queue or a
 * buffer, and so on.
 *
 * @param task If non-NULL, the address of a task descriptor which can
 * be later used to identify uniquely the task, upon success of this
 * call. If NULL, no descriptor is returned.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, a copy of this string is
 * used for indexing the task into the object registry.
 *
 * @param prio The base priority of the task. This value must be in
 * the [0 .. 99] range, where 0 is the lowest effective priority.
 *
 * @param mode The task shadowing mode. The following flags can be
 * OR'ed into this bitmask:
 *
 * - T_LOCK causes the current task to lock the scheduler before
 * returning to the caller, preventing all further involuntary task
 * switches on the current CPU. A call to rt_task_set_mode() from the
 * current task is required to drop this lock.
 *
 * - When running over the Cobalt core, T_WARNSW causes the SIGDEBUG
 * signal to be sent to the current task whenever it switches to the
 * secondary mode. This feature is useful to detect unwanted
 * migrations to the Linux domain. This flag has no effect over the
 * Mercury core.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a prio is invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the task extension.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered task.
 *
 * - -EBUSY is returned if the caller is not a regular POSIX thread.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt handler.
 *
 * @apitags{pthread-only, switch-secondary, switch-primary}
 *
 * @sideeffect Over Cobalt, if the caller is a plain POSIX thread, it
 * is turned into a Xenomai _shadow_ thread, with full access to all
 * Cobalt services. The caller always returns from this service in
 * primary mode.
 *
 * @note Tasks can be referred to from multiple processes which all
 * belong to the same Xenomai session.
 */
int rt_task_shadow(RT_TASK *task, const char *name, int prio, int mode)
{
	struct threadobj *current = threadobj_current();
	struct sched_param_ex param_ex;
	struct alchemy_task *tcb;
	struct service svc;
	int policy, ret;
	pthread_t self;

	if (mode & ~(T_LOCK | T_WARNSW))
		return -EINVAL;

	CANCEL_DEFER(svc);

	/*
	 * This is ok to overlay the default TCB for the main thread
	 * assigned by Copperplate at init, but it is not to
	 * over-shadow a Xenomai thread. A valid TCB pointer with a
	 * zero magic identifies the default main TCB.
	 */
	if (current && threadobj_get_magic(current))
		return -EBUSY;

	/*
	 * Over Cobalt, the following call turns the current context
	 * into a dual-kernel thread. Do this early, since this will
	 * be required next for creating the TCB and running the
	 * prologue code (i.e. real-time mutexes and monitors are
	 * locked there).
	 */
	self = pthread_self();
	policy = prio ? SCHED_FIFO : SCHED_OTHER;
	param_ex.sched_priority = prio;
	ret = __bt(copperplate_renice_local_thread(self, policy, &param_ex));
	if (ret)
		goto out;

	ret = create_tcb(&tcb, task, name, prio, mode);
	if (ret)
		goto out;

	CANCEL_RESTORE(svc);

	if (task)
		task->thread = self;

	ret = threadobj_shadow(&tcb->thobj, tcb->name);
	if (ret)
		goto undo;

	CANCEL_DEFER(svc);

	ret = task_prologue_2(tcb);
	if (ret)
		goto undo;
out:
	CANCEL_RESTORE(svc);

	return ret;
undo:
	delete_tcb(tcb);
	goto out;
}

/**
 * @fn int rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
 * @brief Make a real-time task periodic.
 *
 * Make a task periodic by programing its first release point and its
 * period in the processor time line.  @a task should then call
 * rt_task_wait_period() to sleep until the next periodic release
 * point in the processor timeline is reached.
 *
 * @param task The task descriptor.  If @a task is NULL, the current
 * task is made periodic. @a task must belong the current process.
 *
 * @param idate The initial (absolute) date of the first release
 * point, expressed in clock ticks (see note).  If @a idate is equal
 * to TM_NOW, the current system date is used.
 *
 * @param period The period of the task, expressed in clock ticks (see
 * note). Passing TM_INFINITE stops the task's periodic timer if
 * enabled, then returns successfully.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is NULL but the caller is not a
 * Xenomai task, or if @a task is non-NULL but not a valid task
 * descriptor.
 *
 * - -ETIMEDOUT is returned if @a idate is different from TM_INFINITE
 * and represents a date in the past.
 *
 * @apitags{mode-unrestricted, switch-primary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 *
 * @note Over Cobalt, -EINVAL is returned if @a period is
 * different from TM_INFINITE but shorter than the user scheduling
 * latency value for the target system, as displayed by
 * /proc/xenomai/latency.
 *
 * @note The @a idate and @a period values are interpreted as a
 * multiple of the Alchemy clock resolution (see
 * --alchemy-clock-resolution option, defaults to 1 nanosecond).
 *
 * @attention Unlike its Xenomai 2.x counterpart,
 * rt_task_set_periodic() will @b NOT block @a task until @a idate is
 * reached. The first beat in the periodic timeline should be awaited
 * for by a call to rt_task_wait_period().
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_task_set_periodic,
	     (RT_TASK *task, RTIME idate, RTIME period))
#else
int rt_task_set_periodic(RT_TASK *task, RTIME idate, RTIME period)
#endif
{
	struct timespec its, pts, now;
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	if (period == TM_INFINITE) {
		pts.tv_sec = 0;
		pts.tv_nsec = 0;
		its = pts;
	} else {
		clockobj_ticks_to_timespec(&alchemy_clock, period, &pts);
		if (idate == TM_NOW) {
			__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
			timespec_add(&its, &now, &pts);
		} else
			/*
			 * idate is an absolute time specification
			 * already, so we want a direct conversion to
			 * timespec.
			 */
			clockobj_ticks_to_timespec(&alchemy_clock, idate, &its);
	}

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	if (!threadobj_local_p(&tcb->thobj)) {
		ret = -EINVAL;
		goto out;
	}

	ret = threadobj_set_periodic(&tcb->thobj, &its, &pts);
	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_wait_period(unsigned long *overruns_r)
 * @brief Wait for the next periodic release point.
 *
 * Delay the current task until the next periodic release point is
 * reached. The periodic timer should have been previously started for
 * @a task by a call to rt_task_set_periodic().
 *
 * @param overruns_r If non-NULL, @a overruns_r shall be a pointer to
 * a memory location which will be written with the count of pending
 * overruns. This value is written to only when rt_task_wait_period()
 * returns -ETIMEDOUT or success. The memory location remains
 * unmodified otherwise. If NULL, this count will not be returned.
 *
 * @return Zero is returned upon success. If @a overruns_r is
 * non-NULL, zero is written to the pointed memory
 * location. Otherwise:
 *
 * - -EWOULDBLOCK is returned if rt_task_set_periodic() was not called
 * for the current task.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * waiting task before the next periodic release point was reached. In
 * this case, the overrun counter is also cleared.
 *
 * - -ETIMEDOUT is returned if a timer overrun occurred, which
 * indicates that a previous release point was missed by the calling
 * task. If @a overruns_r is non-NULL, the count of pending overruns
 * is written to the pointed memory location.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * @apitags{xthread-only, switch-primary}
 *
 * @note If the current release point has already been reached at the
 * time of the call, the current task immediately returns from this
 * service with no delay.
 */
int rt_task_wait_period(unsigned long *overruns_r)
{
	if (!threadobj_current_p())
		return -EPERM;

	return threadobj_wait_period(overruns_r);
}

/**
 * @fn int rt_task_sleep_until(RTIME date)
 * @brief Delay the current real-time task (with absolute wakeup date).
 *
 * Delay the execution of the calling task until a given date is
 * reached. The caller is put to sleep, and does not consume any CPU
 * time in such a state.
 *
 * @param date An absolute date expressed in clock ticks, specifying a
 * wakeup date (see note). As a special case, TM_INFINITE is an
 * acceptable value that causes the caller to block indefinitely,
 * until rt_task_unblock() is called against it. Otherwise, any wake
 * up date in the past causes the task to return immediately.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task.
 *
 * - -ETIMEDOUT is returned if @a date has already elapsed.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * @apitags{xthread-only, switch-primary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 *
 * @note The @a date value is interpreted as a multiple of the Alchemy
 * clock resolution (see --alchemy-clock-resolution option, defaults
 * to 1 nanosecond).
 */
int rt_task_sleep_until(RTIME date)
{
	struct timespec ts;
	struct service svc;
	ticks_t now;

	if (!threadobj_current_p())
		return -EPERM;

	if (date == TM_INFINITE)
		ts = zero_time;
	else {
		now = clockobj_get_time(&alchemy_clock);
		if (date <= now)
			return -ETIMEDOUT;
		CANCEL_DEFER(svc);
		clockobj_ticks_to_timespec(&alchemy_clock, date, &ts);
		CANCEL_RESTORE(svc);
	}

	return threadobj_sleep(&ts);
}

/**
 * @fn int rt_task_sleep(RTIME delay)
 * @brief Delay the current real-time task (with relative delay).
 *
 * This routine is a variant of rt_task_sleep_until() accepting a
 * relative timeout specification.
 *
 * @param delay A relative delay expressed in clock ticks (see
 * note). A zero delay causes this service to return immediately to
 * the caller with a success status.
 *
 * @return See rt_task_sleep_until().
 *
 * @apitags{xthread-only, switch-primary}
 *
 * @note The @a delay value is interpreted as a multiple of the
 * Alchemy clock resolution (see --alchemy-clock-resolution option,
 * defaults to 1 nanosecond).
 */
int rt_task_sleep(RTIME delay)
{
	struct timespec ts;
	struct service svc;

	if (!threadobj_current_p())
		return -EPERM;

	if (delay == 0)
		return 0;

	CANCEL_DEFER(svc);
	clockobj_ticks_to_timeout(&alchemy_clock, delay, &ts);
	CANCEL_RESTORE(svc);

	return threadobj_sleep(&ts);
}

/**
 * @fn int rt_task_spawn(RT_TASK *task, const char *name, int stksize, int prio, int mode, void (*entry)(void *arg), void *arg)
 * @brief Create and start a real-time task.
 *
 * This service spawns a task by combining calls to rt_task_create()
 * and rt_task_start() for the new task.
 *
 * @param task The address of a task descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, a copy of this string is
 * used for indexing the created task into the object registry.
 *
 * @param stksize The size of the stack (in bytes) for the new
 * task. If zero is passed, a system-dependent default size will be
 * substituted.
 *
 * @param prio The base priority of the new task. This value must be
 * in the [0 .. 99] range, where 0 is the lowest effective priority. 
 *
 * @param mode The task creation mode. See rt_task_create().
 *
 * @param entry The address of the task entry point.
 *
 * @param arg A user-defined opaque argument @a entry will receive.
 *
 * @return See rt_task_create().
 *
 * @apitags{mode-unrestricted, switch-secondary}
 *
 * @sideeffect see rt_task_create().
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_task_spawn, (RT_TASK *task, const char *name,
				  int stksize, int prio, int mode,
				  void (*entry)(void *arg),
				  void *arg))
#else
int rt_task_spawn(RT_TASK *task, const char *name,
		  int stksize, int prio, int mode,
		  void (*entry)(void *arg),
		  void *arg)
#endif
{
	int ret;

	ret = rt_task_create(task, name, stksize, prio, mode);
	if (ret)
		return ret;

	return rt_task_start(task, entry, arg);
}

/**
 * @fn int rt_task_same(RT_TASK *task1, RT_TASK *task2)
 * @brief Compare real-time task descriptors.
 *
 * This predicate returns true if @a task1 and @a task2 refer to the
 * same task.
 *
 * @param task1 First task descriptor to compare.
 *
 * @param task2 Second task descriptor to compare.
 *
 * @return A non-zero value is returned if both descriptors refer to
 * the same task, zero otherwise.
 *
 * @apitags{unrestricted}
 */
int rt_task_same(RT_TASK *task1, RT_TASK *task2)
{
	return task1->handle == task2->handle;
}

/**
 * @fn int rt_task_suspend(RT_TASK *task)
 * @brief Suspend a real-time task.
 *
 * Forcibly suspend the execution of a task. This task will not be
 * eligible for scheduling until it is explicitly resumed by a call to
 * rt_task_resume(). In other words, the suspended state caused by a
 * call to rt_task_suspend() is cumulative with respect to the delayed
 * and blocked states caused by other services, and is managed
 * separately from them.
 *
 * A nesting count is maintained so that rt_task_suspend() and
 * rt_task_resume() must be used in pairs.
 *
 * Receiving a Linux signal causes the suspended task to resume
 * immediately.
 *
 * @param task The task descriptor. If @a task is NULL, the current
 * task is suspended.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is NULL but the caller is not a
 * Xenomai task, or if @a task is non-NULL but not a valid task
 * descriptor.
 *
 * - -EINTR is returned if a Linux signal has been received by the
 * caller if suspended.
 *
 * - -EPERM is returned if @a task is NULL and this service was called
 * from an invalid context.
 *
 * @apitags{mode-unrestricted, switch-primary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 *
 * @note Blocked and suspended task states are cumulative. Therefore,
 * suspending a task currently waiting on a synchronization object
 * (e.g. semaphore, queue) holds its execution until it is resumed,
 * despite the awaited resource may have been acquired, or a timeout
 * has elapsed in the meantime.
 */
int rt_task_suspend(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	if (tcb->suspends++ == 0)
		ret = threadobj_suspend(&tcb->thobj);

	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_resume(RT_TASK *task)
 * @brief Resume a real-time task.
 *
 * Forcibly resume the execution of a task which was previously
 * suspended by a call to rt_task_suspend(), if the suspend nesting
 * count decrements to zero.
 *
 * @param task The task descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 *
 * @note Blocked and suspended task states are cumulative. Therefore,
 * resuming a task currently waiting on a synchronization object
 * (e.g. semaphore, queue) does not make it eligible for scheduling
 * until the awaited resource is eventually acquired, or a timeout
 * elapses.
 */
int rt_task_resume(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		goto out;

	if (tcb->suspends > 0 && --tcb->suspends == 0)
		ret = threadobj_resume(&tcb->thobj);

	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn RT_TASK *rt_task_self(void)
 * @brief Retrieve the current task descriptor.
 *
 * Return the address of the current Alchemy task descriptor.
 *
 * @return The address of the task descriptor referring to the current
 * Alchemy task is returned upon success, or NULL if not called from a
 * valid Alchemy task context.
 *
 * @apitags{xthread-only}
 */
RT_TASK *rt_task_self(void)
{
	struct alchemy_task *tcb;

	tcb = alchemy_task_current();
	if (tcb == NULL)
		return NULL;

	return &tcb->self;
}

/**
 * @fn int rt_task_set_priority(RT_TASK *task, int prio)
 * @brief Change the base priority of a real-time task.
 *
 * The base priority of a task defines the relative importance of the
 * work being done by each task, which gains conrol of the CPU
 * accordingly.
 *
 * Changing the base priority of a task does not affect the priority
 * boost the target task might have obtained as a consequence of a
 * priority inheritance undergoing.
 *
 * @param task The task descriptor. If @a task is NULL, the priority
 * of the current task is changed.
 *
 * @param prio The new priority. This value must range from [T_LOPRIO
 * .. T_HIPRIO] (inclusive) where T_LOPRIO is the lowest effective
 * priority.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor, or
 * if @a prio is invalid.
 *
 * - -EPERM is returned if @a task is NULL and this service was called
 * from an invalid context.
 *
 * @apitags{mode-unrestricted, switch-primary, switch-secondary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 *
 * @note Assigning the same priority to a running or ready task moves
 * it to the end of its priority group, thus causing a manual
 * round-robin.
 */
int rt_task_set_priority(RT_TASK *task, int prio)
{
	struct sched_param_ex param_ex;
	struct alchemy_task *tcb;
	struct service svc;
	int policy, ret;

	ret = check_task_priority(prio);
	if (ret)
		return ret;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	policy = prio ? SCHED_FIFO : SCHED_OTHER;
	param_ex.sched_priority = prio;
	ret = threadobj_set_schedparam(&tcb->thobj, policy, &param_ex);
	switch (ret) {
	case -EIDRM:
		ret = 0;
		break;
	default:
		put_alchemy_task(tcb);
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_yield(void)
 * @brief Manual round-robin.
 *
 * Move the current task to the end of its priority group, so that the
 * next equal-priority task in ready state is switched in.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * @apitags{xthread-only, switch-primary}
 */
int rt_task_yield(void)
{
	if (!threadobj_current_p())
		return -EPERM;

	threadobj_yield();

	return 0;
}

/**
 * @fn int rt_task_unblock(RT_TASK *task)
 * @brief Unblock a real-time task.
 *
 * Break the task out of any wait it is currently in.  This call
 * clears all delay and/or resource wait condition for the target
 * task.
 *
 * However, rt_task_unblock() does not resume a task which has been
 * forcibly suspended by a previous call to rt_task_suspend().  If all
 * suspensive conditions are gone, the task becomes eligible anew for
 * scheduling.
 *
 * @param task The task descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
int rt_task_unblock(RT_TASK *task)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_unblock(&tcb->thobj);
	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_slice(RT_TASK *task, RTIME quantum)
 * @brief Set a task's round-robin quantum.
 *
 * Set the time credit allotted to a task undergoing the round-robin
 * scheduling. If @a quantum is non-zero, rt_task_slice() also refills
 * the current quantum for the target task, otherwise, time-slicing is
 * stopped for that task.
 *
 * In other words, rt_task_slice() should be used to toggle
 * round-robin scheduling for an Alchemy task.
 *
 * @param task The task descriptor. If @a task is NULL, the time
 * credit of the current task is changed. @a task must belong to the
 * current process.
 *
 * @param quantum The round-robin quantum for the task expressed in
 * clock ticks (see note).
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor, or
 * if @a prio is invalid.
 *
 * - -EPERM is returned if @a task is NULL and this service was called
 * from an invalid context.
 *
 * @apitags{mode-unrestricted, switch-primary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 *
 * @note The @a quantum value is interpreted as a multiple of the
 * Alchemy clock resolution (see --alchemy-clock-resolution option,
 * defaults to 1 nanosecond).
 */
int rt_task_slice(RT_TASK *task, RTIME quantum)
{
	struct sched_param_ex param_ex;
	struct alchemy_task *tcb;
	struct service svc;
	int ret, policy;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	param_ex.sched_priority = threadobj_get_priority(&tcb->thobj);
	if (quantum) {
		struct timespec ts;
		policy = SCHED_RR;
		clockobj_ticks_to_timespec(&alchemy_clock, quantum, &ts);
		param_ex.sched_rr_quantum.tv_sec = ts.tv_sec;
		param_ex.sched_rr_quantum.tv_nsec = ts.tv_nsec;
	} else {
		policy = param_ex.sched_priority ? SCHED_FIFO : SCHED_OTHER;
	}

	ret = threadobj_set_schedparam(&tcb->thobj, policy, &param_ex);
	switch (ret) {
	case -EIDRM:
		ret = 0;
		break;
	default:
		put_alchemy_task(tcb);
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_set_mode(int clrmask, int setmask, int *mode_r)
 * @brief Change the current task mode.
 *
 * Each Alchemy task has a set of internal flags determining several
 * operating conditions. rt_task_set_mode() takes a bitmask of mode
 * bits to clear for disabling the corresponding modes for the current
 * task, and another one to set for enabling them. The mode bits which
 * were previously in effect before the change can be returned upon
 * request.
 *
 * The following bits can be part of the bitmask:
 *
 * - T_LOCK causes the current task to lock the scheduler on the
 * current CPU, preventing all further involuntary task switches on
 * this CPU. Clearing this bit unlocks the scheduler.
 *
 * - Only when running over the Cobalt core:
 *
 *   - T_WARNSW causes the SIGDEBUG signal to be sent to the current
 * task whenever it switches to the secondary mode. This feature is
 * useful to detect unwanted migrations to the Linux domain.
 *
 *   - T_CONFORMING can be passed in @a setmask to switch the current
 * Alchemy task to its preferred runtime mode. The only meaningful use
 * of this switch is to force a real-time task back to primary
 * mode (see note). Any other use leads to a nop.
 *
 * These two last flags have no effect over the Mercury core, and are
 * simply ignored.
 *
 * @param clrmask A bitmask of mode bits to clear for the current
 * task, before @a setmask is applied. Zero is an acceptable value
 * which leads to a no-op.
 *
 * @param setmask A bitmask of mode bits to set for the current
 * task. Zero is an acceptable value which leads to a no-op.
 *
 * @param mode_r If non-NULL, @a mode_r must be a pointer to a memory
 * location which will be written upon success with the previous set
 * of active mode bits. If NULL, the previous set of active mode bits
 * will not be returned.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor, or
 * if any bit from @a clrmask or @a setmask is invalid.

 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * @apitags{xthread-only, switch-primary}
 *
 * @note The caller must be an Alchemy task.
 *
 * @note Forcing the task mode using the T_CONFORMING bit from user
 * code is almost always wrong, since the Xenomai/cobalt core handles
 * mode switches internally when/if required. Most often, manual mode
 * switching from applications introduces useless overhead. This mode
 * bit is part of the API only to cover rare use cases in middleware
 * code based on the Alchemy interface.
 */
int rt_task_set_mode(int clrmask, int setmask, int *mode_r)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p()) {
		clrmask &= ~T_LOCK;
		setmask &= ~T_LOCK;
		return (clrmask | setmask) ? -EPERM : 0;
	}

	if (((clrmask | setmask) & ~(T_LOCK | T_WARNSW | T_CONFORMING)) != 0)
		return -EINVAL;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task_or_self(NULL, &ret);
	if (tcb == NULL)
		goto out;

	ret = threadobj_set_mode(clrmask, setmask, mode_r);
	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_inquire(RT_TASK *task, RT_TASK_INFO *info)
 * @brief Retrieve information about a real-time task.
 *
 * Return various information about an Alchemy task. This service may
 * also be used to probe for task existence.
 *
 * @param task The task descriptor. If @a task is NULL, the
 * information about the current task is returned.
 *
 * @param info  The address of a structure the task information will be
 * written to. Passing NULL is valid, in which case the system is only
 * probed for existence of the specified task.
 *
 * @return Zero is returned if the task exists. In addition, if @a
 * info is non-NULL, it is filled in with task information.
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor, or
 * if @a prio is invalid.
 *
 * - -EPERM is returned if @a task is NULL and this service was called
 * from an invalid context.
 *
 * @apitags{mode-unrestricted, switch-primary}
 *
 * @note The caller must be an Alchemy task if @a task is NULL.
 */
int rt_task_inquire(RT_TASK *task, RT_TASK_INFO *info)
{
	struct alchemy_task *tcb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	tcb = get_alchemy_task_or_self(task, &ret);
	if (tcb == NULL)
		goto out;

	ret = __bt(threadobj_stat(&tcb->thobj, &info->stat));
	if (ret)
		goto out;

	strcpy(info->name, tcb->name);
	info->prio = threadobj_get_priority(&tcb->thobj);
	info->pid = threadobj_get_pid(&tcb->thobj);

	put_alchemy_task(tcb);
out:
	CANCEL_RESTORE(svc);

	return ret;
}


/**
 * @fn ssize_t rt_task_send(RT_TASK *task, RT_TASK_MCB *mcb_s, RT_TASK_MCB *mcb_r, RTIME timeout)
 * @brief Send a message to a real-time task (with relative scalar timeout).
 *
 * This routine is a variant of rt_task_send_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param task The task descriptor.
 *
 * @param mcb_s The address of the message control block referring to
 * the message to be sent.
 *
 * @param mcb_r The address of an optional message control block
 * referring to the reply message area.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE causes the caller to block indefinitely until a reply
 * is received. Passing TM_NONBLOCK causes the service to return
 * without blocking in case the recipient task is not waiting for
 * messages at the time of the call.
 *
 * @apitags{xthread-only, switch-primary}
 */

/**
 * @fn ssize_t rt_task_send_until(RT_TASK *task, RT_TASK_MCB *mcb_s, RT_TASK_MCB *mcb_r, RTIME abs_timeout)
 * @brief Send a message to a real-time task (with absolute scalar timeout).
 *
 * This routine is a variant of rt_task_send_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param task The task descriptor.
 *
 * @param mcb_s The address of the message control block referring to
 * the message to be sent.
 *
 * @param mcb_r The address of an optional message control block
 * referring to the reply message area.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * a reply is received. Passing TM_NONBLOCK causes the service to
 * return without blocking in case the recipient task is not waiting
 * for messages at the time of the call.
 *
 * @apitags{xthread-only, switch-primary}
 */

/**
 * @fn ssize_t rt_task_send_timed(RT_TASK *task, RT_TASK_MCB *mcb_s, RT_TASK_MCB *mcb_r, const struct timespec *abs_timeout)
 * @brief Send a message to a real-time task.
 *
 * This service is part of the synchronous message passing support
 * available to Alchemy tasks. The caller sends a variable-sized
 * message to another task, waiting for the remote to receive the
 * initial message by a call to rt_task_receive(), then reply to it
 * using rt_task_reply().
 *
 * A basic message control block is used to store the location and
 * size of the data area to send or retrieve upon reply, in addition
 * to a user-defined operation code.
 *
 * @param task The task descriptor.
 *
 * @param mcb_s The address of the message control block referring to
 * the message to be sent. The fields from this control block should
 * be set as follows:
 *
 * - mcb_s->data should contain the address of the payload data to
 * send to the remote task.
 *
 * - mcb_s->size should contain the size in bytes of the payload data
 * pointed at by mcb_s->data. Zero is a legitimate value, and
 * indicates that no payload data will be transferred. In the latter
 * case, mcb_s->data will be ignored.
 *
 * - mcb_s->opcode is an opaque operation code carried during the
 * message transfer, the caller can fill with any appropriate
 * value. It will be made available "as is" to the remote task into
 * the operation code field by the rt_task_receive() service.
 *
 * @param mcb_r The address of an optional message control block
 * referring to the reply message area. If @a mcb_r is NULL and a
 * reply is sent back by the remote task, the reply message will be
 * discarded, and -ENOBUFS will be returned to the caller. When @a
 * mcb_r is valid, the fields from this control block should be set as
 * follows:
 *
 * - mcb_r->data should contain the address of a buffer large enough
 * to collect the reply data from the remote task.
 *
 * - mcb_r->size should contain the size in bytes of the buffer space
 * pointed at by mcb_r->data. If mcb_r->size is lower than the actual
 * size of the reply message, no data copy takes place and -ENOBUFS is
 * returned to the caller.
 *
 * Upon return, mcb_r->opcode will contain the status code sent back
 * from the remote task using rt_task_reply(), or zero if unspecified.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for the
 * recipient task to reply to the initial message. Passing NULL causes
 * the caller to block indefinitely until a reply is received. Passing
 * { .tv_sec = 0, .tv_nsec = 0 } causes the service to return without
 * blocking in case the recipient task is not waiting for messages at
 * the time of the call.
 *
 * @return A positive value is returned upon success, representing the
 * length (in bytes) of the reply message returned by the remote
 * task. Zero is a success status, meaning either that @a mcb_r was
 * NULL on entry, or that no actual message was passed to the remote
 * call to rt_task_reply(). Otherwise:
 *
 * - -EINVAL is returned if @a task is not a valid task descriptor.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * - -ENOBUFS is returned if @a mcb_r does not point at a message area
 * large enough to collect the remote task's reply. This includes the
 * case where @a mcb_r is NULL on entry, despite the remote task
 * attempts to send a reply message.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and the recipient @a task is not currently waiting
 * for a message on the rt_task_receive() service.
 *
 * - -EIDRM is returned if @a task has been deleted while waiting for
 * a reply.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before any reply was received from the recipient @a
 * task.
 *
 * @apitags{xthread-only, switch-primary}
 */
ssize_t rt_task_send_timed(RT_TASK *task,
			   RT_TASK_MCB *mcb_s, RT_TASK_MCB *mcb_r,
			   const struct timespec *abs_timeout)
{
	void *rbufin = NULL, *rbufout = NULL;
	struct alchemy_task_wait *wait;
	struct threadobj *current;
	struct alchemy_task *tcb;
	struct syncstate syns;
	struct service svc;
	ssize_t ret;
	int err;

	current = threadobj_current();
	if (current == NULL)
		return -EPERM;

	CANCEL_DEFER(svc);

	tcb = find_alchemy_task(task, &err);
	if (tcb == NULL) {
		ret = err;
		goto out;
	}

	ret = syncobj_lock(&tcb->sobj_msg, &syns);
	if (ret)
		goto out;

	if (alchemy_poll_mode(abs_timeout)) {
		if (!syncobj_count_drain(&tcb->sobj_msg)) {
			ret = -EWOULDBLOCK;
			goto done;
		}
		abs_timeout = NULL;
	}

	/* Get space for the reply. */
	wait = threadobj_prepare_wait(struct alchemy_task_wait);

	/*
	 * Compute the next flow identifier, making sure that we won't
	 * draw a null or negative value.
	 */
	if (++tcb->flowgen < 0)
		tcb->flowgen = 1;

	wait->request = *mcb_s;
	/*
	 * Payloads exchanged with remote tasks have to go through the
	 * main heap.
	 */
	if (mcb_s->size > 0 && !threadobj_local_p(&tcb->thobj)) {
		rbufin = xnmalloc(mcb_s->size);
		if (rbufin == NULL) {
			ret = -ENOMEM;
			goto cleanup;
		}
		memcpy(rbufin, mcb_s->data, mcb_s->size);
		wait->request.__dref = __moff(rbufin);
	}
	wait->request.flowid = tcb->flowgen;
	if (mcb_r) {
		wait->reply.size = mcb_r->size;
		wait->reply.data = mcb_r->data;
		if (mcb_r->size > 0 && !threadobj_local_p(&tcb->thobj)) {
			rbufout = xnmalloc(mcb_r->size);
			if (rbufout == NULL) {
				ret = -ENOMEM;
				goto cleanup;
			}
			wait->reply.__dref = __moff(rbufout);
		}
	} else {
		wait->reply.data = NULL;
		wait->reply.size = 0;
	}

	if (syncobj_count_drain(&tcb->sobj_msg))
		syncobj_drain(&tcb->sobj_msg);

	ret = syncobj_wait_grant(&tcb->sobj_msg, abs_timeout, &syns);
	if (ret) {
		threadobj_finish_wait();
		if (ret == -EIDRM)
			goto out;
		goto done;
	}

	ret = wait->reply.size;
	if (!threadobj_local_p(&tcb->thobj) && ret > 0 && mcb_r)
		memcpy(mcb_r->data, rbufout, ret);
cleanup:
	threadobj_finish_wait();
done:
	syncobj_unlock(&tcb->sobj_msg, &syns);
out:
	if (rbufin)
		xnfree(rbufin);
	if (rbufout)
		xnfree(rbufout);
	
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn ssize_t rt_task_receive(RT_TASK_MCB *mcb_r, RTIME timeout)
 * @brief Receive a message from a real-time task (with relative scalar timeout).
 *
 * This routine is a variant of rt_task_receive_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param mcb_r The address of a message control block referring to
 * the receive message area.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE causes the caller to block indefinitely until a remote
 * task eventually sends a message.Passing TM_NONBLOCK causes the
 * service to return immediately without waiting if no remote task is
 * currently waiting for sending a message.
 *
 * @apitags{xthread-only, switch-primary}
 */

/**
 * @fn ssize_t rt_task_receive_until(RT_TASK_MCB *mcb_r, RTIME abs_timeout)
 * @brief Receive a message from a real-time task (with absolute scalar timeout).
 *
 * This routine is a variant of rt_task_receive_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param mcb_r The address of a message control block referring to
 * the receive message area.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * a remote task eventually sends a message.Passing TM_NONBLOCK
 * causes the service to return immediately without waiting if no
 * remote task is currently waiting for sending a message.
 *
 * @apitags{xthread-only, switch-primary}
 */

/**
 * @fn ssize_t rt_task_receive_timed(RT_TASK_MCB *mcb_r, const struct timespec *abs_timeout)
 * @brief Receive a message from a real-time task.
 *
 * This service is part of the synchronous message passing support
 * available to Alchemy tasks. The caller receives a variable-sized
 * message from another task. The sender is blocked until the caller
 * invokes rt_task_reply() to finish the transaction.
 *
 * A basic message control block is used to store the location and
 * size of the data area to receive from the client, in addition to a
 * user-defined operation code.
 *
 * @param mcb_r The address of a message control block referring to
 * the receive message area. The fields from this control block should
 * be set as follows:
 *
 * - mcb_r->data should contain the address of a buffer large enough
 * to collect the data sent by the remote task;
 *
 * - mcb_r->size should contain the size in bytes of the buffer space
 * pointed at by mcb_r->data. If mcb_r->size is lower than the actual
 * size of the received message, no data copy takes place and -ENOBUFS
 * is returned to the caller. See note.
 *
 * Upon return, mcb_r->opcode will contain the operation code sent
 * from the remote task using rt_task_send().
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying the time limit to wait for
 * receiving a message. Passing NULL causes the caller to block
 * indefinitely until a remote task eventually sends a message.
 * Passing { .tv_sec = 0, .tv_nsec = 0 } causes the service to return
 * immediately without waiting if no remote task is currently waiting
 * for sending a message.
 *
 * @return A strictly positive value is returned upon success,
 * representing a flow identifier for the opening transaction; this
 * token should be passed to rt_task_reply(), in order to send back a
 * reply to and unblock the remote task appropriately. Otherwise:
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before a message was received.
 *
 * - -ENOBUFS is returned if @a mcb_r does not point at a message area
 * large enough to collect the remote task's message.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and no remote task is currently waiting for sending
 * a message to the caller.
 *
 * - -ETIMEDOUT is returned if no message was received within the @a
 * timeout.
 *
 * @apitags{xthread-only, switch-primary}
 */
int rt_task_receive_timed(RT_TASK_MCB *mcb_r,
			  const struct timespec *abs_timeout)
{
	struct alchemy_task_wait *wait;
	struct alchemy_task *current;
	struct threadobj *thobj;
	struct syncstate syns;
	struct service svc;
	RT_TASK_MCB *mcb_s;
	int ret;

	current = alchemy_task_current();
	if (current == NULL)
		return -EPERM;

	CANCEL_DEFER(svc);

	ret = syncobj_lock(&current->sobj_msg, &syns);
	if (ret)
		goto out;

	while (!syncobj_grant_wait_p(&current->sobj_msg)) {
		if (alchemy_poll_mode(abs_timeout)) {
			ret = -EWOULDBLOCK;
			goto done;
		}
		ret = syncobj_wait_drain(&current->sobj_msg, abs_timeout, &syns);
		if (ret)
			goto done;
	}

	thobj = syncobj_peek_grant(&current->sobj_msg);
	wait = threadobj_get_wait(thobj);
	mcb_s = &wait->request;

	if (mcb_s->size > mcb_r->size) {
		ret = -ENOBUFS;
		goto fixup;
	}

	if (mcb_s->size > 0) {
		if (!threadobj_local_p(thobj))
			memcpy(mcb_r->data, __mptr(mcb_s->__dref), mcb_s->size);
		else
			memcpy(mcb_r->data, mcb_s->data, mcb_s->size);
	}

	/* The flow identifier is always strictly positive. */
	ret = mcb_s->flowid;
	mcb_r->opcode = mcb_s->opcode;
fixup:
	mcb_r->size = mcb_s->size;
done:
	syncobj_unlock(&current->sobj_msg, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_reply(int flowid, RT_TASK_MCB *mcb_s)
 * @brief Reply to a remote task message.
 *
 * This service is part of the synchronous message passing support
 * available to Alchemy tasks. The caller sends a variable-sized
 * message back to a remote task, in response to this task's initial
 * message received by a call to rt_task_receive(). As a consequence
 * of calling rt_task_reply(), the remote task will be unblocked from
 * the rt_task_send() service.
 *
 * A basic message control block is used to store the location and
 * size of the data area to send back, in addition to a user-defined
 * status code.
 *
 * @param flowid The flow identifier returned by a previous call to
 * rt_task_receive() which uniquely identifies the current
 * transaction.
 *
 * @param mcb_s The address of an optional message control block
 * referring to the message to be sent back. If @a mcb_s is NULL, the
 * remote will be unblocked without getting any reply data. When @a
 * mcb_s is valid, the fields from this control block should be set as
 * follows:
 *
 * - mcb_s->data should contain the address of the payload data to
 * send to the remote task.
 *
 * - mcb_s->size should contain the size in bytes of the payload data
 * pointed at by mcb_s->data. Zero is a legitimate value, and
 * indicates that no payload data will be transferred. In the latter
 * case, mcb_s->data will be ignored.
 *
 * - mcb_s->opcode is an opaque status code carried during the message
 * transfer the caller can fill with any appropriate value. It will be
 * made available "as is" to the remote task into the status code
 * field by the rt_task_send() service. If @a mcb_s is NULL, Zero will
 * be returned to the remote task into the status code field.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a flowid is invalid.
 *
 * - -ENXIO is returned if @a flowid does not match the expected
 * identifier returned from the latest call of the current task to
 * rt_task_receive(), or if the remote task stopped waiting for the
 * reply in the meantime (e.g. the remote could have been deleted or
 * forcibly unblocked).
 *
 * - -ENOBUFS is returned if the reply data referred to by @a mcb_s is
*  larger than the reply area mentioned by the remote task when
*  calling rt_task_send(). In such a case, the remote task also
*  receives -ENOBUFS on return from rt_task_send().
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * @apitags{xthread-only, switch-primary}
 */
int rt_task_reply(int flowid, RT_TASK_MCB *mcb_s)
{
	struct alchemy_task_wait *wait = NULL;
	struct alchemy_task *current;
	struct threadobj *thobj;
	struct syncstate syns;
	struct service svc;
	RT_TASK_MCB *mcb_r;
	size_t size;
	int ret;

	current = alchemy_task_current();
	if (current == NULL)
		return -EPERM;

	if (flowid <= 0)
		return -EINVAL;

	CANCEL_DEFER(svc);

	ret = __bt(syncobj_lock(&current->sobj_msg, &syns));
	if (ret)
		goto out;

	ret = -ENXIO;
	if (!syncobj_grant_wait_p(&current->sobj_msg))
		goto done;

	syncobj_for_each_grant_waiter(&current->sobj_msg, thobj) {
		wait = threadobj_get_wait(thobj);
		if (wait->request.flowid == flowid)
			goto reply;
	}
	goto done;
 reply:
	size = mcb_s ? mcb_s->size : 0;
	syncobj_grant_to(&current->sobj_msg, thobj);
	mcb_r = &wait->reply;

	/*
	 * NOTE: sending back a NULL or zero-length reply is perfectly
	 * valid; it just means to unblock the client without passing
	 * it back any reply data. Sending a response larger than what
	 * the client expects is invalid.
	 */
	if (mcb_r->size < size) {
		ret = -ENOBUFS;	/* Client will get this too. */
		mcb_r->size = -ENOBUFS;
	} else {
		ret = 0;
		mcb_r->size = size;
		if (size > 0) {
			if (!threadobj_local_p(thobj))
				memcpy(__mptr(mcb_r->__dref), mcb_s->data, size);
			else
				memcpy(mcb_r->data, mcb_s->data, size);
		}
	}

	mcb_r->flowid = flowid;
	mcb_r->opcode = mcb_s ? mcb_s->opcode : 0;
done:
	syncobj_unlock(&current->sobj_msg, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_task_bind(RT_TASK *task, const char *name, RTIME timeout)
 * @brief Bind to a task.
 *
 * This routine creates a new descriptor to refer to an existing
 * Alchemy task identified by its symbolic name. If the object does
 * not exist on entry, the caller may block until a task of the given
 * name is created.
 *
 * @param task The address of a task descriptor filled in by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param name A valid NULL-terminated name which identifies the task
 * to bind to. This string should match the object name argument
 * passed to rt_task_create(), or rt_task_shadow().
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
int rt_task_bind(RT_TASK *task,
		 const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_task_table,
				   timeout,
				   offsetof(struct alchemy_task, cobj),
				   &task->handle);
}

/**
 * @fn int rt_task_unbind(RT_TASK *task)
 * @brief Unbind from a task.
 *
 * @param task The task descriptor.
 *
 * This routine releases a previous binding to an Alchemy task. After
 * this call has returned, the descriptor is no more valid for
 * referencing this object.
 *
 * @apitags{thread-unrestricted}
 */
int rt_task_unbind(RT_TASK *task)
{
	*task = NO_ALCHEMY_TASK;
	return 0;
}

/** @} */

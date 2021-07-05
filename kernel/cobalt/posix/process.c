/*
 * Copyright (C) 2001-2014 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2001-2014 The Xenomai project <http://www.xenomai.org>
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * SMP support Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
 * RTAI/fusion Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <stdarg.h>
#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <pipeline/kevents.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/heap.h>
#include <cobalt/kernel/synch.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/trace.h>
#include <cobalt/kernel/stat.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/vdso.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/uapi/signal.h>
#include <cobalt/uapi/syscall.h>
#include <pipeline/sched.h>
#include <trace/events/cobalt-core.h>
#include <rtdm/driver.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/syscall.h>
#include "../debug.h"
#include "internal.h"
#include "thread.h"
#include "sched.h"
#include "mutex.h"
#include "cond.h"
#include "mqueue.h"
#include "sem.h"
#include "signal.h"
#include "timer.h"
#include "monitor.h"
#include "clock.h"
#include "event.h"
#include "timerfd.h"
#include "io.h"

static int gid_arg = -1;
module_param_named(allowed_group, gid_arg, int, 0644);

static DEFINE_MUTEX(personality_lock);

static struct hlist_head *process_hash;
DEFINE_PRIVATE_XNLOCK(process_hash_lock);
#define PROCESS_HASH_SIZE 13

struct xnthread_personality *cobalt_personalities[NR_PERSONALITIES];

static struct xnsynch yield_sync;

LIST_HEAD(cobalt_global_thread_list);

struct cobalt_resources cobalt_global_resources = {
	.condq = LIST_HEAD_INIT(cobalt_global_resources.condq),
	.mutexq = LIST_HEAD_INIT(cobalt_global_resources.mutexq),
	.semq = LIST_HEAD_INIT(cobalt_global_resources.semq),
	.monitorq = LIST_HEAD_INIT(cobalt_global_resources.monitorq),
	.eventq = LIST_HEAD_INIT(cobalt_global_resources.eventq),
	.schedq = LIST_HEAD_INIT(cobalt_global_resources.schedq),
};

static unsigned __attribute__((pure)) process_hash_crunch(struct mm_struct *mm)
{
	unsigned long hash = ((unsigned long)mm - PAGE_OFFSET) / sizeof(*mm);
	return hash % PROCESS_HASH_SIZE;
}

static struct cobalt_process *__process_hash_search(struct mm_struct *mm)
{
	unsigned int bucket = process_hash_crunch(mm);
	struct cobalt_process *p;

	hlist_for_each_entry(p, &process_hash[bucket], hlink)
		if (p->mm == mm)
			return p;
	
	return NULL;
}

static int process_hash_enter(struct cobalt_process *p)
{
	struct mm_struct *mm = current->mm;
	unsigned int bucket = process_hash_crunch(mm);
	int err;
	spl_t s;

	xnlock_get_irqsave(&process_hash_lock, s);
	if (__process_hash_search(mm)) {
		err = -EBUSY;
		goto out;
	}

	p->mm = mm;
	hlist_add_head(&p->hlink, &process_hash[bucket]);
	err = 0;
  out:
	xnlock_put_irqrestore(&process_hash_lock, s);
	return err;
}

static void process_hash_remove(struct cobalt_process *p)
{
	spl_t s;

	xnlock_get_irqsave(&process_hash_lock, s);
	if (p->mm)
		hlist_del(&p->hlink);
	xnlock_put_irqrestore(&process_hash_lock, s);
}

struct cobalt_process *cobalt_search_process(struct mm_struct *mm)
{
	struct cobalt_process *process;
	spl_t s;
	
	xnlock_get_irqsave(&process_hash_lock, s);
	process = __process_hash_search(mm);
	xnlock_put_irqrestore(&process_hash_lock, s);
	
	return process;
}

static void *lookup_context(int xid)
{
	struct cobalt_process *process = cobalt_current_process();
	void *priv = NULL;
	spl_t s;

	xnlock_get_irqsave(&process_hash_lock, s);
	/*
	 * First try matching the process context attached to the
	 * (usually main) thread which issued sc_cobalt_bind. If not
	 * found, try matching by mm context, which should point us
	 * back to the latter. If none match, then the current process
	 * is unbound.
	 */
	if (process == NULL && current->mm)
		process = __process_hash_search(current->mm);
	if (process)
		priv = process->priv[xid];

	xnlock_put_irqrestore(&process_hash_lock, s);

	return priv;
}

void cobalt_remove_process(struct cobalt_process *process)
{
	struct xnthread_personality *personality;
	void *priv;
	int xid;

	mutex_lock(&personality_lock);

	for (xid = NR_PERSONALITIES - 1; xid >= 0; xid--) {
		if (!__test_and_clear_bit(xid, &process->permap))
			continue;
		personality = cobalt_personalities[xid];
		priv = process->priv[xid];
		if (priv == NULL)
			continue;
		/*
		 * CAUTION: process potentially refers to stale memory
		 * upon return from detach_process() for the Cobalt
		 * personality, so don't dereference it afterwards.
		 */
		if (xid)
			process->priv[xid] = NULL;
		__clear_bit(personality->xid, &process->permap);
		personality->ops.detach_process(priv);
		atomic_dec(&personality->refcnt);
		XENO_WARN_ON(COBALT, atomic_read(&personality->refcnt) < 0);
		if (personality->module)
			module_put(personality->module);
	}

	cobalt_set_process(NULL);

	mutex_unlock(&personality_lock);
}

static void post_ppd_release(struct cobalt_umm *umm)
{
	struct cobalt_process *process;

	process = container_of(umm, struct cobalt_process, sys_ppd.umm);
	kfree(process);
}

static inline char *get_exe_path(struct task_struct *p)
{
	struct file *exe_file;
	char *pathname, *buf;
	struct mm_struct *mm;
	struct path path;

	/*
	 * PATH_MAX is fairly large, and in any case won't fit on the
	 * caller's stack happily; since we are mapping a shadow,
	 * which is a heavyweight operation anyway, let's pick the
	 * memory from the page allocator.
	 */
	buf = (char *)__get_free_page(GFP_KERNEL);
	if (buf == NULL)
		return ERR_PTR(-ENOMEM);

	mm = get_task_mm(p);
	if (mm == NULL) {
		pathname = "vmlinux";
		goto copy;	/* kernel thread */
	}

	exe_file = get_mm_exe_file(mm);
	mmput(mm);
	if (exe_file == NULL) {
		pathname = ERR_PTR(-ENOENT);
		goto out;	/* no luck. */
	}

	path = exe_file->f_path;
	path_get(&exe_file->f_path);
	fput(exe_file);
	pathname = d_path(&path, buf, PATH_MAX);
	path_put(&path);
	if (IS_ERR(pathname))
		goto out;	/* mmmh... */
copy:
	/* caution: d_path() may start writing anywhere in the buffer. */
	pathname = kstrdup(pathname, GFP_KERNEL);
out:
	free_page((unsigned long)buf);

	return pathname;
}

static inline int raise_cap(int cap)
{
	struct cred *new;

	new = prepare_creds();
	if (new == NULL)
		return -ENOMEM;

	cap_raise(new->cap_effective, cap);

	return commit_creds(new);
}

static int bind_personality(struct xnthread_personality *personality)
{
	struct cobalt_process *process;
	void *priv;

	/*
	 * We also check capabilities for stacking a Cobalt extension,
	 * in case the process dropped the supervisor privileges after
	 * a successful initial binding to the Cobalt interface.
	 */
	if (!capable(CAP_SYS_NICE) &&
	    (gid_arg == -1 || !in_group_p(KGIDT_INIT(gid_arg))))
		return -EPERM;
	/*
	 * Protect from the same process binding to the same interface
	 * several times.
	 */
	priv = lookup_context(personality->xid);
	if (priv)
		return 0;

	priv = personality->ops.attach_process();
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	process = cobalt_current_process();
	/*
	 * We are still covered by the personality_lock, so we may
	 * safely bump the module refcount after the attach handler
	 * has returned.
	 */
	if (personality->module && !try_module_get(personality->module)) {
		personality->ops.detach_process(priv);
		return -EAGAIN;
	}

	__set_bit(personality->xid, &process->permap);
	atomic_inc(&personality->refcnt);
	process->priv[personality->xid] = priv;

	raise_cap(CAP_SYS_NICE);
	raise_cap(CAP_IPC_LOCK);
	raise_cap(CAP_SYS_RAWIO);

	return 0;
}

int cobalt_bind_personality(unsigned int magic)
{
	struct xnthread_personality *personality;
	int xid, ret = -ESRCH;

	mutex_lock(&personality_lock);

	for (xid = 1; xid < NR_PERSONALITIES; xid++) {
		personality = cobalt_personalities[xid];
		if (personality && personality->magic == magic) {
			ret = bind_personality(personality);
			break;
		}
	}

	mutex_unlock(&personality_lock);

	return ret ?: xid;
}

int cobalt_bind_core(int ufeatures)
{
	struct cobalt_process *process;
	int ret;

	mutex_lock(&personality_lock);
	ret = bind_personality(&cobalt_personality);
	mutex_unlock(&personality_lock);
	if (ret)
		return ret;

	process = cobalt_current_process();
	/* Feature set userland knows about. */
	process->ufeatures = ufeatures;

	return 0;
}

/**
 * @fn int cobalt_register_personality(struct xnthread_personality *personality)
 * @internal
 * @brief Register a new interface personality.
 *
 * - personality->ops.attach_process() is called when a user-space
 *   process binds to the personality, on behalf of one of its
 *   threads. The attach_process() handler may return:
 *
 *   . an opaque pointer, representing the context of the calling
 *   process for this personality;
 *
 *   . a NULL pointer, meaning that no per-process structure should be
 *   attached to this process for this personality;
 *
 *   . ERR_PTR(negative value) indicating an error, the binding
 *   process will then abort.
 *
 * - personality->ops.detach_process() is called on behalf of an
 *   exiting user-space process which has previously attached to the
 *   personality. This handler is passed a pointer to the per-process
 *   data received earlier from the ops->attach_process() handler.
 *
 * @return the personality (extension) identifier.
 *
 * @note cobalt_get_context() is NULL when ops.detach_process() is
 * invoked for the personality the caller detaches from.
 *
 * @coretags{secondary-only}
 */
int cobalt_register_personality(struct xnthread_personality *personality)
{
	int xid;

	mutex_lock(&personality_lock);

	for (xid = 0; xid < NR_PERSONALITIES; xid++) {
		if (cobalt_personalities[xid] == NULL) {
			personality->xid = xid;
			atomic_set(&personality->refcnt, 0);
			cobalt_personalities[xid] = personality;
			goto out;
		}
	}

	xid = -EAGAIN;
out:
	mutex_unlock(&personality_lock);

	return xid;
}
EXPORT_SYMBOL_GPL(cobalt_register_personality);

/*
 * @brief Unregister an interface personality.
 *
 * @coretags{secondary-only}
 */
int cobalt_unregister_personality(int xid)
{
	struct xnthread_personality *personality;
	int ret = 0;

	if (xid < 0 || xid >= NR_PERSONALITIES)
		return -EINVAL;

	mutex_lock(&personality_lock);

	personality = cobalt_personalities[xid];
	if (atomic_read(&personality->refcnt) > 0)
		ret = -EBUSY;
	else
		cobalt_personalities[xid] = NULL;

	mutex_unlock(&personality_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(cobalt_unregister_personality);

/**
 * Stack a new personality over Cobalt for the current thread.
 *
 * This service registers the current thread as a member of the
 * additional personality identified by @a xid. If the current thread
 * is already assigned this personality, the call returns successfully
 * with no effect.
 *
 * @param xid the identifier of the additional personality.
 *
 * @return A handle to the previous personality. The caller should
 * save this handle for unstacking @a xid when applicable via a call
 * to cobalt_pop_personality().
 *
 * @coretags{secondary-only}
 */
struct xnthread_personality *
cobalt_push_personality(int xid)
{
	struct cobalt_threadinfo *p = pipeline_current();
	struct xnthread_personality *prev, *next;
	struct xnthread *thread = p->thread;

	secondary_mode_only();

	mutex_lock(&personality_lock);

	if (xid < 0 || xid >= NR_PERSONALITIES ||
	    p->process == NULL || !test_bit(xid, &p->process->permap)) {
		mutex_unlock(&personality_lock);
		return NULL;
	}

	next = cobalt_personalities[xid];
	prev = thread->personality;
	if (next == prev) {
		mutex_unlock(&personality_lock);
		return prev;
	}

	thread->personality = next;
	mutex_unlock(&personality_lock);
	xnthread_run_handler(thread, map_thread);

	return prev;
}
EXPORT_SYMBOL_GPL(cobalt_push_personality);

/**
 * Pop the topmost personality from the current thread.
 *
 * This service pops the topmost personality off the current thread.
 *
 * @param prev the previous personality which was returned by the
 * latest call to cobalt_push_personality() for the current thread.
 *
 * @coretags{secondary-only}
 */
void cobalt_pop_personality(struct xnthread_personality *prev)
{
	struct cobalt_threadinfo *p = pipeline_current();
	struct xnthread *thread = p->thread;

	secondary_mode_only();
	thread->personality = prev;
}
EXPORT_SYMBOL_GPL(cobalt_pop_personality);

/**
 * Return the per-process data attached to the calling user process.
 *
 * This service returns the per-process data attached to the calling
 * user process for the personality whose xid is @a xid.
 *
 * The per-process data was obtained from the ->attach_process()
 * handler defined for the personality @a xid refers to.
 *
 * See cobalt_register_personality() documentation for information on
 * the way to attach a per-process data to a process.
 *
 * @param xid the personality identifier.
 *
 * @return the per-process data if the current context is a user-space
 * process; @return NULL otherwise. As a special case,
 * cobalt_get_context(0) returns the current Cobalt process
 * descriptor, which is strictly identical to calling
 * cobalt_current_process().
 *
 * @coretags{task-unrestricted}
 */
void *cobalt_get_context(int xid)
{
	return lookup_context(xid);
}
EXPORT_SYMBOL_GPL(cobalt_get_context);

int cobalt_yield(xnticks_t min, xnticks_t max)
{
	xnticks_t start;
	int ret;

	start = xnclock_read_monotonic(&nkclock);
	max += start;
	min += start;

	do {
		ret = xnsynch_sleep_on(&yield_sync, max, XN_ABSOLUTE);
		if (ret & XNBREAK)
			return -EINTR;
	} while (ret == 0 && xnclock_read_monotonic(&nkclock) < min);

	return 0;
}
EXPORT_SYMBOL_GPL(cobalt_yield);

/**
 * @fn int cobalt_map_user(struct xnthread *thread, __u32 __user *u_winoff)
 * @internal
 * @brief Create a shadow thread context over a user task.
 *
 * This call maps a Xenomai thread to the current regular Linux task
 * running in userland.  The priority and scheduling class of the
 * underlying Linux task are not affected; it is assumed that the
 * interface library did set them appropriately before issuing the
 * shadow mapping request.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to current. This descriptor must have been previously
 * initialized by a call to xnthread_init().
 *
 * @param u_winoff will receive the offset of the per-thread
 * "u_window" structure in the global heap associated to @a
 * thread. This structure reflects thread state information visible
 * from userland through a shared memory window.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EINVAL is returned if the thread control block does not bear the
 * XNUSER bit.
 *
 * - -EBUSY is returned if either the current Linux task or the
 * associated shadow thread is already involved in a shadow mapping.
 *
 * @coretags{secondary-only}
 */
int cobalt_map_user(struct xnthread *thread, __u32 __user *u_winoff)
{
	struct xnthread_user_window *u_window;
	struct xnthread_start_attr attr;
	struct cobalt_ppd *sys_ppd;
	struct cobalt_umm *umm;
	int ret;

	if (!xnthread_test_state(thread, XNUSER))
		return -EINVAL;

	if (xnthread_current() || xnthread_test_state(thread, XNMAPPED))
		return -EBUSY;

	if (!access_wok(u_winoff, sizeof(*u_winoff)))
		return -EFAULT;

	ret = pipeline_prepare_current();
	if (ret)
		return ret;

	umm = &cobalt_kernel_ppd.umm;
	u_window = cobalt_umm_zalloc(umm, sizeof(*u_window));
	if (u_window == NULL)
		return -ENOMEM;

	thread->u_window = u_window;
	__xn_put_user(cobalt_umm_offset(umm, u_window), u_winoff);
	xnthread_pin_initial(thread);

	/*
	 * CAUTION: we enable the pipeline notifier only when our
	 * shadow TCB is consistent, so that we won't trigger false
	 * positive in debug code from handle_schedule_event() and
	 * friends.
	 */
	pipeline_init_shadow_tcb(thread);
	xnthread_suspend(thread, XNRELAX, XN_INFINITE, XN_RELATIVE, NULL);
	pipeline_attach_current(thread);
	xnthread_set_state(thread, XNMAPPED);
	xndebug_shadow_init(thread);
	sys_ppd = cobalt_ppd_get(0);
	atomic_inc(&sys_ppd->refcnt);
	/*
	 * ->map_thread() handler is invoked after the TCB is fully
	 * built, and when we know for sure that current will go
	 * through our task-exit handler, because it has a shadow
	 * extension and I-pipe notifications will soon be enabled for
	 * it.
	 */
	xnthread_run_handler(thread, map_thread);
	pipeline_enable_kevents();

	attr.mode = 0;
	attr.entry = NULL;
	attr.cookie = NULL;
	ret = xnthread_start(thread, &attr);
	if (ret)
		return ret;

	xnthread_sync_window(thread);

	xntrace_pid(xnthread_host_pid(thread),
		    xnthread_current_priority(thread));

	return 0;
}

void cobalt_signal_yield(void)
{
	spl_t s;

	if (!xnsynch_pended_p(&yield_sync))
		return;

	xnlock_get_irqsave(&nklock, s);
	if (xnsynch_pended_p(&yield_sync)) {
		xnsynch_flush(&yield_sync, 0);
		xnsched_run();
	}
	xnlock_put_irqrestore(&nklock, s);
}

static inline struct cobalt_process *
process_from_thread(struct xnthread *thread)
{
	return container_of(thread, struct cobalt_thread, threadbase)->process;
}

void cobalt_stop_debugged_process(struct xnthread *thread)
{
	struct cobalt_process *process = process_from_thread(thread);
	struct cobalt_thread *cth;

	if (process->debugged_threads > 0)
		return;

	list_for_each_entry(cth, &process->thread_list, next) {
		if (&cth->threadbase == thread)
			continue;

		xnthread_suspend(&cth->threadbase, XNDBGSTOP, XN_INFINITE,
				 XN_RELATIVE, NULL);
	}
}

static void cobalt_resume_debugged_process(struct cobalt_process *process)
{
	struct cobalt_thread *cth;

	xnsched_lock();

	list_for_each_entry(cth, &process->thread_list, next)
		if (xnthread_test_state(&cth->threadbase, XNDBGSTOP))
			xnthread_resume(&cth->threadbase, XNDBGSTOP);

	xnsched_unlock();
}

/* called with nklock held */
void cobalt_register_debugged_thread(struct xnthread *thread)
{
	struct cobalt_process *process = process_from_thread(thread);

	xnthread_set_state(thread, XNSSTEP);

	cobalt_stop_debugged_process(thread);
	process->debugged_threads++;

	if (xnthread_test_state(thread, XNRELAX))
		xnthread_suspend(thread, XNDBGSTOP, XN_INFINITE, XN_RELATIVE,
				 NULL);
}

/* called with nklock held */
void cobalt_unregister_debugged_thread(struct xnthread *thread)
{
	struct cobalt_process *process = process_from_thread(thread);

	process->debugged_threads--;
	xnthread_clear_state(thread, XNSSTEP);

	if (process->debugged_threads == 0)
		cobalt_resume_debugged_process(process);
}

int cobalt_handle_setaffinity_event(struct task_struct *task)
{
#ifdef CONFIG_SMP
	struct xnthread *thread;
	spl_t s;

	thread = xnthread_from_task(task);
	if (thread == NULL)
		return KEVENT_PROPAGATE;

	/*
	 * Detect a Cobalt thread sleeping in primary mode which is
	 * required to migrate to another CPU by the host kernel.
	 *
	 * We may NOT fix up thread->sched immediately using the
	 * passive migration call, because that latter always has to
	 * take place on behalf of the target thread itself while
	 * running in secondary mode. Therefore, that thread needs to
	 * go through secondary mode first, then move back to primary
	 * mode, so that affinity_ok() does the fixup work.
	 *
	 * We force this by sending a SIGSHADOW signal to the migrated
	 * thread, asking it to switch back to primary mode from the
	 * handler, at which point the interrupted syscall may be
	 * restarted.
	 */
	xnlock_get_irqsave(&nklock, s);

	if (xnthread_test_state(thread, XNTHREAD_BLOCK_BITS & ~XNRELAX))
		__xnthread_signal(thread, SIGSHADOW, SIGSHADOW_ACTION_HARDEN);

	xnlock_put_irqrestore(&nklock, s);
#endif /* CONFIG_SMP */

	return KEVENT_PROPAGATE;
}

#ifdef CONFIG_SMP
void cobalt_adjust_affinity(struct task_struct *task) /* nklocked, IRQs off */
{
	struct xnthread *thread = xnthread_from_task(task);
	struct xnsched *sched;
	int cpu = task_cpu(task);

	/*
	 * To maintain consistency between both Cobalt and host
	 * schedulers, reflecting a thread migration to another CPU
	 * into the Cobalt scheduler state must happen from secondary
	 * mode only, on behalf of the migrated thread itself once it
	 * runs on the target CPU.
	 *
	 * This means that the Cobalt scheduler state regarding the
	 * CPU information lags behind the host scheduler state until
	 * the migrated thread switches back to primary mode
	 * (i.e. task_cpu(p) != xnsched_cpu(xnthread_from_task(p)->sched)).
	 * This is ok since Cobalt does not schedule such thread until then.
	 *
	 * check_affinity() detects when a Cobalt thread switching
	 * back to primary mode did move to another CPU earlier while
	 * in secondary mode. If so, do the fixups to reflect the
	 * change.
	 */
	if (!xnsched_threading_cpu(cpu)) {
		/*
		 * The thread is about to switch to primary mode on a
		 * non-rt CPU, which is damn wrong and hopeless.
		 * Whine and cancel that thread.
		 */
		printk(XENO_WARNING "thread %s[%d] switched to non-rt CPU%d, aborted.\n",
		       thread->name, xnthread_host_pid(thread), cpu);
		/*
		 * Can't call xnthread_cancel() from a migration
		 * point, that would break. Since we are on the wakeup
		 * path to hardening, just raise XNCANCELD to catch it
		 * in xnthread_harden().
		 */
		xnthread_set_info(thread, XNCANCELD);
		return;
	}

	sched = xnsched_struct(cpu);
	if (sched == thread->sched)
		return;

	/*
	 * The current thread moved to a supported real-time CPU,
	 * which is not part of its original affinity mask
	 * though. Assume user wants to extend this mask.
	 */
	if (!cpumask_test_cpu(cpu, &thread->affinity))
		cpumask_set_cpu(cpu, &thread->affinity);

	xnthread_run_handler_stack(thread, move_thread, cpu);
	xnthread_migrate_passive(thread, sched);
}
#endif /* CONFIG_SMP */

static void __handle_taskexit_event(struct task_struct *p)
{
	struct cobalt_ppd *sys_ppd;
	struct xnthread *thread;
	spl_t s;

	/*
	 * We are called for both kernel and user shadows over the
	 * root thread.
	 */
	secondary_mode_only();

	thread = xnthread_current();
	XENO_BUG_ON(COBALT, thread == NULL);
	trace_cobalt_shadow_unmap(thread);

	xnlock_get_irqsave(&nklock, s);

	if (xnthread_test_state(thread, XNSSTEP))
		cobalt_unregister_debugged_thread(thread);

	xnsched_run();

	xnlock_put_irqrestore(&nklock, s);

	xnthread_run_handler_stack(thread, exit_thread);

	if (xnthread_test_state(thread, XNUSER)) {
		cobalt_umm_free(&cobalt_kernel_ppd.umm, thread->u_window);
		thread->u_window = NULL;
		sys_ppd = cobalt_ppd_get(0);
		if (atomic_dec_and_test(&sys_ppd->refcnt))
			cobalt_remove_process(cobalt_current_process());
	}
}

int cobalt_handle_user_return(struct task_struct *task)
{
	struct xnthread *thread;
	spl_t s;
	int err;

	thread = xnthread_from_task(task);
	if (thread == NULL)
		return KEVENT_PROPAGATE;

	if (xnthread_test_info(thread, XNCONTHI)) {
		xnlock_get_irqsave(&nklock, s);
		xnthread_clear_info(thread, XNCONTHI);
		xnlock_put_irqrestore(&nklock, s);

		err = xnthread_harden();

		/*
		 * XNCONTHI may or may not have been re-applied if
		 * harden bailed out due to pending signals. Make sure
		 * it is set in that case.
		 */
		if (err == -ERESTARTSYS) {
			xnlock_get_irqsave(&nklock, s);
			xnthread_set_info(thread, XNCONTHI);
			xnlock_put_irqrestore(&nklock, s);
		}
	}

	return KEVENT_PROPAGATE;
}

static void detach_current(void)
{
	struct cobalt_threadinfo *p = pipeline_current();

	p->thread = NULL;
	p->process = NULL;
}

int cobalt_handle_taskexit_event(struct task_struct *task) /* task == current */
{
	__handle_taskexit_event(task);

	/*
	 * __xnthread_cleanup() -> ... -> finalize_thread
	 * handler. From that point, the TCB is dropped. Be careful of
	 * not treading on stale memory within @thread.
	 */
	__xnthread_cleanup(xnthread_current());

	detach_current();

	return KEVENT_PROPAGATE;
}

int cobalt_handle_cleanup_event(struct mm_struct *mm)
{
	struct cobalt_process *old, *process;
	struct cobalt_ppd *sys_ppd;
	struct xnthread *curr;

	/*
	 * We are NOT called for exiting kernel shadows.
	 * cobalt_current_process() is cleared if we get there after
	 * handle_task_exit(), so we need to restore this context
	 * pointer temporarily.
	 */
	process = cobalt_search_process(mm);
	old = cobalt_set_process(process);
	sys_ppd = cobalt_ppd_get(0);
	if (sys_ppd != &cobalt_kernel_ppd) {
		bool running_exec;

		/*
		 * Detect a userland shadow running exec(), i.e. still
		 * attached to the current linux task (no prior
		 * detach_current). In this case, we emulate a task
		 * exit, since the Xenomai binding shall not survive
		 * the exec() syscall. Since the process will keep on
		 * running though, we have to disable the event
		 * notifier manually for it.
		 */
		curr = xnthread_current();
		running_exec = curr && (current->flags & PF_EXITING) == 0;
		if (running_exec) {
			__handle_taskexit_event(current);
			pipeline_cleanup_process();
		}
		if (atomic_dec_and_test(&sys_ppd->refcnt))
			cobalt_remove_process(process);
		if (running_exec) {
			__xnthread_cleanup(curr);
			detach_current();
		}
	}

	/*
	 * CAUTION: Do not override a state change caused by
	 * cobalt_remove_process().
	 */
	if (cobalt_current_process() == process)
		cobalt_set_process(old);

	return KEVENT_PROPAGATE;
}

static int attach_process(struct cobalt_process *process)
{
	struct cobalt_ppd *p = &process->sys_ppd;
	char *exe_path;
	int ret;

	ret = cobalt_umm_init(&p->umm, CONFIG_XENO_OPT_PRIVATE_HEAPSZ * 1024,
			      post_ppd_release);
	if (ret)
		return ret;

	cobalt_umm_set_name(&p->umm, "private heap[%d]", task_pid_nr(current));

	ret = pipeline_attach_process(process);
	if (ret)
		goto fail_pipeline;

	exe_path = get_exe_path(current);
	if (IS_ERR(exe_path)) {
		printk(XENO_WARNING
		       "%s[%d] can't find exe path\n",
		       current->comm, task_pid_nr(current));
		exe_path = NULL; /* Not lethal, but weird. */
	}
	p->exe_path = exe_path;
	xntree_init(&p->fds);
	atomic_set(&p->refcnt, 1);

	ret = process_hash_enter(process);
	if (ret)
		goto fail_hash;

	return 0;
fail_hash:
	pipeline_detach_process(process);
	if (p->exe_path)
		kfree(p->exe_path);
fail_pipeline:
	cobalt_umm_destroy(&p->umm);

	return ret;
}

static void *cobalt_process_attach(void)
{
	struct cobalt_process *process;
	int ret;

	process = kzalloc(sizeof(*process), GFP_KERNEL);
	if (process == NULL)
		return ERR_PTR(-ENOMEM);

	ret = attach_process(process);
	if (ret) {
		kfree(process);
		return ERR_PTR(ret);
	}

	INIT_LIST_HEAD(&process->resources.condq);
	INIT_LIST_HEAD(&process->resources.mutexq);
	INIT_LIST_HEAD(&process->resources.semq);
	INIT_LIST_HEAD(&process->resources.monitorq);
	INIT_LIST_HEAD(&process->resources.eventq);
	INIT_LIST_HEAD(&process->resources.schedq);
	INIT_LIST_HEAD(&process->sigwaiters);
	INIT_LIST_HEAD(&process->thread_list);
	xntree_init(&process->usems);
	bitmap_fill(process->timers_map, CONFIG_XENO_OPT_NRTIMERS);
	cobalt_set_process(process);

	return process;
}

static void detach_process(struct cobalt_process *process)
{
	struct cobalt_ppd *p = &process->sys_ppd;

	if (p->exe_path)
		kfree(p->exe_path);

	rtdm_fd_cleanup(p);
	process_hash_remove(process);
	/*
	 * CAUTION: the process descriptor might be immediately
	 * released as a result of calling cobalt_umm_destroy(), so we
	 * must do this last, not to tread on stale memory.
	 */
	cobalt_umm_destroy(&p->umm);
}

static void __reclaim_resource(struct cobalt_process *process,
			       void (*reclaim)(struct cobalt_resnode *node, spl_t s),
			       struct list_head *local,
			       struct list_head *global)
{
	struct cobalt_resnode *node, *tmp;
	LIST_HEAD(stash);
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (list_empty(global))
		goto flush_local;

	list_for_each_entry_safe(node, tmp, global, next) {
		if (node->owner == process) {
			list_del(&node->next);
			list_add(&node->next, &stash);
		}
	}
		
	list_for_each_entry_safe(node, tmp, &stash, next) {
		reclaim(node, s);
		xnlock_get_irqsave(&nklock, s);
	}

	XENO_BUG_ON(COBALT, !list_empty(&stash));

flush_local:
	if (list_empty(local))
		goto out;

	list_for_each_entry_safe(node, tmp, local, next) {
		reclaim(node, s);
		xnlock_get_irqsave(&nklock, s);
	}
out:
	xnsched_run();
	xnlock_put_irqrestore(&nklock, s);
}

#define cobalt_reclaim_resource(__process, __reclaim, __type)		\
	__reclaim_resource(__process, __reclaim,			\
			   &(__process)->resources.__type ## q,		\
			   &cobalt_global_resources.__type ## q)

static void cobalt_process_detach(void *arg)
{
	struct cobalt_process *process = arg;

	cobalt_nsem_reclaim(process);
 	cobalt_timer_reclaim(process);
 	cobalt_sched_reclaim(process);
	cobalt_reclaim_resource(process, cobalt_cond_reclaim, cond);
	cobalt_reclaim_resource(process, cobalt_mutex_reclaim, mutex);
	cobalt_reclaim_resource(process, cobalt_event_reclaim, event);
	cobalt_reclaim_resource(process, cobalt_monitor_reclaim, monitor);
	cobalt_reclaim_resource(process, cobalt_sem_reclaim, sem);
 	detach_process(process);
	/*
	 * The cobalt_process descriptor release may be deferred until
	 * the last mapping on the private heap is gone. However, this
	 * is potentially stale memory already.
	 */
}

struct xnthread_personality cobalt_personality = {
	.name = "cobalt",
	.magic = 0,
	.ops = {
		.attach_process = cobalt_process_attach,
		.detach_process = cobalt_process_detach,
		.map_thread = cobalt_thread_map,
		.exit_thread = cobalt_thread_exit,
		.finalize_thread = cobalt_thread_finalize,
	},
};
EXPORT_SYMBOL_GPL(cobalt_personality);

__init int cobalt_init(void)
{
	unsigned int i, size;
	int ret;

	size = sizeof(*process_hash) * PROCESS_HASH_SIZE;
	process_hash = kmalloc(size, GFP_KERNEL);
	if (process_hash == NULL) {
		printk(XENO_ERR "cannot allocate processes hash table\n");
		return -ENOMEM;
	}

	ret = xndebug_init();
	if (ret)
		goto fail_debug;

	for (i = 0; i < PROCESS_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&process_hash[i]);

	xnsynch_init(&yield_sync, XNSYNCH_FIFO, NULL);

	ret = cobalt_memdev_init();
	if (ret)
		goto fail_memdev;

	ret = cobalt_register_personality(&cobalt_personality);
	if (ret)
		goto fail_register;

	ret = cobalt_signal_init();
	if (ret)
		goto fail_siginit;

	ret = pipeline_trap_kevents();
	if (ret)
		goto fail_kevents;

	if (gid_arg != -1)
		printk(XENO_INFO "allowing access to group %d\n", gid_arg);

	return 0;
fail_kevents:
	cobalt_signal_cleanup();
fail_siginit:
	cobalt_unregister_personality(0);
fail_register:
	cobalt_memdev_cleanup();
fail_memdev:
	xnsynch_destroy(&yield_sync);
	xndebug_cleanup();
fail_debug:
	kfree(process_hash);

	return ret;
}

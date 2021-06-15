/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2001-2014 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2001-2014 The Xenomai project <http://www.xenomai.org>
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * SMP support Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
 * RTAI/fusion Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 */

#include <linux/ptrace.h>
#include <pipeline/kevents.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/vdso.h>
#include <cobalt/kernel/init.h>
#include <rtdm/driver.h>
#include <trace/events/cobalt-core.h>
#include "../posix/process.h"
#include "../posix/thread.h"
#include "../posix/memory.h"

static void detach_current(void);

void arch_inband_task_init(struct task_struct *tsk)
{
	struct cobalt_threadinfo *p = dovetail_task_state(tsk);

	p->thread = NULL;
	p->process = NULL;
}

void handle_oob_trap_entry(unsigned int trapnr, struct pt_regs *regs)
{
	struct xnthread *thread;
	struct xnsched *sched;
	spl_t s;

	sched = xnsched_current();
	thread = sched->curr;

	/*
	 * Enable back tracing.
	 */
	trace_cobalt_thread_fault(xnarch_fault_pc(regs), trapnr);

	if (xnthread_test_state(thread, XNROOT))
		return;

	if (xnarch_fault_bp_p(trapnr) && user_mode(regs)) {
		XENO_WARN_ON(CORE, xnthread_test_state(thread, XNRELAX));
		xnlock_get_irqsave(&nklock, s);
		xnthread_set_info(thread, XNCONTHI);
		dovetail_request_ucall(current);
		cobalt_stop_debugged_process(thread);
		xnlock_put_irqrestore(&nklock, s);
		xnsched_run();
	}

	/*
	 * If we experienced a trap on behalf of a shadow thread
	 * running in primary mode, move it to the Linux domain,
	 * leaving the kernel process the exception.
	 */
#if defined(CONFIG_XENO_OPT_DEBUG_COBALT) || defined(CONFIG_XENO_OPT_DEBUG_USER)
	if (!user_mode(regs)) {
		xntrace_panic_freeze();
		printk(XENO_WARNING
		       "switching %s to secondary mode after exception #%u in "
		       "kernel-space at 0x%lx (pid %d)\n", thread->name,
		       trapnr,
		       xnarch_fault_pc(regs),
		       xnthread_host_pid(thread));
		xntrace_panic_dump();
	} else if (xnarch_fault_notify(trapnr)) /* Don't report debug traps */
		printk(XENO_WARNING
		       "switching %s to secondary mode after exception #%u from "
		       "user-space at 0x%lx (pid %d)\n", thread->name,
		       trapnr,
		       xnarch_fault_pc(regs),
		       xnthread_host_pid(thread));
#endif

	if (xnarch_fault_pf_p(trapnr))
		/*
		 * The page fault counter is not SMP-safe, but it's a
		 * simple indicator that something went wrong wrt
		 * memory locking anyway.
		 */
		xnstat_counter_inc(&thread->stat.pf);

	xnthread_relax(xnarch_fault_notify(trapnr), SIGDEBUG_MIGRATE_FAULT);
}

static inline int handle_setaffinity_event(struct dovetail_migration_data *d)
{
	return cobalt_handle_setaffinity_event(d->task);
}

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

static int handle_taskexit_event(struct task_struct *p) /* p == current */
{
	__handle_taskexit_event(p);

	/*
	 * __xnthread_cleanup() -> ... -> finalize_thread
	 * handler. From that point, the TCB is dropped. Be careful of
	 * not treading on stale memory within @thread.
	 */
	__xnthread_cleanup(xnthread_current());

	detach_current();

	return KEVENT_PROPAGATE;
}

static int handle_user_return(struct task_struct *task)
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

void handle_oob_mayday(struct pt_regs *regs)
{
	XENO_BUG_ON(COBALT, !xnthread_test_state(xnthread_current(), XNUSER));

	xnthread_relax(0, 0);
}

static int handle_sigwake_event(struct task_struct *p)
{
	struct xnthread *thread;
	sigset_t pending;
	spl_t s;

	thread = xnthread_from_task(p);
	if (thread == NULL)
		return KEVENT_PROPAGATE;

	xnlock_get_irqsave(&nklock, s);

	/*
	 * CAUTION: __TASK_TRACED is not set in p->state yet. This
	 * state bit will be set right after we return, when the task
	 * is woken up.
	 */
	if ((p->ptrace & PT_PTRACED) && !xnthread_test_state(thread, XNSSTEP)) {
		/* We already own the siglock. */
		sigorsets(&pending,
			  &p->pending.signal,
			  &p->signal->shared_pending.signal);

		if (sigismember(&pending, SIGTRAP) ||
		    sigismember(&pending, SIGSTOP)
		    || sigismember(&pending, SIGINT))
			cobalt_register_debugged_thread(thread);
	}

	if (xnthread_test_state(thread, XNRELAX))
		goto out;

	/*
	 * Allow a thread stopped for debugging to resume briefly in order to
	 * migrate to secondary mode. xnthread_relax will reapply XNDBGSTOP.
	 */
	if (xnthread_test_state(thread, XNDBGSTOP))
		xnthread_resume(thread, XNDBGSTOP);

	__xnthread_kick(thread);
out:
	xnsched_run();

	xnlock_put_irqrestore(&nklock, s);

	return KEVENT_PROPAGATE;
}

static int handle_cleanup_event(struct mm_struct *mm)
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
			dovetail_stop_altsched();
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

int handle_ptrace_resume(struct task_struct *tracee)
{
	struct xnthread *thread;
	spl_t s;

	thread = xnthread_from_task(tracee);
	if (thread == NULL)
		return KEVENT_PROPAGATE;

	if (xnthread_test_state(thread, XNSSTEP)) {
		xnlock_get_irqsave(&nklock, s);

		xnthread_resume(thread, XNDBGSTOP);
		cobalt_unregister_debugged_thread(thread);

		xnlock_put_irqrestore(&nklock, s);
	}

	return KEVENT_PROPAGATE;
}

static void handle_ptrace_cont(void)
{
	struct xnthread *curr = xnthread_current();
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (xnthread_test_state(curr, XNSSTEP)) {
		if (!xnthread_test_info(curr, XNCONTHI))
			cobalt_unregister_debugged_thread(curr);

		xnthread_set_localinfo(curr, XNHICCUP);

		dovetail_request_ucall(current);
	}

	xnlock_put_irqrestore(&nklock, s);
}

void handle_inband_event(enum inband_event_type event, void *data)
{
	switch (event) {
	case INBAND_TASK_SIGNAL:
		handle_sigwake_event(data);
		break;
	case INBAND_TASK_MIGRATION:
		handle_setaffinity_event(data);
		break;
	case INBAND_TASK_EXIT:
		if (xnthread_current())
			handle_taskexit_event(current);
		break;
	case INBAND_TASK_RETUSER:
		handle_user_return(data);
		break;
	case INBAND_TASK_PTSTEP:
		handle_ptrace_resume(data);
		break;
	case INBAND_TASK_PTCONT:
		handle_ptrace_cont();
		break;
	case INBAND_TASK_PTSTOP:
		break;
	case INBAND_PROCESS_CLEANUP:
		handle_cleanup_event(data);
		break;
	}
}

/*
 * Called by the in-band kernel when the CLOCK_REALTIME epoch changes.
 */
void inband_clock_was_set(void)
{
	if (realtime_core_enabled())
		xnclock_set_wallclock(ktime_get_real_fast_ns());
}

#ifdef CONFIG_MMU

int pipeline_prepare_current(void)
{
	struct task_struct *p = current;
	kernel_siginfo_t si;

	if ((p->mm->def_flags & VM_LOCKED) == 0) {
		memset(&si, 0, sizeof(si));
		si.si_signo = SIGDEBUG;
		si.si_code = SI_QUEUE;
		si.si_int = SIGDEBUG_NOMLOCK | sigdebug_marker;
		send_sig_info(SIGDEBUG, &si, p);
	}

	return 0;
}

static inline int get_mayday_prot(void)
{
	return PROT_READ|PROT_EXEC;
}

#else /* !CONFIG_MMU */

int pipeline_prepare_current(void)
{
	return 0;
}

static inline int get_mayday_prot(void)
{
	/*
	 * Until we stop backing /dev/mem with the mayday page, we
	 * can't ask for PROT_EXEC since the former does not define
	 * mmap capabilities, and default ones won't allow an
	 * executable mapping with MAP_SHARED. In the NOMMU case, this
	 * is (currently) not an issue.
	 */
	return PROT_READ;
}

#endif /* !CONFIG_MMU */

void resume_oob_task(struct task_struct *p) /* inband, oob stage stalled */
{
	struct xnthread *thread = xnthread_from_task(p);

	xnlock_get(&nklock);

	/*
	 * We fire the handler before the thread is migrated, so that
	 * thread->sched does not change between paired invocations of
	 * relax_thread/harden_thread handlers.
	 */
	xnthread_run_handler_stack(thread, harden_thread);
	if (cobalt_affinity_ok(p))
		xnthread_resume(thread, XNRELAX);

	/*
	 * In case we migrated independently of the user return notifier, clear
	 * XNCONTHI here and also disable the notifier - we are already done.
	 */
	if (unlikely(xnthread_test_info(thread, XNCONTHI))) {
		xnthread_clear_info(thread, XNCONTHI);
		dovetail_clear_ucall();
	}

	/* Unregister as debugged thread in case we postponed this. */
	if (unlikely(xnthread_test_state(thread, XNSSTEP)))
		cobalt_unregister_debugged_thread(thread);

	xnlock_put(&nklock);

	xnsched_run();

}

void pipeline_attach_current(struct xnthread *thread)
{
	struct cobalt_threadinfo *p;

	p = pipeline_current();
	p->thread = thread;
	p->process = cobalt_search_process(current->mm);
	dovetail_init_altsched(&xnthread_archtcb(thread)->altsched);
}

static void detach_current(void)
{
	struct cobalt_threadinfo *p = pipeline_current();

	p->thread = NULL;
	p->process = NULL;
}

int pipeline_trap_kevents(void)
{
	dovetail_start();
	return 0;
}

void pipeline_enable_kevents(void)
{
	dovetail_start_altsched();
}

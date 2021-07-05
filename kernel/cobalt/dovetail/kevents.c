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

static inline int handle_taskexit_event(struct task_struct *p)
{
	return cobalt_handle_taskexit_event(p);
}

static inline int handle_user_return(struct task_struct *task)
{
	return cobalt_handle_user_return(task);
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

static inline int handle_cleanup_event(struct mm_struct *mm)
{
	return cobalt_handle_cleanup_event(mm);
}

void pipeline_cleanup_process(void)
{
	dovetail_stop_altsched();
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

	cobalt_adjust_affinity(p);

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

int pipeline_trap_kevents(void)
{
	dovetail_start();
	return 0;
}

void pipeline_enable_kevents(void)
{
	dovetail_start_altsched();
}

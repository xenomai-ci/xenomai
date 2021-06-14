/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2001-2020 Philippe Gerum <rpm@xenomai.org>.
 */

#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/assert.h>
#include <pipeline/sched.h>
#include <trace/events/cobalt-core.h>

int pipeline_schedule(struct xnsched *sched)
{
	int ret = 0;

	XENO_WARN_ON_ONCE(COBALT,
		!hard_irqs_disabled() && is_secondary_domain());

	if (!xnarch_escalate())
		ret = ___xnsched_run(sched);

	return ret;
}
EXPORT_SYMBOL_GPL(pipeline_schedule);

void pipeline_prep_switch_oob(struct xnthread *root)
{
	struct xnarchtcb *rootcb = xnthread_archtcb(root);
	struct task_struct *p = current;

	ipipe_notify_root_preemption();
	/* Remember the preempted Linux task pointer. */
	rootcb->core.host_task = p;
	rootcb->core.tsp = &p->thread;
	rootcb->core.mm = rootcb->core.active_mm = ipipe_get_active_mm();
	rootcb->core.tip = task_thread_info(p);
	xnarch_leave_root(root);
}

#ifdef CONFIG_XENO_ARCH_FPU

static void switch_fpu(void)
{
	struct xnsched *sched = xnsched_current();
	struct xnthread *curr = sched->curr;

	if (!xnthread_test_state(curr, XNFPU))
		return;

	xnarch_switch_fpu(sched->fpuholder, curr);
	sched->fpuholder = curr;
}

static void giveup_fpu(struct xnthread *thread)
{
	struct xnsched *sched = thread->sched;

	if (thread == sched->fpuholder)
		sched->fpuholder = NULL;
}

#else

static inline void giveup_fpu(struct xnthread *thread)
{ }

#endif /* !CONFIG_XENO_ARCH_FPU */

bool pipeline_switch_to(struct xnthread *prev, struct xnthread *next,
			bool leaving_inband)
{
	xnarch_switch_to(prev, next);

	/*
	 * Test whether we transitioned from primary mode to secondary
	 * over a shadow thread, caused by a call to xnthread_relax().
	 * In such a case, we are running over the regular schedule()
	 * tail code, so we have to tell the caller to skip the Cobalt
	 * tail code.
	 */
	if (!leaving_inband && is_secondary_domain()) {
		__ipipe_complete_domain_migration();
		XENO_BUG_ON(COBALT, xnthread_current() == NULL);
		/*
		 * Interrupts must be disabled here (has to be done on
		 * entry of the Linux [__]switch_to function), but it
		 * is what callers expect, specifically the reschedule
		 * of an IRQ handler that hit before we call
		 * xnsched_run in xnthread_suspend() when relaxing a
		 * thread.
		 */
		XENO_BUG_ON(COBALT, !hard_irqs_disabled());
		return true;
	}

	switch_fpu();

	return false;
}

void pipeline_init_shadow_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct task_struct *p = current;

	/*
	 * If the current task is a kthread, the pipeline will take
	 * the necessary steps to make the FPU usable in such
	 * context. The kernel already took care of this issue for
	 * userland tasks (e.g. setting up a clean backup area).
	 */
	__ipipe_share_current(0);

	tcb->core.host_task = p;
	tcb->core.tsp = &p->thread;
	tcb->core.mm = p->mm;
	tcb->core.active_mm = p->mm;
	tcb->core.tip = task_thread_info(p);
#ifdef CONFIG_XENO_ARCH_FPU
	tcb->core.user_fpu_owner = p;
#endif /* CONFIG_XENO_ARCH_FPU */
	xnarch_init_shadow_tcb(thread);

	trace_cobalt_shadow_map(thread);
}

void pipeline_init_root_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	struct task_struct *p = current;

	tcb->core.host_task = p;
	tcb->core.tsp = &tcb->core.ts;
	tcb->core.mm = p->mm;
	tcb->core.tip = NULL;
	xnarch_init_root_tcb(thread);
}

int pipeline_leave_inband(void)
{
	int ret;

	ret = __ipipe_migrate_head();
	if (ret)
		return ret;

	switch_fpu();

	return 0;
}

int pipeline_leave_oob_prepare(void)
{
	struct xnthread *curr = xnthread_current();
	struct task_struct *p = current;
	int suspmask = XNRELAX;

	set_current_state(p->state & ~TASK_NOWAKEUP);

	/*
	 * If current is being debugged, record that it should migrate
	 * back in case it resumes in userspace. If it resumes in
	 * kernel space, i.e.  over a restarting syscall, the
	 * associated hardening will both clear XNCONTHI and disable
	 * the user return notifier again.
	 */
	if (xnthread_test_state(curr, XNSSTEP)) {
		xnthread_set_info(curr, XNCONTHI);
		ipipe_enable_user_intret_notifier();
		suspmask |= XNDBGSTOP;
	}
	/*
	 * Return the suspension bits the caller should pass to
	 * xnthread_suspend().
	 */
	return suspmask;
}

void pipeline_leave_oob_finish(void)
{
	__ipipe_reenter_root();
}

void pipeline_finalize_thread(struct xnthread *thread)
{
	giveup_fpu(thread);
}

void pipeline_raise_mayday(struct task_struct *tsk)
{
	ipipe_raise_mayday(tsk);
}

void pipeline_clear_mayday(void) /* May solely affect current. */
{
	ipipe_clear_thread_flag(TIP_MAYDAY);
}

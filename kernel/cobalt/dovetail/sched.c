/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2001-2020 Philippe Gerum <rpm@xenomai.org>.
 */

#include <linux/cpuidle.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/sched.h>
#include <pipeline/sched.h>
#include <trace/events/cobalt-core.h>

/* in-band stage, hard_irqs_disabled() */
bool irq_cpuidle_control(struct cpuidle_device *dev,
			struct cpuidle_state *state)
{
	/*
	 * Deny entering sleep state if this entails stopping the
	 * timer (i.e. C3STOP misfeature).
	 */
	if (state && (state->flags & CPUIDLE_FLAG_TIMER_STOP))
		return false;

	return true;
}

bool pipeline_switch_to(struct xnthread *prev, struct xnthread *next,
			bool leaving_inband)
{
	return dovetail_context_switch(&xnthread_archtcb(prev)->altsched,
			&xnthread_archtcb(next)->altsched, leaving_inband);
}

void pipeline_init_shadow_tcb(struct xnthread *thread)
{
	/*
	 * Initialize the alternate scheduling control block.
	 */
	dovetail_init_altsched(&xnthread_archtcb(thread)->altsched);

	trace_cobalt_shadow_map(thread);
}

void pipeline_init_root_tcb(struct xnthread *thread)
{
	/*
	 * Initialize the alternate scheduling control block.
	 */
	dovetail_init_altsched(&xnthread_archtcb(thread)->altsched);
}

int pipeline_leave_inband(void)
{
	return dovetail_leave_inband();
}

int pipeline_leave_oob_prepare(void)
{
	dovetail_leave_oob();

	return XNRELAX;
}

void pipeline_leave_oob_finish(void)
{
	dovetail_resume_inband();
}

void pipeline_raise_mayday(struct task_struct *tsk)
{
	dovetail_send_mayday(tsk);
}

void pipeline_clear_mayday(void) /* May solely affect current. */
{
	clear_thread_flag(TIF_MAYDAY);
}

irqreturn_t pipeline_reschedule_ipi_handler(int irq, void *dev_id)
{

	/* Will reschedule from irq_exit_pipeline(). */

	return IRQ_HANDLED;
}

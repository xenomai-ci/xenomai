/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
 * 
 * ARM64 port
 *   Copyright (C) 2015 Dmitriy Cherkasov <dmitriy@mperpetuo.com>
 *   Copyright (C) 2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <linux/sched.h>
#include <linux/ipipe.h>
#include <linux/mm.h>
#include <linux/jump_label.h>
#include <asm/mmu_context.h>
#include <cobalt/kernel/thread.h>
#include <asm/fpsimd.h>
#include <asm/processor.h>
#include <asm/hw_breakpoint.h>

#ifdef ARM64_XENO_OLD_SWITCH

#include <asm/fpsimd.h>

#define FPSIMD_EN (0x3 << 20)

static inline unsigned long get_cpacr(void)
{
	unsigned long result;
	__asm__ ("mrs %0, cpacr_el1": "=r"(result));
	return result;
}

static inline void set_cpacr(long val)
{
	__asm__ __volatile__ (
		"msr cpacr_el1, %0\n\t"
		"isb"
		: /* */ : "r"(val));
}

static inline void enable_fpsimd(void)
{
	set_cpacr(get_cpacr() | FPSIMD_EN);
}

static inline struct fpsimd_state *get_fpu_owner(struct xnarchtcb *rootcb)
{
	struct task_struct *curr = rootcb->core.host_task;

	if (test_ti_thread_flag(task_thread_info(curr), TIF_FOREIGN_FPSTATE))
		/* Foreign fpu state, use auxiliary backup area */
		return &rootcb->xnfpsimd_state;

	return &curr->thread.fpsimd_state;
}

void xnarch_leave_root(struct xnthread *root)
{
	struct xnarchtcb *rootcb = xnthread_archtcb(root);
	rootcb->fpup = get_fpu_owner(rootcb);
}

void xnarch_switch_fpu(struct xnthread *from, struct xnthread *to)
{
	struct fpsimd_state *const from_fpup = from ? from->tcb.fpup : NULL;
	struct fpsimd_state *const to_fpup = to->tcb.fpup;

	enable_fpsimd();

	if (from_fpup == to_fpup)
		return;

	fpsimd_save_state(from_fpup);

	fpsimd_load_state(to_fpup);
	to_fpup->cpu = ipipe_processor_id();
}

void xnarch_init_shadow_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = xnthread_archtcb(thread);
	tcb->fpup = &tcb->core.host_task->thread.fpsimd_state;
}

void xnarch_init_root_tcb(struct xnthread *thread)
{
	struct xnarchtcb *tcb = &thread->tcb;
	tcb->fpup = NULL;
}

#endif /* ARM64_XENO_OLD_SWITCH */

void xnarch_switch_to(struct xnthread *out, struct xnthread *in)
{
	struct xnarchtcb *out_tcb = &out->tcb, *in_tcb = &in->tcb;
	struct task_struct *prev, *next, *last;
	struct mm_struct *prev_mm, *next_mm;

	next = in_tcb->core.host_task;
	prev = out_tcb->core.host_task;
	prev_mm = out_tcb->core.active_mm;

	next_mm = in_tcb->core.mm;
	if (next_mm == NULL) {
		in_tcb->core.active_mm = prev_mm;
		enter_lazy_tlb(prev_mm, next);
	} else {
		ipipe_switch_mm_head(prev_mm, next_mm, next);
		/*
		 * We might be switching back to the root thread,
		 * which we preempted earlier, shortly after "current"
		 * dropped its mm context in the do_exit() path
		 * (next->mm == NULL). In that particular case, the
		 * kernel expects a lazy TLB state for leaving the mm.
		 */
		if (next->mm == NULL)
			enter_lazy_tlb(prev_mm, next);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
	ipipe_switch_to(prev, next);
	(void)last;
#else
	switch_to(prev, next, last);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0)
	fpsimd_restore_current_state();
#endif
#endif
}

int xnarch_escalate(void)
{
	if (ipipe_root_p) {
		ipipe_raise_irq(cobalt_pipeline.escalate_virq);
		return 1;
	}

	return 0;
}

/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2008 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 * Copyright (C) 2021 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/mm.h>
#include <asm/xenomai/machine.h>

static void mach_arm_prefault(struct vm_area_struct *vma)
{
	unsigned long addr;
	unsigned int flags;

	if ((vma->vm_flags & VM_MAYREAD)) {
		flags = (vma->vm_flags & VM_MAYWRITE) ? FAULT_FLAG_WRITE : 0;
		for (addr = vma->vm_start;
		     addr != vma->vm_end; addr += PAGE_SIZE)
			handle_mm_fault(vma, addr, flags, NULL);
	}
}

static const char *const fault_labels[] = {
	[ARM_TRAP_ACCESS] = "Data or instruction access",
	[ARM_TRAP_SECTION] = "Section fault",
	[ARM_TRAP_DABT] = "Generic data abort",
	[ARM_TRAP_PABT] = "Prefetch abort",
	[ARM_TRAP_BREAK] = "Instruction breakpoint",
	[ARM_TRAP_FPU] = "Floating point exception",
	[ARM_TRAP_VFP] = "VFP Floating point exception",
	[ARM_TRAP_UNDEFINSTR] = "Undefined instruction",
	[ARM_TRAP_ALIGNMENT] = "Unaligned access exception",
	[31] = NULL
};

struct cobalt_machine cobalt_machine = {
	.name = "arm",
	.init = NULL,
	.late_init = NULL,
	.cleanup = NULL,
	.prefault = mach_arm_prefault,
	.fault_labels = fault_labels,
};

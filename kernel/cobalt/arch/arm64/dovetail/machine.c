// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2008 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
// Copyright (C) 2021 Philippe Gerum  <rpm@xenomai.org>

#include <linux/mm.h>
#include <asm/xenomai/machine.h>

static void mach_arm64_prefault(struct vm_area_struct *vma)
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
	[ARM64_TRAP_ACCESS] = "Data or instruction abort",
	[ARM64_TRAP_ALIGN] = "SP/PC alignment abort",
	[ARM64_TRAP_SEA] = "Synchronous external abort",
	[ARM64_TRAP_DEBUG] = "Debug trap",
	[ARM64_TRAP_UNDI] = "Undefined instruction",
	[ARM64_TRAP_UNDSE] = "Undefined synchronous exception",
	[ARM64_TRAP_FPE] = "FPSIMD exception",
	[ARM64_TRAP_SVE] = "SVE access trap",
	[ARM64_TRAP_BTI] = "Branch target identification trap",
	[31] = NULL
};

struct cobalt_machine cobalt_machine = {
	.name = "arm64",
	.init = NULL,
	.late_init = NULL,
	.cleanup = NULL,
	.prefault = mach_arm64_prefault,
	.fault_labels = fault_labels,
};

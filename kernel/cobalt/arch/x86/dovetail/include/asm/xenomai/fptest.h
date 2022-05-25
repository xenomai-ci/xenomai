/*
 * Copyright (C) 2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#ifndef _COBALT_X86_ASM_FPTEST_H
#define _COBALT_X86_ASM_FPTEST_H

#include <linux/errno.h>
#include <asm/processor.h>
#include <asm/xenomai/wrappers.h>
#include <asm/xenomai/uapi/fptest.h>

/*
 * We do NOT support out-of-band FPU operations in kernel space for a
 * reason: this is a mess. Out-of-band FPU is just fine and makes a
 * lot of sense for many real-time applications, but you have to do
 * that from userland.
 */
static inline int fp_kernel_supported(void)
{
	return 0;
}

static inline void fp_init(void)
{
}

static inline int fp_linux_begin(void)
{
	kernel_fpu_begin();
	/*
	 * We need a clean context for testing the sanity of the FPU
	 * register stack across switches in fp_regs_check()
	 * (fildl->fistpl), which kernel_fpu_begin() does not
	 * guarantee us. Force this manually.
	 */
	asm volatile("fninit");

	return true;
}

static inline void fp_linux_end(void)
{
	kernel_fpu_end();
}

static inline int fp_detect(void)
{
	int features = 0;

	if (boot_cpu_has(X86_FEATURE_XMM2))
		features |= __COBALT_HAVE_SSE2;

	if (boot_cpu_has(X86_FEATURE_AVX))
		features |= __COBALT_HAVE_AVX;

	return features;
}

#endif /* _COBALT_X86_ASM_FPTEST_H */

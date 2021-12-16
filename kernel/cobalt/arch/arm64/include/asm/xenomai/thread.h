/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2015 Dmitriy Cherkasov <dmitriy@mperpetuo.com>
 * Copyright (C) 2021 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_ARM64_THREAD_H
#define _COBALT_ARM64_THREAD_H

#include <asm-generic/xenomai/thread.h>
#include <asm/dovetail.h>

#define xnarch_fault_pc(__regs)	((unsigned long)((__regs)->pc - 4)) /* XXX ? */

#define xnarch_fault_pf_p(__nr)	((__nr) == ARM64_TRAP_ACCESS)
#define xnarch_fault_bp_p(__nr)	((current->ptrace & PT_PTRACED) &&	\
				 ((__nr) == ARM64_TRAP_DEBUG || (__nr) == ARM64_TRAP_UNDI))

#define xnarch_fault_notify(__nr) (!xnarch_fault_bp_p(__nr))

#endif /* !_COBALT_ARM64_THREAD_H */

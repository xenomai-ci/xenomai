/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2015 Dmitriy Cherkasov <dmitriy@mperpetuo.com>
 * Copyright (C) 2021 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_ARM64_SYSCALL_H
#define _COBALT_ARM64_SYSCALL_H

#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm-generic/xenomai/syscall.h>

/*
 * Cobalt and Linux syscall numbers can be fetched from syscallno,
 * masking out the __COBALT_SYSCALL_BIT marker.
 */
#define __xn_reg_sys(__regs)	((unsigned long)(__regs)->syscallno)
#define __xn_syscall_p(regs)	((__xn_reg_sys(regs) & __COBALT_SYSCALL_BIT) != 0)

#ifdef CONFIG_XENO_ARCH_SYS3264
#define __xn_syscall(__regs)    __COBALT_SYSNR32emu(__xn_reg_sys(__regs)	\
				    & ~__COBALT_SYSCALL_BIT)
#else
#define __xn_syscall(__regs)	((unsigned long)(__xn_reg_sys(__regs) & ~__COBALT_SYSCALL_BIT))
#endif

#define __xn_reg_rval(__regs)	((__regs)->regs[0])
#define __xn_reg_pc(__regs)	((__regs)->pc)
#define __xn_reg_sp(__regs)	((__regs)->sp)

/*
 * Root syscall number with predicate (valid only if
 * !__xn_syscall_p(__regs)).
 */
#define __xn_rootcall_p(__regs, __code)			\
	({						\
		*(__code) = __xn_reg_sys(__regs);	\
		*(__code) < NR_syscalls;		\
	})

static inline void __xn_error_return(struct pt_regs *regs, int v)
{
	__xn_reg_rval(regs) = v;
}

static inline void __xn_status_return(struct pt_regs *regs, long v)
{
	__xn_reg_rval(regs) = v;
}

static inline int __xn_interrupted_p(struct pt_regs *regs)
{
	return __xn_reg_rval(regs) == -EINTR;
}

#endif /* !_COBALT_ARM64_SYSCALL_H */

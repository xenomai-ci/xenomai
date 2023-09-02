/*
 * Copyright (C) 2001-2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_X86_ASM_SYSCALL_H
#define _COBALT_X86_ASM_SYSCALL_H

#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm-generic/xenomai/syscall.h>

/*
 * Cobalt and Linux syscall numbers can be fetched from ORIG_AX,
 * masking out the __COBALT_SYSCALL_BIT marker.
 */
#define __xn_reg_sys(regs)    ((regs)->orig_ax)
#define __xn_reg_rval(regs)   ((regs)->ax)
#define __xn_reg_pc(regs)     ((regs)->ip)
#define __xn_reg_sp(regs)     ((regs)->sp)

#define __xn_syscall_p(regs)  (__xn_reg_sys(regs) & __COBALT_SYSCALL_BIT)
#ifdef CONFIG_XENO_ARCH_SYS3264
#define __xn_syscall(regs)    __COBALT_CALL32_SYSNR(__xn_reg_sys(regs)	\
				    & ~__COBALT_SYSCALL_BIT)
#else
#define __xn_syscall(regs)    (__xn_reg_sys(regs) & ~__COBALT_SYSCALL_BIT)
#endif

#ifdef CONFIG_IA32_EMULATION
#define __xn_nr_root_syscalls						\
	({								\
		struct thread_info *__ti = current_thread_info();	\
		__ti->status & TS_COMPAT ? IA32_NR_syscalls : NR_syscalls; \
	})
#else
#define __xn_nr_root_syscalls	NR_syscalls
#endif
/*
 * Root syscall number with predicate (valid only if
 * !__xn_syscall_p(__regs)).
 */
#define __xn_rootcall_p(__regs, __code)			\
	({						\
		*(__code) = __xn_reg_sys(__regs);	\
		*(__code) < __xn_nr_root_syscalls;	\
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

#endif /* !_COBALT_X86_ASM_SYSCALL_H */

/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
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
#ifndef _COBALT_ARM_DOVETAIL_SYSCALL_H
#define _COBALT_ARM_DOVETAIL_SYSCALL_H

#include <linux/errno.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <asm-generic/xenomai/syscall.h>

/*
 * Cobalt syscall numbers can be fetched from ARM_ORIG_r0 with ARM_r7
 * containing the Xenomai syscall marker, Linux syscalls directly from
 * ARM_r7. Since we have to work with Dovetail whilst remaining binary
 * compatible with applications built for the I-pipe, we retain the
 * old syscall signature based on receiving XENO_ARM_SYSCALL in
 * ARM_r7, possibly ORed with __COBALT_SYSCALL_BIT by Dovetail
 * (IPIPE_COMPAT mode).
 *
 * FIXME: We also have __COBALT_SYSCALL_BIT (equal to
 * __OOB_SYSCALL_BIT) present in the actual syscall number in r0,
 * which is pretty much useless. Oh, well...  When support for the
 * I-pipe is dropped, we may switch back to the regular convention
 * Dovetail abides by, with the actual syscall number into r7 ORed
 * with __OOB_SYSCALL_BIT, freeing r0 for passing a call argument.
 */
#define __xn_reg_sys(__regs)	((__regs)->ARM_ORIG_r0)
#define __xn_syscall_p(__regs)	(((__regs)->ARM_r7 & ~__COBALT_SYSCALL_BIT) == XENO_ARM_SYSCALL)
#define __xn_syscall(__regs)	(__xn_reg_sys(__regs) & ~__COBALT_SYSCALL_BIT)

/*
 * Root syscall number with predicate (valid only if
 * !__xn_syscall_p(__regs)).
 */
#define __xn_rootcall_p(__regs, __code)					\
	({								\
		*(__code) = (__regs)->ARM_r7;				\
		*(__code) < NR_syscalls || *(__code) >= __ARM_NR_BASE;	\
	})

#define __xn_reg_rval(__regs)	((__regs)->ARM_r0)
#define __xn_reg_pc(__regs)	((__regs)->ARM_ip)
#define __xn_reg_sp(__regs)	((__regs)->ARM_sp)

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

static inline
int xnarch_local_syscall(unsigned long a1, unsigned long a2,
			unsigned long a3, unsigned long a4,
			unsigned long a5)
{
	/* We need none of these with Dovetail. */
	return -ENOSYS;
}

#define pipeline_get_syscall_args pipeline_get_syscall_args
static inline void pipeline_get_syscall_args(struct task_struct *task,
					     struct pt_regs *regs,
					     unsigned long *args)
{
	args[0] = regs->ARM_r1;
	args[1] = regs->ARM_r2;
	args[2] = regs->ARM_r3;
	args[3] = regs->ARM_r4;
	args[4] = regs->ARM_r5;
}

#endif /* !_COBALT_ARM_DOVETAIL_SYSCALL_H */

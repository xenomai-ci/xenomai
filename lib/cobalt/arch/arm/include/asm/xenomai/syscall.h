/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _LIB_COBALT_ARM_SYSCALL_H
#define _LIB_COBALT_ARM_SYSCALL_H

#include <xeno_config.h>
#include <errno.h>
#include <cobalt/uapi/syscall.h>

/*
 * Some of the following macros have been adapted from Linux's
 * implementation of the syscall mechanism in <asm-arm/unistd.h>:
 */
#if defined(HAVE_TLS) && __GNUC__ == 4 && __GNUC_MINOR__ >= 3
#error TLS support (__thread) is broken with GCC >= 4.3, use --disable-tls when configuring
#endif

#define LOADARGS_0(syscode, dummy...)	\
	__a0 = (unsigned long) (syscode)
#define LOADARGS_1(syscode, arg1)	\
	LOADARGS_0(syscode);		\
	__a1 = (unsigned long) (arg1)
#define LOADARGS_2(syscode, arg1, arg2)	\
	LOADARGS_1(syscode, arg1);	\
	__a2 = (unsigned long) (arg2)
#define LOADARGS_3(syscode, arg1, arg2, arg3)	\
	LOADARGS_2(syscode,  arg1, arg2);	\
	__a3 = (unsigned long) (arg3)
#define LOADARGS_4(syscode,  arg1, arg2, arg3, arg4)	\
	LOADARGS_3(syscode,  arg1, arg2, arg3);		\
	__a4 = (unsigned long) (arg4)
#define LOADARGS_5(syscode, arg1, arg2, arg3, arg4, arg5)	\
	LOADARGS_4(syscode, arg1, arg2, arg3, arg4);		\
	__a5 = (unsigned long) (arg5)

#define CLOBBER_REGS_0 "r0"
#define CLOBBER_REGS_1 CLOBBER_REGS_0, "r1"
#define CLOBBER_REGS_2 CLOBBER_REGS_1, "r2"
#define CLOBBER_REGS_3 CLOBBER_REGS_2, "r3"
#define CLOBBER_REGS_4 CLOBBER_REGS_3, "r4"
#define CLOBBER_REGS_5 CLOBBER_REGS_4, "r5"

#define LOADREGS_0 __r0 = __a0
#define LOADREGS_1 LOADREGS_0; __r1 = __a1
#define LOADREGS_2 LOADREGS_1; __r2 = __a2
#define LOADREGS_3 LOADREGS_2; __r3 = __a3
#define LOADREGS_4 LOADREGS_3; __r4 = __a4
#define LOADREGS_5 LOADREGS_4; __r5 = __a5

#define ASM_INDECL_0							\
	unsigned long __a0; register unsigned long __r0  __asm__ ("r0");
#define ASM_INDECL_1 ASM_INDECL_0;					\
	unsigned long __a1; register unsigned long __r1  __asm__ ("r1")
#define ASM_INDECL_2 ASM_INDECL_1;					\
	unsigned long __a2; register unsigned long __r2  __asm__ ("r2")
#define ASM_INDECL_3 ASM_INDECL_2;					\
	unsigned long __a3; register unsigned long __r3  __asm__ ("r3")
#define ASM_INDECL_4 ASM_INDECL_3;					\
	unsigned long __a4; register unsigned long __r4  __asm__ ("r4")
#define ASM_INDECL_5 ASM_INDECL_4;					\
	unsigned long __a5; register unsigned long __r5  __asm__ ("r5")

#define ASM_INPUT_0 "0" (__r0)
#define ASM_INPUT_1 ASM_INPUT_0, "r" (__r1)
#define ASM_INPUT_2 ASM_INPUT_1, "r" (__r2)
#define ASM_INPUT_3 ASM_INPUT_2, "r" (__r3)
#define ASM_INPUT_4 ASM_INPUT_3, "r" (__r4)
#define ASM_INPUT_5 ASM_INPUT_4, "r" (__r5)

#define __sys2(x)	#x
#define __sys1(x)	__sys2(x)

#ifdef __ARM_EABI__
#define __SYS_REG_DECL unsigned long __r7 = XENO_ARM_SYSCALL
#define __SYS_REG_INPUT , [__r7] "r" (__r7)
#define __SYS_CALLOP "push {r7}; mov %%r7,%[__r7]; swi\t0; pop {r7}"
#else
#define __SYS_REG_DECL
#define __SYS_REG_INPUT
#define __NR_OABI_SYSCALL_BASE	0x900000
#define __SYS_CALLOP "swi\t" __sys1(__NR_OABI_SYSCALL_BASE + XENO_ARM_SYSCALL) ""
#endif

#define XENOMAI_DO_SYSCALL(nr, op, args...)				\
	({								\
		ASM_INDECL_##nr;					\
		__SYS_REG_DECL;						\
		LOADARGS_##nr(__xn_syscode(op), args);			\
		__asm__ __volatile__ ("" : /* */ : /* */ :		\
				      CLOBBER_REGS_##nr);		\
		LOADREGS_##nr;						\
		__asm__ __volatile__ (					\
			__SYS_CALLOP					\
			: "=r" (__r0)					\
			: ASM_INPUT_##nr __SYS_REG_INPUT		\
			: "memory");					\
		(int) __r0;						\
	})

#define XENOMAI_SYSCALL0(op)			\
	XENOMAI_DO_SYSCALL(0,op)
#define XENOMAI_SYSCALL1(op,a1)			\
	XENOMAI_DO_SYSCALL(1,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)		\
	XENOMAI_DO_SYSCALL(2,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)		\
	XENOMAI_DO_SYSCALL(3,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)	\
	XENOMAI_DO_SYSCALL(4,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5)	\
	XENOMAI_DO_SYSCALL(5,op,a1,a2,a3,a4,a5)
#define XENOMAI_SYSBIND(breq)			\
	XENOMAI_DO_SYSCALL(1,sc_cobalt_bind,breq)

#endif /* !_LIB_COBALT_ARM_SYSCALL_H */

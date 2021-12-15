/*
 * Copyright (C) 2001,2002,2003,2007 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _LIB_COBALT_X86_SYSCALL_H
#define _LIB_COBALT_X86_SYSCALL_H

#include <xeno_config.h>
#include <cobalt/uapi/syscall.h>

/* Some code pulled from glibc's inline syscalls. */

#ifdef __i386__

#ifdef CONFIG_XENO_X86_VSYSCALL
/*
 * This form relies on the kernel's vsyscall support in order to use
 * the most appropriate syscall entry instruction the CPU supports. We
 * also depend on the NPTL providing us a pointer to the vsyscall DSO
 * entry point, to which we branch to instead of issuing a trap.
 * We assume this pointer to be available at %gs:0x10.
 */
#define DOSYSCALL  "call *%%gs:0x10\n\t"
#else /* CONFIG_XENO_X86_VSYSCALL */
#define DOSYSCALL  "int $0x80\n\t"
#endif /* CONFIG_XENO_X86_VSYSCALL */

/* The one that cannot fail. */
#define DOSYSCALLSAFE  "int $0x80\n\t"

asm (".L__X'%ebx = 1\n\t"
     ".L__X'%ecx = 2\n\t"
     ".L__X'%edx = 2\n\t"
     ".L__X'%eax = 3\n\t"
     ".L__X'%esi = 3\n\t"
     ".L__X'%edi = 3\n\t"
     ".L__X'%ebp = 3\n\t"
     ".L__X'%esp = 3\n\t"
     ".macro bpushl name reg\n\t"
     ".if 1 - \\name\n\t"
     ".if 2 - \\name\n\t"
     "pushl %ebx\n\t"
     ".else\n\t"
     "xchgl \\reg, %ebx\n\t"
     ".endif\n\t"
     ".endif\n\t"
     ".endm\n\t"
     ".macro bpopl name reg\n\t"
     ".if 1 - \\name\n\t"
     ".if 2 - \\name\n\t"
     "popl %ebx\n\t"
     ".else\n\t"
     "xchgl \\reg, %ebx\n\t"
     ".endif\n\t"
     ".endif\n\t"
     ".endm\n\t"
     ".macro bmovl name reg\n\t"
     ".if 1 - \\name\n\t"
     ".if 2 - \\name\n\t"
     "movl \\reg, %ebx\n\t"
     ".endif\n\t"
     ".endif\n\t"
     ".endm\n\t");

#define XENOMAI_DO_SYSCALL(nr, op, args...)			\
({								\
	unsigned __resultvar;					\
	asm volatile (						\
		LOADARGS_##nr					\
		"movl %1, %%eax\n\t"				\
		DOSYSCALL					\
		RESTOREARGS_##nr				\
		: "=a" (__resultvar)				\
		: "g" (__xn_syscode(op)) ASMFMT_##nr(args)	\
		: "memory", "cc");				\
	(int) __resultvar;					\
})

#define XENOMAI_DO_SYSCALL_SAFE(nr, op, args...)		\
({								\
	unsigned __resultvar;					\
	asm volatile (						\
		LOADARGS_##nr					\
		"movl %1, %%eax\n\t"				\
		DOSYSCALLSAFE					\
		RESTOREARGS_##nr				\
		: "=a" (__resultvar)				\
		: "g" (__xn_syscode(op)) ASMFMT_##nr(args)	\
		: "memory", "cc");				\
	(int) __resultvar;					\
})

#define LOADARGS_0
#define LOADARGS_1 \
	"bpushl .L__X'%k2, %k2\n\t" \
	"bmovl .L__X'%k2, %k2\n\t"
#define LOADARGS_2	LOADARGS_1
#define LOADARGS_3	LOADARGS_1
#define LOADARGS_4	LOADARGS_1
#define LOADARGS_5	LOADARGS_1

#define RESTOREARGS_0
#define RESTOREARGS_1 \
	"bpopl .L__X'%k2, %k2\n\t"
#define RESTOREARGS_2	RESTOREARGS_1
#define RESTOREARGS_3	RESTOREARGS_1
#define RESTOREARGS_4	RESTOREARGS_1
#define RESTOREARGS_5	RESTOREARGS_1

#define ASMFMT_0()
#define ASMFMT_1(arg1) \
	, "acdSD" (arg1)
#define ASMFMT_2(arg1, arg2) \
	, "adSD" (arg1), "c" (arg2)
#define ASMFMT_3(arg1, arg2, arg3) \
	, "aSD" (arg1), "c" (arg2), "d" (arg3)
#define ASMFMT_4(arg1, arg2, arg3, arg4) \
	, "aD" (arg1), "c" (arg2), "d" (arg3), "S" (arg4)
#define ASMFMT_5(arg1, arg2, arg3, arg4, arg5) \
	, "a" (arg1), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)

#define XENOMAI_SYSBIND(breq) \
	XENOMAI_DO_SYSCALL_SAFE(1, sc_cobalt_bind, breq)

#else /* x86_64 */

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 3)
#define LOAD_ARGS_0()	asm volatile ("" : /* */ : /* */ : "memory");
#else
#define LOAD_ARGS_0()
#endif
#define LOAD_REGS_0
#define ASM_ARGS_0

#define LOAD_ARGS_1(a1)					\
	long int __arg1 = (long) (a1);			\
	LOAD_ARGS_0()
#define LOAD_REGS_1					\
	register long int _a1 asm ("rdi") = __arg1;	\
	LOAD_REGS_0
#define ASM_ARGS_1	ASM_ARGS_0, "r" (_a1)

#define LOAD_ARGS_2(a1, a2)				\
	long int __arg2 = (long) (a2);			\
	LOAD_ARGS_1(a1)
#define LOAD_REGS_2					\
	register long int _a2 asm ("rsi") = __arg2;	\
	LOAD_REGS_1
#define ASM_ARGS_2	ASM_ARGS_1, "r" (_a2)

#define LOAD_ARGS_3(a1, a2, a3)				\
	long int __arg3 = (long) (a3);			\
	LOAD_ARGS_2 (a1, a2)
#define LOAD_REGS_3					\
	register long int _a3 asm ("rdx") = __arg3;	\
	LOAD_REGS_2
#define ASM_ARGS_3	ASM_ARGS_2, "r" (_a3)

#define LOAD_ARGS_4(a1, a2, a3, a4)			\
	long int __arg4 = (long) (a4);			\
	LOAD_ARGS_3 (a1, a2, a3)
#define LOAD_REGS_4					\
	register long int _a4 asm ("r10") = __arg4;	\
	LOAD_REGS_3
#define ASM_ARGS_4	ASM_ARGS_3, "r" (_a4)

#define LOAD_ARGS_5(a1, a2, a3, a4, a5)			\
	long int __arg5 = (long) (a5);			\
	LOAD_ARGS_4 (a1, a2, a3, a4)
#define LOAD_REGS_5					\
	register long int _a5 asm ("r8") = __arg5;	\
	LOAD_REGS_4
#define ASM_ARGS_5	ASM_ARGS_4, "r" (_a5)

#define DO_SYSCALL(name, nr, args...)			\
({							\
	unsigned long __resultvar;			\
	LOAD_ARGS_##nr(args)				\
	LOAD_REGS_##nr					\
	asm volatile (					\
		"syscall\n\t"				\
		: "=a" (__resultvar)			\
		: "0" (name) ASM_ARGS_##nr		\
		: "memory", "cc", "r11", "cx");		\
	(int) __resultvar;				\
})

#define XENOMAI_DO_SYSCALL(nr, op, args...) \
	DO_SYSCALL(__xn_syscode(op), nr, args)

#define XENOMAI_SYSBIND(breq) \
	XENOMAI_DO_SYSCALL(1, sc_cobalt_bind, breq)

#endif /* x86_64 */

#define XENOMAI_SYSCALL0(op)			XENOMAI_DO_SYSCALL(0,op)
#define XENOMAI_SYSCALL1(op,a1)			XENOMAI_DO_SYSCALL(1,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)		XENOMAI_DO_SYSCALL(2,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)		XENOMAI_DO_SYSCALL(3,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)	XENOMAI_DO_SYSCALL(4,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5)	XENOMAI_DO_SYSCALL(5,op,a1,a2,a3,a4,a5)

#endif /* !_LIB_COBALT_X86_SYSCALL_H */

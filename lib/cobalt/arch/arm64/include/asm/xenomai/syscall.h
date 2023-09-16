/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) Siemens AG, 2021
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
#ifndef _LIB_COBALT_ARM64_SYSCALL_H
#define _LIB_COBALT_ARM64_SYSCALL_H

#include <xeno_config.h>
#include <errno.h>
#include <cobalt/uapi/syscall.h>
#include <boilerplate/compiler.h>

#define __xn_syscall_args0
#define __xn_syscall_args1 , unsigned long __a1
#define __xn_syscall_args2 __xn_syscall_args1, unsigned long __a2
#define __xn_syscall_args3 __xn_syscall_args2, unsigned long __a3
#define __xn_syscall_args4 __xn_syscall_args3, unsigned long __a4
#define __xn_syscall_args5 __xn_syscall_args4, unsigned long __a5

#define __emit_syscall0(__args...)					\
	register unsigned int __scno __asm__("w8") = __xn_syscode(__op); \
	register unsigned long __res __asm__("x0");			\
	__asm__ __volatile__ (						\
		"svc 0;\n\t"						\
		: "=r" (__res)						\
		: "r" (__scno), ##__args				\
		: "cc", "memory");					\
	return __res
#define __emit_syscall1(__args...)					\
	register unsigned long __x0 __asm__("x0") = __a1;		\
	__emit_syscall0("r" (__x0),  ##__args)
#define __emit_syscall2(__args...)					\
	register unsigned long __x1 __asm__("x1") = __a2;		\
	__emit_syscall1("r" (__x1), ##__args)
#define __emit_syscall3(__args...)					\
	register unsigned long __x2 __asm__("x2") = __a3;		\
	__emit_syscall2("r" (__x2), ##__args)
#define __emit_syscall4(__args...)					\
	register unsigned long __x3 __asm__("x3") = __a4;		\
	__emit_syscall3("r" (__x3), ##__args)
#define __emit_syscall5(__args...)	\
	register unsigned long __x4 __asm__("x4") = __a5;		\
	__emit_syscall4("r" (__x4), ##__args)

#define DEFINE_XENOMAI_SYSCALL(__argnr)					\
static inline long __attribute__((always_inline))			\
__xenomai_do_syscall##__argnr(unsigned int __op				\
			      __xn_syscall_args##__argnr)		\
{									\
	__emit_syscall##__argnr();					\
}

DEFINE_XENOMAI_SYSCALL(0)
DEFINE_XENOMAI_SYSCALL(1)
DEFINE_XENOMAI_SYSCALL(2)
DEFINE_XENOMAI_SYSCALL(3)
DEFINE_XENOMAI_SYSCALL(4)
DEFINE_XENOMAI_SYSCALL(5)

#define XENOMAI_SYSCALL0(__op)					\
	__xenomai_do_syscall0(__op)
#define XENOMAI_SYSCALL1(__op, __a1)				\
	__xenomai_do_syscall1(__op,				\
			      (unsigned long)__a1)
#define XENOMAI_SYSCALL2(__op, __a1, __a2)			\
	__xenomai_do_syscall2(__op,				\
			      (unsigned long)__a1,		\
			      (unsigned long)__a2)
#define XENOMAI_SYSCALL3(__op, __a1, __a2, __a3)		\
	__xenomai_do_syscall3(__op,				\
			      (unsigned long)__a1,		\
			      (unsigned long)__a2,		\
			      (unsigned long)__a3)
#define XENOMAI_SYSCALL4(__op, __a1, __a2, __a3, __a4)		\
	__xenomai_do_syscall4(__op,				\
			      (unsigned long)__a1,		\
			      (unsigned long)__a2,		\
			      (unsigned long)__a3,		\
			      (unsigned long)__a4)
#define XENOMAI_SYSCALL5(__op, __a1, __a2, __a3, __a4, __a5)	\
	__xenomai_do_syscall5(__op,				\
			      (unsigned long)__a1,		\
			      (unsigned long)__a2,		\
			      (unsigned long)__a3,		\
			      (unsigned long)__a4,		\
			      (unsigned long)__a5)
#define XENOMAI_SYSBIND(__breq)					\
	__xenomai_do_syscall1(sc_cobalt_bind,			\
			      (unsigned long)__breq)

#define XENOMAI_BUILD_SIGRETURN()					\
	__asm__ (							\
		".text\n\t"						\
		"__cobalt_sigreturn:\n\t"				\
		"mov w8,#" __stringify(sc_cobalt_sigreturn) "\n\t"	\
		"orr w8,w8,#" __stringify(__COBALT_SYSCALL_BIT) "\n\t"	\
		"svc 0\n\t")

#endif /* !_LIB_COBALT_ARM64_SYSCALL_H */

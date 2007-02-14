/*
 * Copyright (C) 2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_X86_64_SWITCH_H
#define _XENO_ASM_X86_64_SWITCH_H

#ifndef __KERNEL__
#error "Pure kernel header included from user-space!"
#endif

struct xnarch_x8664_initstack {

	unsigned long rbp;
	unsigned long eflags;
	unsigned long arg;
	unsigned long entry;
};

#define __SWITCH_CLOBBER_LIST  , "r12", "r13", "r14", "r15"

#define xnarch_switch_threads(prev,next,p_rsp,n_rsp,p_rip,n_rip)	\
	asm volatile("pushfq\n\t"					\
		     "pushq	%%rbp\n\t"				\
		     "movq	%%rsi, %%rbp\n\t"			\
		     "movq	%%rsp, (%%rdx)\n\t"			\
		     "movq	$1f, (%%rax)\n\t"			\
		     "movq	(%%rcx), %%rsp\n\t"			\
		     "pushq	(%%rbx)\n\t"				\
		     "testq	%%rsi, %%rsi\n\t"			\
		     "jnz	__switch_to\n\t"			\
		     "ret\n\t"						\
		     "1:\n\t"						\
		     "movq	%%rbp, %%rsi\n\t"			\
		     "popq	%%rbp\n\t"				\
		     "popfq\n\t"					\
		     : /* no output */					\
		     : "S" (next), "D" (prev), "d" (p_rsp), "c" (n_rsp), \
		       "a" (p_rip), "b" (n_rip)	\
		     : "memory", "cc" __SWITCH_CLOBBER_LIST)

#define xnarch_thread_head()						\
	asm volatile(".globl __thread_head\n\t"				\
		     "__thread_head:\n\t"				\
		     "popq	%%rbp\n\t"				\
		     "popfq\n\t"					\
		     "popq	%%rdi\n\t"				\
		     "ret\n\t"						\
		     : /* no output */					\
		     : /* no input */					\
		     : "cc", "memory", "rdi")

asmlinkage void __thread_head(void);

#define xnarch_switch_clobber()		\
	asm volatile(""				\
		     : /* no output */		\
		     : /* no input */		\
		     : "cc", "memory" __SWITCH_CLOBBER_LIST)

#endif /* !_XENO_ASM_X86_64_SWITCH_H */

/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _XENO_ASM_BLACKFIN_SYSCALL_H
#define _XENO_ASM_BLACKFIN_SYSCALL_H

#include <asm-generic/xenomai/syscall.h>

/* The way we mangle Xenomai syscalls with our multiplexer
   marker. Note: watch out for the p0 sign convention used by Linux
   (i.e. negative syscall number in orig_p0 meaning "non-syscall
   entry"). */
#define __xn_mux_code(shifted_id,op) (shifted_id|((op << 16) & 0xff0000)|(__xn_sys_mux & 0xffff))
#define __xn_mux_shifted_id(id) (id << 24)

/* Local syscalls -- the braindamage thing about this arch is the
   absence of atomic ops usable from user-space; so we export what
   we need as syscalls implementing those ops from kernel space. Sigh... */
#define __xn_lsys_xchg        0

#ifdef __KERNEL__

#include <linux/errno.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>

/* Register mapping for accessing syscall args. */

#define __xn_reg_mux(regs)    ((regs)->orig_p0)
#define __xn_reg_rval(regs)   ((regs)->r0)
#define __xn_reg_arg1(regs)   ((regs)->r0)
#define __xn_reg_arg2(regs)   ((regs)->r1)
#define __xn_reg_arg3(regs)   ((regs)->r2)
#define __xn_reg_arg4(regs)   ((regs)->r3)
#define __xn_reg_arg5(regs)   ((regs)->r4)
#define __xn_reg_sigp(regs)   ((regs)->r5)

#define __xn_reg_mux_p(regs)        ((__xn_reg_mux(regs) & 0xffff) == __xn_sys_mux)
#define __xn_mux_id(regs)           ((__xn_reg_mux(regs) >> 24) & 0xff)
#define __xn_mux_op(regs)           ((__xn_reg_mux(regs) >> 16) & 0xff)

/* Purposedly used inlines and not macros for the following routines
   so that we don't risk spurious side-effects on the value arg. */

static inline void __xn_success_return(struct pt_regs *regs, int v)
{
    __xn_reg_rval(regs) = v;
}

static inline void __xn_error_return(struct pt_regs *regs, int v)
{
    __xn_reg_rval(regs) = v;
}

static inline void __xn_status_return(struct pt_regs *regs, int v)
{
    __xn_reg_rval(regs) = v;
}

static inline int __xn_interrupted_p(struct pt_regs *regs)
{
    return __xn_reg_rval(regs) == -EINTR;
}

#else /* !__KERNEL__ */

#include <errno.h>

/*
 * The following code defines an inline syscall mechanism used by
 * Xenomai's real-time interfaces to invoke the skin module
 * services in kernel space.
 */

#define __emit_syscall0(muxcode, sigp)					\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"r5=%2;\n\t"						\
		"p0=%1;\n\t"						\
		"excpt 0;\n\t"						\
		"%0=r0;\n\t"						\
		: "=da" (__res)						\
		: "d" (muxcode),					\
		  "a" ((long)(sigp))					\
		: "CC","R5","P0","memory");				\
	__res;								\
})

#define __emit_syscall1(muxcode, sigp, a1)				\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"r5=%3;\n\t"						\
		"r0=%2;\n\t"						\
		"p0=%1;\n\t"						\
		"excpt 0;\n\t"						\
		"%0=r0;\n\t"						\
		: "=da" (__res)						\
		: "d" (muxcode),					\
		  "a" ((long)(a1)),					\
		  "a" ((long)(sigp))					\
		: "CC","R0","R5","P0","memory");			\
	__res;								\
})

#define __emit_syscall2(muxcode, sigp, a1, a2)				\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"r5=%4;\n\t"						\
		"r1=%3;\n\t"						\
		"r0=%2;\n\t"						\
		"p0=%1;\n\t"						\
		"excpt 0;\n\t"						\
		"%0=r0;\n\t"						\
		: "=da" (__res)						\
		: "d" (muxcode),					\
		  "a" ((long)(a1)),					\
		  "a" ((long)(a2)),					\
		  "a" ((long)(sigp))					\
		: "CC","R0","R1","R5","P0","memory");			\
	__res;								\
})

#define __emit_syscall3(muxcode, sigp, a1, a2, a3)			\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"r5=%5;\n\t"						\
		"r2=%4;\n\t"						\
		"r1=%3;\n\t"						\
		"r0=%2;\n\t"						\
		"p0=%1;\n\t"						\
		"excpt 0;\n\t"						\
		"%0=r0;\n\t"						\
		: "=da" (__res)						\
		: "d"   (muxcode),					\
		  "a"   ((long)(a1)),					\
		  "a"   ((long)(a2)),					\
		  "a"   ((long)(a3)),					\
		  "a"   ((long)(sigp))					\
		: "CC","R0","R1","R2","R5","P0","memory");		\
	__res;								\
})

#define __emit_syscall4(muxcode, sigp, a1, a2, a3, a4)			\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"r5=%6;\n\t"						\
		"r3=%5;\n\t"						\
		"r2=%4;\n\t"						\
		"r1=%3;\n\t"						\
		"r0=%2;\n\t"						\
		"p0=%1;\n\t"						\
		"excpt 0;\n\t"						\
		"%0=r0;\n\t"						\
		: "=da" (__res)						\
		: "d"  (muxcode),					\
		  "a"  ((long)(a1)),					\
		  "a"  ((long)(a2)),					\
		  "a"  ((long)(a3)),					\
		  "a"  ((long)(a4)),					\
		  "a"  ((long)(sigp))					\
		: "CC","R0","R1","R2","R3","R5","P0","memory");		\
	__res;								\
})

#define __emit_syscall5(muxcode, sigp, a1, a2, a3, a4, a5)		\
({									\
	long __res;							\
	__asm__ __volatile__ (						\
		"r5=%7;\n\t"						\
		"r4=%6;\n\t"						\
		"r3=%5;\n\t"						\
		"r2=%4;\n\t"						\
		"r1=%3;\n\t"						\
		"r0=%2;\n\t"						\
		"p0=%1;\n\t"						\
		"excpt 0;\n\t"						\
		"%0=r0;\n\t"						\
		: "=da" (__res)						\
		: "d"  (muxcode),					\
		  "rm"  ((long)(a1)),					\
		  "rm"  ((long)(a2)),					\
		  "rm"  ((long)(a3)),					\
		  "rm"  ((long)(a4)),					\
		  "rm"  ((long)(a5)),					\
		  "rm"  ((long)(sigp))					\
		: "CC","R0","R1","R2","R3","R4","R5","P0","memory");	\
	__res;								\
})

#define XENOMAI_DO_SYSCALL_INNER(nr, shifted_id, op, args...)		\
    __emit_syscall##nr(__xn_mux_code(shifted_id,op), ##args)

#define XENOMAI_DO_SYSCALL(nr, shifted_id, op, args...)			\
	({								\
		int __err__, __res__ = -ERESTART;			\
		struct xnsig __sigs__;					\
									\
		do {							\
			__sigs__.nsigs = 0;				\
			__err__ = XENOMAI_DO_SYSCALL_INNER(nr, shifted_id, \
							   op, &__sigs__, ##args); \
			__res__ = xnsig_dispatch(&__sigs__, __res__, __err__); \
			while (__sigs__.nsigs && __sigs__.remaining) {	\
				__sigs__.nsigs = 0;			\
				__err__ = XENOMAI_DO_SYSCALL_INNER	\
					(0, 0, __xn_sys_get_next_sigs, &__sigs__); \
				__res__ = xnsig_dispatch_next(&__sigs__, __res__, __err__); \
			}						\
		} while (__res__ == -ERESTART);				\
		__res__;						\
	})

#define XENOMAI_SYSCALL0(op)                XENOMAI_DO_SYSCALL(0,0,op)
#define XENOMAI_SYSCALL1(op,a1)             XENOMAI_DO_SYSCALL(1,0,op,a1)
#define XENOMAI_SYSCALL2(op,a1,a2)          XENOMAI_DO_SYSCALL(2,0,op,a1,a2)
#define XENOMAI_SYSCALL3(op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,0,op,a1,a2,a3)
#define XENOMAI_SYSCALL4(op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,0,op,a1,a2,a3,a4)
#define XENOMAI_SYSCALL5(op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,0,op,a1,a2,a3,a4,a5)
#define XENOMAI_SYSBIND(a1,a2,a3,a4)        XENOMAI_DO_SYSCALL(4,0,__xn_sys_bind,a1,a2,a3,a4)

#define XENOMAI_SKINCALL0(id,op)                XENOMAI_DO_SYSCALL(0,id,op)
#define XENOMAI_SKINCALL1(id,op,a1)             XENOMAI_DO_SYSCALL(1,id,op,a1)
#define XENOMAI_SKINCALL2(id,op,a1,a2)          XENOMAI_DO_SYSCALL(2,id,op,a1,a2)
#define XENOMAI_SKINCALL3(id,op,a1,a2,a3)       XENOMAI_DO_SYSCALL(3,id,op,a1,a2,a3)
#define XENOMAI_SKINCALL4(id,op,a1,a2,a3,a4)    XENOMAI_DO_SYSCALL(4,id,op,a1,a2,a3,a4)
#define XENOMAI_SKINCALL5(id,op,a1,a2,a3,a4,a5) XENOMAI_DO_SYSCALL(5,id,op,a1,a2,a3,a4,a5)

static inline unsigned long long __xn_rdtsc (void)
{
    union {
	struct {
	    unsigned long l;
	    unsigned long h;
	} s;
	unsigned long long t;
    } u;
    unsigned long cy2;

    __asm__ __volatile__ (	"1: %0 = CYCLES2\n"
				"%1 = CYCLES\n"
				"%2 = CYCLES2\n"
				"CC = %2 == %0\n"
				"if !cc jump 1b\n"
				:"=d" (u.s.h),
				"=d" (u.s.l),
				"=d" (cy2)
				: /*no input*/ : "cc");
    return u.t;
}

/* uClibc does not provide pthread_atfork() for this arch; provide it
   here. Note: let the compiler decides whether it wants to actually
   inline this routine, i.e. do not force always_inline. */
inline __attribute__((weak)) int pthread_atfork(void (*prepare)(void),
						void (*parent)(void),
						void (*child)(void))
{
	return 0;
}

#include <errno.h>

inline __attribute__((weak)) int shm_open(const char *name,
					  int oflag,
					  mode_t mode)
{
	errno = ENOSYS;
	return -1;
}

inline __attribute__((weak)) int shm_unlink(const char *name)
{
	errno = ENOSYS;
	return -1;
}

#endif /* __KERNEL__ */

#endif /* !_XENO_ASM_BLACKFIN_SYSCALL_H */

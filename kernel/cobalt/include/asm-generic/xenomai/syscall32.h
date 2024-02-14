/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_ASM_GENERIC_SYSCALL32_H
#define _COBALT_ASM_GENERIC_SYSCALL32_H

#ifdef CONFIG_XENO_ARCH_SYS3264

#define __COBALT_COMPAT32_BASE		256 /* Power of two. */

#define __COBALT_SYSNR32emu(__reg)					\
	({								\
		long __nr = __reg;					\
		if (in_compat_syscall())				\
			__nr += __COBALT_COMPAT32_BASE;			\
		__nr;							\
	})

#define __COBALT_COMPAT32emu(__reg)					\
	(in_compat_syscall() ? __COBALT_COMPAT_BIT : 0)

#if __NR_COBALT_SYSCALLS > __COBALT_COMPAT32_BASE
#error "__NR_COBALT_SYSCALLS > __COBALT_COMPAT32_BASE"
#endif

#define __syshand32emu__(__name)	\
	((cobalt_syshand)(void (*)(void))(CoBaLt32emu_ ## __name))

#define __COBALT_CALL32emu_INITHAND(__handler)	\
	[__COBALT_COMPAT32_BASE ... __COBALT_COMPAT32_BASE + __NR_COBALT_SYSCALLS-1] = __handler,

#define __COBALT_CALL32emu_INITMODE(__mode)	\
	[__COBALT_COMPAT32_BASE ... __COBALT_COMPAT32_BASE + __NR_COBALT_SYSCALLS-1] = __mode,

/* compat default entry (no thunk) */
#define __COBALT_CALL32emu_ENTRY(__name, __handler)		\
	[sc_cobalt_ ## __name + __COBALT_COMPAT32_BASE] = __handler,

/* compat thunk installation */
#define __COBALT_CALL32emu_THUNK(__name)	\
	__COBALT_CALL32emu_ENTRY(__name, __syshand32emu__(__name))

/* compat thunk implementation. */
#define COBALT_SYSCALL32emu(__name, __mode, __args)	\
	long CoBaLt32emu_ ## __name __args

/* compat thunk declaration. */
#define COBALT_SYSCALL32emu_DECL(__name, __args)	\
	long CoBaLt32emu_ ## __name __args

#else /* !CONFIG_XENO_ARCH_SYS3264 */

/* compat support disabled. */

#define __COBALT_SYSNR32emu(__reg)	(__reg)

#define __COBALT_COMPAT32emu(__reg)	0

#define __COBALT_CALL32emu_INITHAND(__handler)

#define __COBALT_CALL32emu_INITMODE(__mode)

#define __COBALT_CALL32emu_ENTRY(__name, __handler)

#define __COBALT_CALL32emu_THUNK(__name)

#define COBALT_SYSCALL32emu_DECL(__name, __args)

#endif /* !CONFIG_XENO_ARCH_SYS3264 */

#define __COBALT_CALL32_ENTRY(__name, __handler)	\
	__COBALT_CALL32emu_ENTRY(__name, __handler)

#define __COBALT_CALL32_INITHAND(__handler)	\
	__COBALT_CALL32emu_INITHAND(__handler)

#define __COBALT_CALL32_INITMODE(__mode)	\
	__COBALT_CALL32emu_INITMODE(__mode)

/* Already checked for __COBALT_SYSCALL_BIT */
#define __COBALT_CALL32_SYSNR(__reg)	__COBALT_SYSNR32emu(__reg)

#define __COBALT_CALL_COMPAT(__reg)	__COBALT_COMPAT32emu(__reg)

#endif /* !_COBALT_ASM_GENERIC_SYSCALL32_H */

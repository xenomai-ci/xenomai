/*
 * Copyright (C) 2001-2013 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004-2006 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#ifndef _COBALT_X86_ASM_THREAD_H
#define _COBALT_X86_ASM_THREAD_H

#include <linux/dovetail.h>
#include <asm-generic/xenomai/thread.h>
#include <asm/traps.h>

struct xnarchtcb {
	struct xntcb core;
	struct dovetail_altsched_context altsched;
};

#define xnarch_fault_pc(__regs)		((__regs)->ip)
#define xnarch_fault_pf_p(__nr)		((__nr) == X86_TRAP_PF)
#define xnarch_fault_bp_p(__nr)		((current->ptrace & PT_PTRACED) &&	\
					 ((__nr) == X86_TRAP_DB || (__nr) == X86_TRAP_BP))
#define xnarch_fault_notify(__nr)	(!xnarch_fault_bp_p(__nr))

#endif /* !_COBALT_X86_ASM_THREAD_H */

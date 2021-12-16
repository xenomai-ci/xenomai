/*
 * Copyright (C) 2005 Stelian Pop
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
#ifndef _COBALT_ARM_THREAD_H
#define _COBALT_ARM_THREAD_H

#include <asm-generic/xenomai/thread.h>
#include <asm/traps.h>

#define xnarch_fault_pc(__regs)	((__regs)->ARM_pc - (thumb_mode(__regs) ? 2 : 4))
#define xnarch_fault_pf_p(__nr)	((__nr) == ARM_TRAP_ACCESS)
#define xnarch_fault_bp_p(__nr)	((current->ptrace & PT_PTRACED) &&	\
					((__nr) == ARM_TRAP_BREAK ||	\
						(__nr) == ARM_TRAP_UNDEFINSTR))
#define xnarch_fault_notify(__nr) (!xnarch_fault_bp_p(__nr))

#endif /* !_COBALT_ARM_THREAD_H */

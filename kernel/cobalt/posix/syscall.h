/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _COBALT_POSIX_SYSCALL_H
#define _COBALT_POSIX_SYSCALL_H

#include <cobalt/uapi/syscall.h>

struct pt_regs;

/* Regular (native) syscall handler implementation. */
#define COBALT_SYSCALL(__name, __mode, __args)	\
	long CoBaLt_ ## __name __args

/* Regular (native) syscall handler declaration. */
#define COBALT_SYSCALL_DECL(__name, __args)	\
	long CoBaLt_ ## __name __args

#include <asm-generic/xenomai/syscall32.h>

int handle_head_syscall(bool caller_is_relaxed,
			struct pt_regs *regs);

int handle_root_syscall(struct pt_regs *regs);

#endif /* !_COBALT_POSIX_SYSCALL_H */

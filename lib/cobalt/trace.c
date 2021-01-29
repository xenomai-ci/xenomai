/*
 * Copyright (C) 2006 Jan Kiszka <jan.kiszka@web.de>.
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <errno.h>
#include <stdio.h>
#include <cobalt/uapi/kernel/trace.h>
#include <cobalt/trace.h>
#include <asm/xenomai/syscall.h>

int xntrace_max_begin(unsigned long v)
{
	return XENOMAI_SYSCALL2(sc_cobalt_trace, __xntrace_op_max_begin, v);
}

int xntrace_max_end(unsigned long v)
{
	return XENOMAI_SYSCALL2(sc_cobalt_trace, __xntrace_op_max_end, v);
}

int xntrace_max_reset(void)
{
	return XENOMAI_SYSCALL1(sc_cobalt_trace, __xntrace_op_max_reset);
}

int xntrace_user_start(void)
{
	return XENOMAI_SYSCALL1(sc_cobalt_trace, __xntrace_op_user_start);
}

int xntrace_user_stop(unsigned long v)
{
	return XENOMAI_SYSCALL2(sc_cobalt_trace, __xntrace_op_user_stop, v);
}

int xntrace_user_freeze(unsigned long v, int once)
{
	return XENOMAI_SYSCALL3(sc_cobalt_trace, __xntrace_op_user_freeze,
				v, once);
}

void xntrace_latpeak_freeze(int delay)
{
	XENOMAI_SYSCALL2(sc_cobalt_trace, __xntrace_op_latpeak_freeze, delay);
}

int xntrace_special(unsigned char id, unsigned long v)
{
	return XENOMAI_SYSCALL3(sc_cobalt_trace, __xntrace_op_special, id, v);
}

int xntrace_special_u64(unsigned char id, unsigned long long v)
{
	return XENOMAI_SYSCALL4(sc_cobalt_trace, __xntrace_op_special_u64, id,
				(unsigned long)(v >> 32),
				(unsigned long)(v & 0xFFFFFFFF));
}

int xnftrace_vprintf(const char *format, va_list args)
{
	char buf[256];
	int ret;

	ret = vsnprintf(buf, sizeof(buf), format, args);
	if (ret < 0)
		return ret;
	if (ret >= sizeof(buf))
		return -EOVERFLOW;

	return XENOMAI_SYSCALL1(sc_cobalt_ftrace_puts, buf);
}

int xnftrace_printf(const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = xnftrace_vprintf(format, args);
	va_end(args);

	return ret;
}

/*
 * Copyright (C) 2006 Jan Kiszka <jan.kiszka@web.de>.
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
#ifndef _COBALT_KERNEL_DOVETAIL_TRACE_H
#define _COBALT_KERNEL_DOVETAIL_TRACE_H

#include <linux/types.h>
#include <linux/kconfig.h>
#include <cobalt/uapi/kernel/trace.h>
#include <trace/events/cobalt-core.h>
#include <cobalt/kernel/assert.h>

static inline int xntrace_max_begin(unsigned long v)
{
	TODO();
	return 0;
}

static inline int xntrace_max_end(unsigned long v)
{
	TODO();
	return 0;
}

static inline int xntrace_max_reset(void)
{
	TODO();
	return 0;
}

static inline int xntrace_user_start(void)
{
	trace_cobalt_trigger("user-start");
	return 0;
}

static inline int xntrace_user_stop(unsigned long v)
{
	trace_cobalt_trace_longval(0, v);
	trace_cobalt_trigger("user-stop");
	return 0;
}

static inline int xntrace_user_freeze(unsigned long v, int once)
{
	trace_cobalt_trace_longval(0, v);
	trace_cobalt_trigger("user-freeze");
	return 0;
}

static inline void xntrace_latpeak_freeze(int delay)
{
	trace_cobalt_latpeak(delay);
	trace_cobalt_trigger("latency-freeze");
}

static inline int xntrace_special(unsigned char id, unsigned long v)
{
	trace_cobalt_trace_longval(id, v);
	return 0;
}

static inline int xntrace_special_u64(unsigned char id,
				unsigned long long v)
{
	trace_cobalt_trace_longval(id, v);
	return 0;
}

static inline int xntrace_pid(pid_t pid, short prio)
{
	trace_cobalt_trace_pid(pid, prio);
	return 0;
}

static inline int xntrace_tick(unsigned long delay_ticks) /* ns */
{
	trace_cobalt_tick_shot(delay_ticks);
	return 0;
}

static inline int xntrace_panic_freeze(void)
{
	TODO();
	return 0;
}

static inline int xntrace_panic_dump(void)
{
	TODO();
	return 0;
}

static inline bool xntrace_enabled(void)
{
	return IS_ENABLED(CONFIG_DOVETAIL_TRACE);
}

#endif /* !_COBALT_KERNEL_DOVETAIL_TRACE_H */

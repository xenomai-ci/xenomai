/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2013 Gilles Chanteperdrix <gch@xenomai.org>.
 *
 * ARM port
 *   Copyright (C) 2005 Stelian Pop
 *
 * Copyright (C) 2007 Sebastian Smolorz <sesmo@gmx.net>
 *	Support for TSC emulation in user space for decrementing counters
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
#ifndef _LIB_COBALT_ARM64_TSC_H
#define _LIB_COBALT_ARM64_TSC_H

#include <asm/xenomai/uapi/tsc.h>
#include <asm/xenomai/features.h>
#include <inttypes.h>
#include <sys/time.h>

typedef unsigned long long __xn_rdtsc_t(volatile unsigned *vaddr);
struct __xn_full_tscinfo {
	struct __xn_tscinfo kinfo;
};
extern struct __xn_full_tscinfo __xn_tscinfo;

static inline uint64_t get_counter(void)
{
        uint64_t cval;

	asm volatile("isb; mrs %0, cntvct_el0; isb; " : "=r" (cval) :: "memory");

	return cval;
}

static inline __attribute__((always_inline))
unsigned long long cobalt_read_legacy_tsc(void)
{
	return get_counter();
}

#endif /* !_LIB_COBALT_ARM64_TSC_H */
